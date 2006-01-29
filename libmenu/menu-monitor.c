/*
 * Copyright (C) 2005 Red Hat, Inc.
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

#include "menu-monitor.h"

#include "menu-monitor-backend.h"
#include "menu-util.h"

struct MenuMonitor
{
  char  *path;
  guint  refcount;

  GSList *notifies;

  gpointer backend_data;

  guint is_directory : 1;
};

typedef struct
{
  MenuMonitorNotifyFunc notify_func;
  gpointer              user_data;
  guint                 refcount;
} MenuMonitorNotify;

static MenuMonitorNotify *menu_monitor_notify_ref   (MenuMonitorNotify *notify);
static void               menu_monitor_notify_unref (MenuMonitorNotify *notify);

static GHashTable *monitors_registry = NULL;
static guint       events_idle_handler = 0;
static GSList     *pending_events = NULL;

static void
invoke_notifies (MenuMonitor      *monitor,
		 MenuMonitorEvent  event,
		 const char       *path)
{
  GSList *copy;
  GSList *tmp;

  copy = g_slist_copy (monitor->notifies);
  g_slist_foreach (copy,
		   (GFunc) menu_monitor_notify_ref,
		   NULL);

  tmp = copy;
  while (tmp != NULL)
    {
      MenuMonitorNotify *notify = tmp->data;
      GSList            *next   = tmp->next;

      if (notify->notify_func)
	{
	  notify->notify_func (monitor, event, path, notify->user_data);
	}

      menu_monitor_notify_unref (notify);

      tmp = next;
    }

  g_slist_free (copy);
}

static gboolean
emit_events_in_idle (void)
{
  GSList *events_to_emit;
  GSList *tmp;

  events_to_emit = pending_events;

  pending_events = NULL;
  events_idle_handler = 0;

  tmp = events_to_emit;
  while (tmp != NULL)
    {
      MenuMonitorEventInfo *event_info = tmp->data;

      menu_monitor_ref (event_info->monitor);

      tmp = tmp->next;
    }
  
  tmp = events_to_emit;
  while (tmp != NULL)
    {
      MenuMonitorEventInfo *event_info = tmp->data;

      invoke_notifies (event_info->monitor,
		       event_info->event,
		       event_info->path);

      menu_monitor_unref (event_info->monitor);
      event_info->monitor = NULL;

      g_free (event_info->path);
      event_info->path = NULL;

      event_info->event = MENU_MONITOR_EVENT_INVALID;

      g_free (event_info);

      tmp = tmp->next;
    }

  g_slist_free (events_to_emit);

  return FALSE;
}

void
menu_monitor_queue_event (MenuMonitorEventInfo *event_info)
{
  pending_events = g_slist_append (pending_events, event_info);

  if (events_idle_handler == 0)
    {
      events_idle_handler = g_idle_add ((GSourceFunc) emit_events_in_idle, NULL);
    }
}

gboolean
menu_monitor_get_is_directory (MenuMonitor *monitor)
{
  return monitor->is_directory;
}

void
menu_monitor_set_backend_data (MenuMonitor *monitor,
			       gpointer     backend_data)
{
  monitor->backend_data = backend_data;
}

gpointer
menu_monitor_get_backend_data (MenuMonitor *monitor)
{
  return monitor->backend_data;
}

static inline char *
get_registry_key (const char *path,
		  gboolean    is_directory)
{
  return g_strdup_printf ("%s:%s",
			  path,
			  is_directory ? "<dir>" : "<file>");
}

static MenuMonitor *
lookup_monitor (const char *path,
		gboolean    is_directory)
{
  MenuMonitor *retval;
  char        *registry_key;

  retval = NULL;

  registry_key = get_registry_key (path, is_directory);

  if (monitors_registry == NULL)
    {
      monitors_registry = g_hash_table_new_full (g_str_hash,
						 g_str_equal,
						 g_free,
						 NULL);
    }
  else
    {
      retval = g_hash_table_lookup (monitors_registry, registry_key);
    }

  if (retval == NULL)
    {
      retval = g_new0 (MenuMonitor, 1);

      retval->path         = g_strdup (path);
      retval->refcount     = 1;
      retval->is_directory = is_directory != FALSE;

      menu_monitor_backend_register_monitor (retval);

      g_hash_table_insert (monitors_registry, registry_key, retval);

      return retval;
    }
  else
    {
      g_free (registry_key);

      return menu_monitor_ref (retval);
    }
}

MenuMonitor *
menu_get_file_monitor (const char *path)
{
  g_return_val_if_fail (path != NULL, NULL);

  return lookup_monitor (path, FALSE);
}

MenuMonitor *
menu_get_directory_monitor (const char *path)
{
  g_return_val_if_fail (path != NULL, NULL);

  return lookup_monitor (path, TRUE);
}

MenuMonitor *
menu_monitor_ref (MenuMonitor *monitor)
{
  g_return_val_if_fail (monitor != NULL, NULL);
  g_return_val_if_fail (monitor->refcount > 0, NULL);

  monitor->refcount++;

  return monitor;
}

static void
menu_monitor_clear_pending_events (MenuMonitor *monitor)
{
  GSList *tmp;

  tmp = pending_events;
  while (tmp != NULL)
    {
      MenuMonitorEventInfo *event_info = tmp->data;
      GSList               *next = tmp->next;

      if (event_info->monitor == monitor)
	{
	  pending_events = g_slist_delete_link (pending_events, tmp);

	  g_free (event_info->path);
	  event_info->path = NULL;

	  event_info->monitor = NULL;
	  event_info->event   = MENU_MONITOR_EVENT_INVALID;

	  g_free (event_info);
	}

      tmp = next;
    }
}

void
menu_monitor_unref (MenuMonitor *monitor)
{
  char *registry_key;

  g_return_if_fail (monitor != NULL);
  g_return_if_fail (monitor->refcount > 0);

  if (--monitor->refcount > 0)
    return;

  registry_key = get_registry_key (monitor->path, monitor->is_directory);
  g_hash_table_remove (monitors_registry, registry_key);
  g_free (registry_key);

  menu_monitor_backend_unregister_monitor (monitor);

  g_slist_foreach (monitor->notifies, (GFunc) menu_monitor_notify_unref, NULL);
  g_slist_free (monitor->notifies);
  monitor->notifies = NULL;

  menu_monitor_clear_pending_events (monitor);

  g_free (monitor->path);
  monitor->path = NULL;

  g_free (monitor);
}

const char *
menu_monitor_get_path (MenuMonitor *monitor)
{
  g_return_val_if_fail (monitor != NULL, NULL);

  return monitor->path;
}

static MenuMonitorNotify *
menu_monitor_notify_ref (MenuMonitorNotify *notify)
{
  g_return_val_if_fail (notify != NULL, NULL);
  g_return_val_if_fail (notify->refcount > 0, NULL);

  notify->refcount++;

  return notify;
}

static void
menu_monitor_notify_unref (MenuMonitorNotify *notify)
{
  g_return_if_fail (notify != NULL);
  g_return_if_fail (notify->refcount > 0);

  if (--notify->refcount > 0)
    return;

  g_free (notify);
}

void
menu_monitor_add_notify (MenuMonitor           *monitor,
			 MenuMonitorNotifyFunc  notify_func,
			 gpointer               user_data)
{
  MenuMonitorNotify *notify;
  GSList            *tmp;

  g_return_if_fail (monitor != NULL);
  g_return_if_fail (notify_func != NULL);

  tmp = monitor->notifies;
  while (tmp != NULL)
    {
      notify = tmp->data;

      if (notify->notify_func == notify_func &&
          notify->user_data   == user_data)
        break;

      tmp = tmp->next;
    }

  if (tmp == NULL)
    {
      notify              = g_new0 (MenuMonitorNotify, 1);
      notify->notify_func = notify_func;
      notify->user_data   = user_data;
      notify->refcount    = 1;

      monitor->notifies = g_slist_append (monitor->notifies, notify);
    }
}

void
menu_monitor_remove_notify (MenuMonitor           *monitor,
			    MenuMonitorNotifyFunc  notify_func,
			    gpointer               user_data)
{
  GSList *tmp;

  tmp = monitor->notifies;
  while (tmp != NULL)
    {
      MenuMonitorNotify *notify = tmp->data;
      GSList            *next   = tmp->next;

      if (notify->notify_func == notify_func &&
          notify->user_data   == user_data)
        {
	  notify->notify_func = NULL;
	  notify->user_data   = NULL;
          menu_monitor_notify_unref (notify);

          monitor->notifies = g_slist_delete_link (monitor->notifies, tmp);
        }

      tmp = next;
    }
}
