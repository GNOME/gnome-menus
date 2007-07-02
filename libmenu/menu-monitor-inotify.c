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

/*
 * TODO:
 *  1) Read events from a timeout
 *  2) Handle symlinks correctly
 *  3) Race on file deletion - it may have been re-created
 *     by the time we add the watch on the ancestor causing
 *     us to miss the creation event
 */

#include <config.h>

#include "menu-monitor-backend.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/inotify.h>

#include "menu-util.h"
#include "canonicalize.h"

#define DEFAULT_BUFLEN (32 * (sizeof (struct inotify_event) + 16))
#define MAX_BUFLEN     (32 * DEFAULT_BUFLEN)

typedef struct
{
  int   fd;
  guint io_watch;

  GHashTable *wd_to_watch;
  GHashTable *path_to_watch;

  guint   buflen;
  guchar *buffer;
} MenuInotifyData;

typedef struct
{
  int     wd;
  char   *path;
  GSList *monitors;
  GSList *creation_monitors;
} MenuInotifyWatch;

static gboolean        initialized_inotify = FALSE;
static gboolean        failed_to_initialize = FALSE;
static MenuInotifyData inotify_data = { -1, 0, NULL, NULL, 0, NULL };

static void close_inotify (void);

static MenuInotifyWatch *add_watch    (MenuInotifyData  *idata,
                                       MenuMonitor      *monitor);
static void              remove_watch (MenuInotifyData  *idata,
                                       MenuInotifyWatch *watch,
                                       MenuMonitor      *monitor);

static void
queue_monitor_event (MenuMonitor      *monitor,
                     MenuMonitorEvent  event,
                     const char       *path)
{
  MenuMonitorEventInfo *event_info;

  event_info = g_new0 (MenuMonitorEventInfo, 1);

  event_info->path    = g_strdup (path);
  event_info->event   = event;
  event_info->monitor = monitor;

  menu_monitor_queue_event (event_info);
}

static void
handle_inotify_event (MenuInotifyData      *idata,
                      MenuInotifyWatch     *watch,
                      struct inotify_event *ievent)
{
  MenuMonitorEvent  event;
  const char       *path;
  char             *freeme;

  freeme = NULL;

  if (ievent->len > 0)
    path = freeme = g_build_filename (watch->path, ievent->name, NULL);
  else
    path = watch->path;

  event = MENU_MONITOR_EVENT_INVALID;

  if (ievent->mask & (IN_CREATE|IN_MOVED_TO))
    {
      event = MENU_MONITOR_EVENT_CREATED;
    }
  else if (ievent->mask & (IN_DELETE|IN_DELETE_SELF|IN_MOVED_FROM|IN_MOVE_SELF))
    {
      event = MENU_MONITOR_EVENT_DELETED;
    }
  else if (ievent->mask & (IN_MODIFY|IN_ATTRIB))
    {
      event = MENU_MONITOR_EVENT_CHANGED;
    }

  if (event != MENU_MONITOR_EVENT_INVALID)
    {
      GSList *tmp;

      tmp = watch->monitors;
      while (tmp != NULL)
        {
          MenuMonitor *monitor = tmp->data;

          queue_monitor_event (monitor, event, path);

          tmp = tmp->next;
        }
    }

  if (event == MENU_MONITOR_EVENT_CREATED)
    {
      GSList *tmp;
      GSList *monitors_to_remove;
      int     pathlen;

      pathlen = strlen (path);
      monitors_to_remove = NULL;

      tmp = watch->creation_monitors;
      while (tmp != NULL)
        {
          MenuMonitor *monitor = tmp->data;
          GSList      *next    = tmp->next;
          const char  *monitor_path;

          monitor_path = menu_monitor_get_path (monitor);

          if (!strncmp (monitor_path, path, pathlen) &&
              (monitor_path[pathlen] == '\0' || monitor_path[pathlen] == '/'))
            {
              MenuInotifyWatch *new_watch;

              new_watch = add_watch (idata, monitor);

              if (!new_watch)
                {
                  g_warning ("Failed to add monitor on '%s': %s",
                             menu_monitor_get_path (monitor),
                             g_strerror (errno));

                  watch->creation_monitors =
                    g_slist_delete_link (watch->creation_monitors, tmp);
                }
              else if (new_watch != watch)
                {
                  monitors_to_remove =
                    g_slist_prepend (monitors_to_remove, monitor);

                  if (monitor_path[pathlen] == '\0')
                    queue_monitor_event (monitor, event, path);
                }
            }

          tmp = next;
        }

      tmp = monitors_to_remove;
      while (tmp != NULL)
        {
          MenuMonitor *monitor = tmp->data;

          remove_watch (idata, watch, monitor);

          tmp = tmp->next;
        }

      g_slist_free (monitors_to_remove);
    }

