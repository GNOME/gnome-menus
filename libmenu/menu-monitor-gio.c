/*
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2006 Mark McLoughlin
 * Copyright (C) 2007 Sebastian Dröge
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include "menu-monitor-backend.h"

#include <gio/gio.h>

#include "menu-util.h"

static gboolean
monitor_callback (GFileMonitor      *monitor,
                  GFile             *child,
                  GFile             *other_file,
                  GFileMonitorEvent eflags,
                  gpointer          user_data)
{
  MenuMonitorEventInfo *event_info;
  MenuMonitorEvent      event;
  MenuMonitor          *menu_monitor = (MenuMonitor *) user_data;

  event = MENU_MONITOR_EVENT_INVALID;
  switch (eflags)
    {
    case G_FILE_MONITOR_EVENT_CHANGED:
      event = MENU_MONITOR_EVENT_CHANGED;
      break;
    case G_FILE_MONITOR_EVENT_CREATED:
      event = MENU_MONITOR_EVENT_CREATED;
      break;
    case G_FILE_MONITOR_EVENT_DELETED:
      event = MENU_MONITOR_EVENT_DELETED;
      break;
    default:
      return TRUE;
    }

  event_info = g_new0 (MenuMonitorEventInfo, 1);

  event_info->path    = g_file_get_parse_name (child);
  event_info->event   = event;
  event_info->monitor = menu_monitor;

  menu_monitor_queue_event (event_info);

  return TRUE;
}

void
menu_monitor_backend_register_monitor (MenuMonitor *monitor)
{
  GFile *file;
  GFileMonitor *file_monitor;

  file = g_file_new_for_path (menu_monitor_get_path (monitor));

  if (file == NULL)
    {
      menu_verbose ("Not adding %s monitor on '%s', failed to create GFile\n",
                    menu_monitor_get_is_directory (monitor) ? "directory" : "file",
                    menu_monitor_get_path (monitor));
      return;
    }

  if (menu_monitor_get_is_directory (monitor))
      file_monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL);
  else
      file_monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL);

  g_object_unref (G_OBJECT (file));

  if (file_monitor == NULL)
    {
      menu_verbose ("Not adding %s monitor on '%s', failed to create monitor\n",
                    menu_monitor_get_is_directory (monitor) ? "directory" : "file",
                    menu_monitor_get_path (monitor));
      return;
    }

  g_signal_connect (file_monitor, "changed", G_CALLBACK (monitor_callback), monitor);

  menu_monitor_set_backend_data (monitor, file_monitor);
}

void
menu_monitor_backend_unregister_monitor (MenuMonitor *monitor)
{
  GFileMonitor *file_monitor;

  if ((file_monitor = menu_monitor_get_backend_data (monitor)) != NULL)
    {
      g_file_monitor_cancel (file_monitor);
      g_object_unref (file_monitor);
    }
}
