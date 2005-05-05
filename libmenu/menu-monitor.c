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

#ifdef HAVE_FAM
#include <string.h>
#include <fam.h>
#endif

#include "menu-util.h"

struct MenuMonitor
{
  char  *path;
  guint  refcount;

  GSList *notifies;

#ifdef HAVE_FAM
  FAMRequest  request;
  GSList     *pending_events;
#endif /* HAVE_FAM */

  guint is_directory : 1;
};

typedef struct
{
  MenuMonitorNotifyFunc notify_func;
  gpointer              user_data;
} MenuMonitorNotify;

#ifdef HAVE_FAM
typedef struct
{
  MenuMonitor      *monitor;
  MenuMonitorEvent  event;
  char             *path;
} MenuMonitorEventInfo;
#endif /* HAVE_FAM */

static GHashTable *monitors_registry = NULL;

#ifdef HAVE_FAM
static FAMConnection  fam_connection;
static gboolean       opened_connection = FALSE;
static gboolean       failed_to_connect = FALSE;
static guint          fam_io_watch = 0;
static guint          events_idle_handler = 0;

static void
invoke_notifies (MenuMonitor      *monitor,
		 MenuMonitorEvent  event,
		 const char       *path)
{
  GSList *tmp;

  tmp = monitor->notifies;
  while (tmp != NULL)
    {
      MenuMonitorNotify *notify = tmp->data;
      GSList            *next    = tmp->next;

      notify->notify_func (monitor, event, path, notify->user_data);

      tmp = next;
    }
}

static void
list_pending_events_foreach (const char   *key,
			     MenuMonitor  *monitor,
			     GSList      **pending_events)
{
  GSList *tmp;

  tmp = monitor->pending_events;
  while (tmp != NULL)
    {
      MenuMonitorEventInfo *event = tmp->data;

      menu_monitor_ref (event->monitor);

      tmp = tmp->next;
    }

  *pending_events = g_slist_concat (*pending_events,
				    monitor->pending_events);

  monitor->pending_events = NULL;
}

static gboolean
emit_events_in_idle (void)
{
  GSList *pending_events = NULL;
  GSList *tmp;

  events_idle_handler = 0;
  
  g_hash_table_foreach (monitors_registry,
			(GHFunc) list_pending_events_foreach,
			&pending_events);

  tmp = pending_events;
  while (tmp != NULL)
    {
      MenuMonitorEventInfo *event = tmp->data;

      invoke_notifies (event->monitor, event->event, event->path);

      menu_monitor_unref (event->monitor);
      event->monitor = NULL;

      g_free (event->path);
      event->path = NULL;

      event->event = MENU_MONITOR_EVENT_INVALID;

      g_free (event);

      tmp = tmp->next;
    }

  g_slist_free (pending_events);

  return FALSE;
}