  g_free (freeme);

  if (ievent->mask & IN_MOVE_SELF)
    {
      /* 
       * Watch is on a different file/directory now so we
       * delete it and attempt to re-add it later.
       */
      inotify_rm_watch (idata->fd, watch->wd);
    }

  if (ievent->mask & IN_IGNORED)
    {
      GSList *tmp;
      GSList *monitors_to_add;

      monitors_to_add = watch->monitors;
      monitors_to_add = g_slist_concat (monitors_to_add,
                                        watch->creation_monitors);

      watch->monitors = NULL;
      watch->creation_monitors = NULL;

      remove_watch (idata, watch, NULL);

      tmp = monitors_to_add;
      while (tmp != NULL)
        {
          MenuMonitor      *monitor = tmp->data;
          MenuInotifyWatch *new_watch;

          new_watch = add_watch (idata, monitor);
          if (!new_watch)
            g_warning ("Failed to add monitor on '%s': %s",
                       menu_monitor_get_path (monitor),
                       g_strerror (errno));

          tmp = tmp->next;
        }

      g_slist_free (monitors_to_add);
    }
}

static gboolean
inotify_data_pending (GIOChannel   *source,
		      GIOCondition  condition)
{
  int len;
  int i;

  g_assert (condition == G_IO_IN || condition == G_IO_PRI);

  do
    {
      while ((len = read (inotify_data.fd, inotify_data.buffer, inotify_data.buflen)) < 0 && errno == EINTR);
      if (len > 0)
        break;
      else if (len < 0)
        {
          g_warning ("Error reading inotify event: %s",
                     g_strerror (errno));
          goto error_cancel;
        }

      g_assert (len == 0);

      if ((inotify_data.buflen << 1) > MAX_BUFLEN)
        {
          g_warning ("Error reading inotify event: Exceded maximum buffer size");
          goto error_cancel;
        }
        
      menu_verbose ("Buffer size %u too small, trying again at %u\n",
                    inotify_data.buflen, inotify_data.buflen << 1);

      inotify_data.buflen <<= 1;
      inotify_data.buffer = g_realloc (inotify_data.buffer, inotify_data.buflen);
    }
  while (TRUE);

  i = 0;
  while (i < len)
    {
      struct inotify_event *ievent = (struct inotify_event *) &inotify_data.buffer [i];
      MenuInotifyWatch     *watch;

      menu_verbose ("Got event wd = %d, mask = 0x%x, cookie = %d, len = %d, name= %s\n",
		    ievent->wd,
		    ievent->mask,
		    ievent->cookie,
		    ievent->len,
		    ievent->len > 0 ? ievent->name : "<none>");

      if ((watch = g_hash_table_lookup (inotify_data.wd_to_watch,
                                        GINT_TO_POINTER (ievent->wd))) != NULL)
	handle_inotify_event (&inotify_data, watch, ievent);

      i += sizeof (struct inotify_event) + ievent->len;
    }

  return TRUE; /* do come again */

 error_cancel:
  inotify_data.io_watch = 0;

  close_inotify ();

  return FALSE;
}

static void
remove_watch (MenuInotifyData  *idata,
              MenuInotifyWatch *watch,
              MenuMonitor      *monitor)
{
  watch->monitors = g_slist_remove_all (watch->monitors, monitor);

  watch->creation_monitors =
    g_slist_remove_all (watch->creation_monitors, monitor);

  if (watch->monitors || watch->creation_monitors)
    return;

  g_hash_table_remove (idata->wd_to_watch, GINT_TO_POINTER (watch->wd));

  g_hash_table_remove (idata->path_to_watch, watch->path);

  g_free (watch->path);
  watch->path = NULL;

  inotify_rm_watch (idata->fd, watch->wd);
  watch->wd = -1;

  g_free (watch);
}

static MenuInotifyWatch *
add_canonical_watch (MenuInotifyData *idata,
                     const char      *canonical_path,
                     MenuMonitor     *monitor,
                     gboolean         creation_monitor)
{
#define WATCH_EVENTS (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB)

  MenuInotifyWatch *watch;

  if (!(watch = g_hash_table_lookup (idata->path_to_watch, canonical_path)))
    {
      int wd;

      wd = inotify_add_watch (idata->fd, canonical_path, WATCH_EVENTS);
      if (wd < 0)
        {
          char *dirname;

          if (errno != ENOENT)
            return NULL;

          dirname = g_path_get_dirname (canonical_path);

          menu_verbose ("Failed to add watch on '%s' - "
                        "adding watch to ancestor '%s' instead\n",
                        canonical_path, dirname);

          watch = add_canonical_watch (idata, dirname, monitor, TRUE);

          g_free (dirname);

          return watch;
        }

      watch = g_new0 (MenuInotifyWatch, 1);

      watch->wd   = wd;
      watch->path = g_strdup (canonical_path);

      g_hash_table_insert (idata->wd_to_watch, GINT_TO_POINTER (wd), watch);
      g_hash_table_insert (idata->path_to_watch, watch->path, watch);
    }

  if (!creation_monitor)
    watch->monitors = g_slist_prepend (watch->monitors, monitor);
  else
    watch->creation_monitors = g_slist_prepend (watch->creation_monitors,
                                                monitor);

  menu_monitor_set_backend_data (monitor, watch);

  return watch;

#undef WATCH_EVENTS
}

