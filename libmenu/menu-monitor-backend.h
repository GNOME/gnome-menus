/*
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2006 Mark McLoughlin
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

#ifndef __MENU_MONITOR_BACKEND_H__
#define __MENU_MONITOR_BACKEND_H__

#include "menu-monitor.h"

G_BEGIN_DECLS

typedef struct
{
  MenuMonitor      *monitor;
  MenuMonitorEvent  event;
  char             *path;
} MenuMonitorEventInfo;

gboolean menu_monitor_get_is_directory (MenuMonitor *monitor);

void     menu_monitor_set_backend_data (MenuMonitor *monitor,
					gpointer     backend_data);
gpointer menu_monitor_get_backend_data (MenuMonitor *monitor);

void menu_monitor_queue_event (MenuMonitorEventInfo *event_info);

void menu_monitor_backend_register_monitor   (MenuMonitor *monitor);
void menu_monitor_backend_unregister_monitor (MenuMonitor *monitor);

G_END_DECLS

#endif /* __MENU_MONITOR_BACKEND_H__ */