static void
queue_fam_event (MenuMonitor *monitor,
		 FAMEvent    *fam_event)
{
  MenuMonitorEventInfo *event;
  const char           *path;
  char                 *freeme;
  GSList               *tmp;

  freeme = NULL;
  if (fam_event->filename[0] == '/')
    {
      path = fam_event->filename;
    }
  else
    {
      path = freeme = g_build_filename (monitor->path,
					fam_event->filename);
    }

  event = NULL;
  tmp = monitor->pending_events;
  while (tmp != NULL)
    {
      event = tmp->data;

      if (strcmp (event->path, path) == 0)
	break;

      event = NULL;

      tmp = tmp->next;
    }

  if (event == NULL)
    {
      event = g_new0 (MenuMonitorEventInfo, 1);

      event->path    = g_strdup (path);
      event->monitor = monitor;

      monitor->pending_events = g_slist_append (monitor->pending_events,
						event);
    }

  switch (fam_event->code)
    {
    case FAMChanged:
      event->event = MENU_MONITOR_EVENT_CHANGED;
      break;

    case FAMCreated:
      event->event = MENU_MONITOR_EVENT_CREATED;
      break;

    case FAMDeleted:
      event->event = MENU_MONITOR_EVENT_DELETED;
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  if (events_idle_handler == 0)
    {
      events_idle_handler = g_idle_add ((GSourceFunc) emit_events_in_idle, NULL);
    }

  g_free (freeme);
}

static inline void
debug_event (FAMEvent *event)
{
#define PRINT_EVENT(str) menu_verbose ("Got event: %d %s <" str ">\n", event->code, event->filename);

  switch (event->code)
    {
    case FAMChanged:
      PRINT_EVENT ("changed");
      break;
    case FAMDeleted:
      PRINT_EVENT ("deleted");
      break;
    case FAMStartExecuting:
      PRINT_EVENT ("start-executing");
      break;
    case FAMStopExecuting:
      PRINT_EVENT ("stop-executing");
      break;
    case FAMCreated:
      PRINT_EVENT ("created");
      break;
    case FAMAcknowledge:
      PRINT_EVENT ("acknowledge");
      break;
    case FAMExists:
      PRINT_EVENT ("exists");
      break;
    case FAMEndExist:
      PRINT_EVENT ("end-exist");
      break;
    case FAMMoved:
      PRINT_EVENT ("moved");
      break;
    default:
      PRINT_EVENT ("invalid");
      break;
    }

#undef PRINT_EVENT
}

static gboolean
process_fam_events (void)
{
  if (failed_to_connect)
    return FALSE;

  while (FAMPending (&fam_connection))
    {
      FAMEvent event;

      if (FAMNextEvent (&fam_connection, &event) != 1)
        {
	  g_warning ("Failed to read next event from FAM: %s",
		     FamErrlist[FAMErrno]);
	  failed_to_connect = TRUE;
          FAMClose (&fam_connection);
          return FALSE;
        }

      debug_event (&event);

      if (event.code != FAMChanged &&
	  event.code != FAMCreated &&
	  event.code != FAMDeleted)
	continue;

      queue_fam_event (event.userdata, &event);
    }

  return TRUE;
}

static gboolean
fam_data_pending (GIOChannel   *source,
		  GIOCondition  condition)
{
  g_assert (condition == G_IO_IN || condition == G_IO_PRI);

  if (!process_fam_events ())
    {
      fam_io_watch = 0;
      return FALSE;
    }

  return TRUE; /* do come again */
}

static FAMConnection *
get_fam_connection (void)
{
  if (!opened_connection)
    {
      if (FAMOpen (&fam_connection) == 0)
	{
	  GIOChannel *io_channel;

#ifdef HAVE_FAMNOEXISTS
	  FAMNoExists (&fam_connection);
#endif /* HAVE_FAMNOEXISTS */

	  io_channel = g_io_channel_unix_new (FAMCONNECTION_GETFD (&fam_connection));
	  fam_io_watch = g_io_add_watch (io_channel,
					 G_IO_IN|G_IO_PRI,
					 (GIOFunc) fam_data_pending,
					 NULL);
	  g_io_channel_unref (io_channel);
	}
      else
	{
	  g_warning ("Failed to connect to the FAM server: %s",
		     FamErrlist[FAMErrno]);
	  failed_to_connect = TRUE;
	}

      opened_connection = TRUE;
    }

  return failed_to_connect ? NULL : &fam_connection;
}
#endif /* HAVE_FAM */

static void
register_monitor_with_fam (MenuMonitor *monitor)
{
#ifdef HAVE_FAM
  FAMConnection *fam_connection;

  if ((fam_connection = get_fam_connection ()) == NULL)
    {
      menu_verbose ("Not adding %s monitor on '%s', failed to connect to FAM server\n",
		    monitor->is_directory ? "directory" : "file",
		    monitor->path);
      return;
    }

  /* Need to process any pending events, otherwise we may block
   * on write - i.e. the FAM sever is blocked because its write
   * buffer is full notifying us of events, we need to read those
   * events before it can process our new request.
   */
  if (!process_fam_events ())
    {
      g_source_remove (fam_io_watch);
      fam_io_watch = 0;
      return;
    }

  if (monitor->is_directory)
    {
      if (FAMMonitorDirectory (fam_connection,
			       monitor->path,
			       &monitor->request,
			       monitor) != 0)
	{
	  g_warning ("Failed to add directory monitor on '%s': %s",
		     monitor->path, FamErrlist[FAMErrno]);
	}
    }
  else
    {
      if (FAMMonitorFile (fam_connection,
			  monitor->path,
			  &monitor->request,
			  monitor) != 0)
	{
	  g_warning ("Failed to add file monitor on '%s': %s",
		     monitor->path, FamErrlist[FAMErrno]);
	}
    }
#endif /* HAVE_FAM */
}

static void
unregister_monitor_with_fam (MenuMonitor *monitor)
{
#ifdef HAVE_FAM
  if (failed_to_connect)
    return;

  FAMCancelMonitor (&fam_connection, &monitor->request);
#endif /* HAVE_FAM */
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

      register_monitor_with_fam (retval);

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

#ifdef HAVE_FAM
static void
free_event_info (MenuMonitorEventInfo *event)
{
  g_free (event->path);
  event->path = NULL;

  event->monitor = NULL;
  event->event   = MENU_MONITOR_EVENT_INVALID;
}
#endif /* HAVE_FAM */

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

  unregister_monitor_with_fam (monitor);

  g_slist_foreach (monitor->notifies, (GFunc) g_free, NULL);
  g_slist_free (monitor->notifies);
  monitor->notifies = NULL;

#ifdef HAVE_FAM
  g_slist_foreach (monitor->pending_events, (GFunc) free_event_info, NULL);
  g_slist_free (monitor->pending_events);
  monitor->pending_events = NULL;
#endif /* HAVE_FAM */

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
          monitor->notifies = g_slist_delete_link (monitor->notifies, tmp);
          g_free (notify);
        }

      tmp = next;
    }
}