static MenuInotifyWatch *
add_watch (MenuInotifyData *idata,
           MenuMonitor     *monitor)
{
  MenuInotifyWatch *watch;
  const char       *canonical_path;
  char             *freeme;

  canonical_path = freeme =
    menu_canonicalize_file_name (menu_monitor_get_path (monitor), FALSE);

  if (!canonical_path)
    canonical_path = menu_monitor_get_path (monitor);

  watch = add_canonical_watch (idata, canonical_path, monitor, FALSE);

  g_free (freeme);

  return watch;
}

static gboolean
remove_watch_foreach (const char       *path,
                      MenuInotifyWatch *watch,
                      MenuInotifyData  *idata)
{
  g_slist_free (watch->monitors);
  watch->monitors = NULL;

  g_slist_free (watch->creation_monitors);
  watch->creation_monitors = NULL;

  g_free (watch->path);
  watch->path = NULL;

  inotify_rm_watch (idata->fd, watch->wd);
  watch->wd = -1;

  g_free (watch);

  return TRUE;
}

static void
close_inotify (void)
{
  failed_to_initialize = FALSE;

  if (!initialized_inotify)
    return;

  initialized_inotify = FALSE;

  g_hash_table_foreach_remove (inotify_data.path_to_watch,
                               (GHRFunc) remove_watch_foreach,
                               &inotify_data);
  if (inotify_data.path_to_watch)
    g_hash_table_destroy (inotify_data.path_to_watch);
  inotify_data.path_to_watch = NULL;

  if (inotify_data.wd_to_watch)
    g_hash_table_destroy (inotify_data.wd_to_watch);
  inotify_data.wd_to_watch = NULL;

  g_free (inotify_data.buffer);
  inotify_data.buffer = NULL;
  inotify_data.buflen = 0;

  if (inotify_data.io_watch)
    g_source_remove (inotify_data.io_watch);
  inotify_data.io_watch = 0;

  if (inotify_data.fd > 0)
    close (inotify_data.fd);
  inotify_data.fd = 0;
}

static MenuInotifyData *
get_inotify (void)
{
  GIOChannel *io_channel;
  int         fd;

  if (failed_to_initialize)
    return NULL;

  if (initialized_inotify)
    return &inotify_data;

  if ((fd = inotify_init ()) < 0)
    {
      g_warning ("Failed to initialize inotify: %s",
		     g_strerror (errno));
      failed_to_initialize = TRUE;
      return NULL;
    }

  inotify_data.fd = fd;

  io_channel = g_io_channel_unix_new (fd);
  inotify_data.io_watch = g_io_add_watch (io_channel,
                                          G_IO_IN|G_IO_PRI,
                                          (GIOFunc) inotify_data_pending,
                                          NULL);
  g_io_channel_unref (io_channel);

  inotify_data.buflen = DEFAULT_BUFLEN;
  inotify_data.buffer = g_malloc (DEFAULT_BUFLEN);

  inotify_data.wd_to_watch = g_hash_table_new (g_direct_hash,
					       g_direct_equal);

  inotify_data.path_to_watch = g_hash_table_new (g_str_hash,
                                                 g_str_equal);
  
  initialized_inotify = TRUE;

  return &inotify_data;
}

void
menu_monitor_backend_register_monitor (MenuMonitor *monitor)
{
  MenuInotifyData  *idata;
  MenuInotifyWatch *watch;

  if (!(idata = get_inotify ()))
    {
      menu_verbose ("Not adding %s monitor on '%s', failed to initialize inotify\n",
                    menu_monitor_get_is_directory (monitor) ? "directory" : "file",
                    menu_monitor_get_path (monitor));
      return;
    }

  watch = add_watch (idata, monitor);
  if (!watch)
    g_warning ("Failed to add monitor on '%s': %s",
               menu_monitor_get_path (monitor),
               g_strerror (errno));
}

void
menu_monitor_backend_unregister_monitor (MenuMonitor *monitor)
{
  MenuInotifyData  *idata;
  MenuInotifyWatch *watch;

  if (!(idata = get_inotify ()))
    return;

  if (!(watch = menu_monitor_get_backend_data (monitor)))
    return;

  remove_watch (idata, watch, monitor);
}
