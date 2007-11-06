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

#include <config.h>

#include "menu-monitor-backend.h"

#include <string.h>
#include <fam.h>

#include "menu-util.h"

static FAMConnection  fam_connection;
static gboolean       opened_connection = FALSE;
static gboolean       failed_to_connect = FALSE;
static guint          fam_io_watch = 0;

static void
queue_fam_event (MenuMonitor *monitor,
		 FAMEvent    *fam_event)
{
  MenuMonitorEventInfo *event_info;
  MenuMonitorEvent      event;
  const char           *path;
  char                 *freeme;

  freeme = NULL;
  if (fam_event->filename[0] == G_DIR_SEPARATOR)
    {
      path = fam_event->filename;
    }
  else
    {
      path = freeme = g_build_filename (menu_monitor_get_path (monitor),
					fam_event->filename,
					NULL);
    }

  event = MENU_MONITOR_EVENT_INVALID;
  switch (fam_event->code)
    {
    case FAMChanged:
      event = MENU_MONITOR_EVENT_CHANGED;
      break;

    case FAMCreated:
      event = MENU_MONITOR_EVENT_CREATED;
      break;

    case FAMDeleted:
      event = MENU_MONITOR_EVENT_DELETED;
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  event_info = g_new0 (MenuMonitorEventInfo, 1);

  event_info->path    = g_strdup (path);
  event_info->event   = event;
  event_info->monitor = monitor;

  menu_monitor_queue_event (event_info);

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

void
menu_monitor_backend_register_monitor (MenuMonitor *monitor)
{
  FAMConnection *fam_connection;
  FAMRequest    *request;

  if ((fam_connection = get_fam_connection ()) == NULL)
    {
      menu_verbose ("Not adding %s monitor on '%s', failed to connect to FAM server\n",
		    menu_monitor_get_is_directory (monitor) ? "directory" : "file",
		    menu_monitor_get_path (monitor));
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

  request = g_new0 (FAMRequest, 1);

  if (menu_monitor_get_is_directory (monitor))
    {
      if (FAMMonitorDirectory (fam_connection,
			       menu_monitor_get_path (monitor),
			       request,
			       monitor) != 0)
	{
	  g_warning ("Failed to add directory monitor on '%s': %s",
		     menu_monitor_get_path (monitor),
		     FamErrlist[FAMErrno]);
	  g_free (request);
	  request = NULL;
	}
    }
  else
    {
      if (FAMMonitorFile (fam_connection,
			  menu_monitor_get_path (monitor),
			  request,
			  monitor) != 0)
	{
	  g_warning ("Failed to add file monitor on '%s': %s",
		     menu_monitor_get_path (monitor),
		     FamErrlist[FAMErrno]);
	  g_free (request);
	  request = NULL;
	}
    }

  menu_monitor_set_backend_data (monitor, request);
}

void
menu_monitor_backend_unregister_monitor (MenuMonitor *monitor)
{
  FAMRequest *request;

  if (failed_to_connect)
    return;

  if ((request = menu_monitor_get_backend_data (monitor)) != NULL)
    {
      FAMCancelMonitor (&fam_connection, request);
      g_free (request);
      request = NULL;
    }

  /* Need to process any remaining events for this monitor
   */
  if (!process_fam_events ())
    {
      g_source_remove (fam_io_watch);
      fam_io_watch = 0;
    }
}
