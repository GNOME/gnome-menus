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

#include <errno.h>
#include <unistd.h>

#include "inotify.h"
#include "inotify-syscalls.h"
#include "menu-util.h"

static int         inotify_fd = -1;
static guint       inotify_io_watch = 0;
static gboolean    initialized_inotify = FALSE;
static gboolean    failed_to_initialize = FALSE;
static GHashTable *inotify_monitors = NULL;

static void
queue_inotify_event (MenuMonitor          *monitor,
		     struct inotify_event *ievent)
{
  MenuMonitorEventInfo *event_info;
  MenuMonitorEvent      event;
  char                 *path;

  if (ievent->len > 0)
    {
      path = g_build_filename (menu_monitor_get_path (monitor),
			       ievent->name,
			       NULL);
    }
  else
    {
      path = g_strdup (menu_monitor_get_path (monitor));
    }

  event = MENU_MONITOR_EVENT_INVALID;
  if (ievent->mask & IN_CREATE)
    {
      event = MENU_MONITOR_EVENT_CREATED;
    }
  else if (ievent->mask & IN_DELETE)
    {
      event = MENU_MONITOR_EVENT_DELETED;
    }
  else if (ievent->mask & IN_MODIFY)
    {
      event = MENU_MONITOR_EVENT_CHANGED;
    }

  /* FIXME: 
   *   More logic required here to get the semantics similar
   *   to the FAM implementation.
   *
   *    - interpret MOVE_FROM as a delete
   *    - interpret MOVE_TO as a create
   *    - IN_IGNORED means the file/directory we're monitoring
   *      has been deleted and the watch removed. We need to
   *      add a watch on whichever ancestor still exists.
   */

  event_info = g_new0 (MenuMonitorEventInfo, 1);

  event_info->path    = g_strdup (path);
  event_info->event   = event;
  event_info->monitor = monitor;

  menu_monitor_queue_event (event_info);
}

static gboolean
inotify_data_pending (GIOChannel   *source,
		      GIOCondition  condition)
{
#define BUF_LEN (1024 * (sizeof (struct inotify_event) + 16))

  char buf[BUF_LEN];
  int  len;
  int  i;

  g_assert (condition == G_IO_IN || condition == G_IO_PRI);

  while ((len = read (inotify_fd, buf, BUF_LEN)) < 0 && errno == EINTR);
  if (len < 0)
    {
      g_warning ("Error reading inotify event: %s",
                 g_strerror (errno));
      goto error_cancel;
    }

  if (len == 0)
    {
      /*
       * FIXME: handle this better?
       */
      g_warning ("Error reading inotify event: supplied buffer was too small");
      goto error_cancel;
    }

  i = 0;
  while (i < len)
    {
      struct inotify_event *ievent = (struct inotify_event *) &buf [i];
      MenuMonitor          *monitor;

      menu_verbose ("Got event wd = %d, mask = 0x%x, cookie = %d, len = %d, name= %s\n",
		    ievent->wd,
		    ievent->mask,
		    ievent->cookie,
		    ievent->len,
		    ievent->len > 0 ? ievent->name : "<none>");

      if ((monitor = g_hash_table_lookup (inotify_monitors, GINT_TO_POINTER (ievent->wd))) != NULL)
	queue_inotify_event (monitor, ievent);

      i += sizeof (struct inotify_event) + ievent->len;
    }

  return TRUE; /* do come again */

 error_cancel:
  failed_to_initialize = TRUE;

  g_hash_table_destroy (inotify_monitors);
  inotify_monitors = NULL;

  close (inotify_fd);
  inotify_fd = -1;

  inotify_io_watch = 0;

  return FALSE;
}

static int
get_inotify_fd (void)
{
  if (!initialized_inotify)
    {
      if ((inotify_fd = inotify_init ()) < 0)
	{
	  g_warning ("Failed to initialize inotify: %s",
		     g_strerror (errno));
	  failed_to_initialize = TRUE;
	}
      else
	{
	  GIOChannel *io_channel;

          io_channel = g_io_channel_unix_new (inotify_fd);
          inotify_io_watch = g_io_add_watch (io_channel,
					     G_IO_IN|G_IO_PRI,
					     (GIOFunc) inotify_data_pending,
					     NULL);
          g_io_channel_unref (io_channel);

	  inotify_monitors = g_hash_table_new (g_direct_hash,
					       g_direct_equal);
	}

      initialized_inotify = TRUE;
    }

  return inotify_fd;
}

void
menu_monitor_backend_register_monitor (MenuMonitor *monitor)
{
  int fd;
  int wd;

  menu_monitor_set_backend_data (monitor, GINT_TO_POINTER (-1));

  if ((fd = get_inotify_fd ()) < 0)
    {
      menu_verbose ("Not adding %s monitor on '%s', failed to initialize inotify\n",
                    menu_monitor_get_is_directory (monitor) ? "directory" : "file",
                    menu_monitor_get_path (monitor));
      return;
    }

  wd = inotify_add_watch (fd,
			  menu_monitor_get_path (monitor),
			  IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE);
  if (wd < 0)
    {
      /*
       * FIXME: add monitor to an ancestor which does actually exist,
       *        or do the equivalent of inotify-missing.c by maintaining
       *        a list of monitors on non-existent files/directories
       *        which you retry in a timeout.
       */
      g_warning ("Failed to add monitor on '%s': %s",
		 menu_monitor_get_path (monitor),
		 g_strerror (errno));
      return;
    }

  menu_monitor_set_backend_data (monitor, GINT_TO_POINTER (wd));

  g_hash_table_insert (inotify_monitors,
		       GINT_TO_POINTER (wd),
		       monitor);
}

void
menu_monitor_backend_unregister_monitor (MenuMonitor *monitor)
{
  int fd;
  int wd;

  if ((fd = get_inotify_fd ()) < 0)
    return;

  wd = GPOINTER_TO_INT (menu_monitor_get_backend_data (monitor));
  if (wd < 0)
    return;

  inotify_rm_watch (fd, wd);
}
