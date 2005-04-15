/*
 * Copyright (C) 2002 - 2004 Red Hat, Inc.
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

#include "entry-directories.h"

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <libgnomevfs/gnome-vfs.h>

#include "menu-util.h"
#include "canonicalize.h"

typedef struct CachedDir CachedDir;
typedef struct CachedDirMonitor CachedDirMonitor;

struct EntryDirectory
{
  CachedDir *dir;
  char      *legacy_prefix;

  guint entry_type : 2;
  guint is_legacy : 1;
  guint refcount : 24;
};

struct EntryDirectoryList
{
  int    refcount;
  int    length;
  GList *dirs;
};

struct CachedDir
{
  CachedDir *parent;
  char      *name;

  GSList *entries;
  GSList *subdirs;

  GnomeVFSMonitorHandle *monitor;
  GSList                *monitors;

  guint have_read_entries : 1;
  guint no_monitor_support : 1;
};

struct CachedDirMonitor
{
  EntryDirectory            *ed;
  EntryDirectoryChangedFunc  callback;
  gpointer                   user_data;
};

static void     cached_dir_free                   (CachedDir  *dir);
static gboolean cached_dir_load_entries_recursive (CachedDir  *dir,
                                                   const char *dirname);

/*
 * Entry directory cache
 */

static CachedDir *dir_cache = NULL;

static CachedDir *
cached_dir_new (const char *name)
{
  CachedDir *dir;

  dir = g_new0 (CachedDir, 1);

  dir->name = g_strdup (name);

  return dir;
}

static void
cached_dir_clear_entries (CachedDir *dir)
{
  g_slist_foreach (dir->entries,
                   (GFunc) desktop_entry_unref,
                   NULL);
  g_slist_free (dir->entries);
  dir->entries = NULL;
}

static void
cached_dir_clear_subdirs (CachedDir *dir)
{
  g_slist_foreach (dir->subdirs,
                   (GFunc) cached_dir_free,
                   NULL);
  g_slist_free (dir->subdirs);
  dir->subdirs = NULL;
}

static void
cached_dir_free (CachedDir *dir)
{
  cached_dir_clear_entries (dir);
  cached_dir_clear_subdirs (dir);

  if (dir->monitor)
    gnome_vfs_monitor_cancel (dir->monitor);
  dir->monitor = NULL;

  g_slist_foreach (dir->monitors, (GFunc) g_free, NULL);
  g_slist_free (dir->monitors);
  dir->monitors = NULL;

  g_free (dir->name);
  g_free (dir);
}

static inline CachedDir *
find_subdir (CachedDir  *dir,
             const char *subdir)
{
  GSList *tmp;

  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      CachedDir *sub = tmp->data;

      if (strcmp (sub->name, subdir) == 0)
        return sub;

      tmp = tmp->next;
    }

  return NULL;
}

static DesktopEntry *
find_entry (CachedDir  *dir,
            const char *basename)
{
  GSList *tmp;

  tmp = dir->entries;
  while (tmp != NULL)
    {
      if (strcmp (desktop_entry_get_basename (tmp->data), basename) == 0)
        return tmp->data;

      tmp = tmp->next;
    }

  return NULL;
}

static DesktopEntry *
cached_dir_find_relative_path (CachedDir  *dir,
                               const char *relative_path)
{
  DesktopEntry  *retval = NULL;
  char         **split;
  int            i;

  split = g_strsplit (relative_path, "/", -1);

  i = 0;
  while (split[i] != NULL)
    {
      if (*(split[i]) == '\0')
        continue;

      if (split[i + 1] != NULL)
        {
          if ((dir = find_subdir (dir, split[i])) == NULL)
            break;
        }
      else
        {
          retval = find_entry (dir, split[i]);
          break;
        }

      ++i;
    }

  g_strfreev (split);

  return retval;
}

static DesktopEntry *
cached_dir_find_file_id (CachedDir  *dir,
                         const char *file_id,
                         gboolean    legacy)
{
  DesktopEntry *retval = NULL;

  if (!legacy)
    {
      char *p;
      char *freeme;

      p = freeme = g_strdup (file_id);
      while (p != NULL)
        {
          char *q;

          if ((retval = find_entry (dir, p)))
            break;

          q = strchr (p, '-');
          if (q)
            {
              CachedDir *subdir;

              *(q++) = '\0';

              if ((subdir = find_subdir (dir, p)))
                {
                  retval = cached_dir_find_file_id (subdir, q, legacy);
                  if (retval)
                    break;
                }
            }

          p = q;
        }

      g_free (freeme);
    }
  else /* legacy */
    {
      retval = find_entry (dir, file_id);

      /* Ignore entries with categories in legacy trees
       */
      if (retval && desktop_entry_has_categories (retval))
        retval = NULL;

      if (!retval)
        {
          GSList *tmp;

          tmp = dir->subdirs;
          while (tmp != NULL)
            {
              retval = cached_dir_find_file_id (tmp->data, file_id, legacy);
              if (retval)
                break;

              tmp = tmp->next;
            }
        }
    }

  return retval;
}

static CachedDir *
cached_dir_lookup (const char *canonical)
{
  CachedDir  *dir;
  char      **split;
  int         i;

  if (dir_cache == NULL)
    dir_cache = cached_dir_new ("/");
  dir = dir_cache;

  g_assert (canonical != NULL && canonical[0] == '/');

  menu_verbose ("Looking up cached dir \"%s\"\n", canonical);

  split = g_strsplit (canonical + 1, "/", -1);

  i = 0;
  while (split[i] != NULL)
    {
      CachedDir *subdir;

      if (*(split[i]) == '\0')
        continue;

      if ((subdir = find_subdir (dir, split[i])) == NULL)
        {
          subdir = cached_dir_new (split[i]);
          dir->subdirs = g_slist_prepend (dir->subdirs, subdir);
          subdir->parent = dir;
        }

      dir = subdir;

      ++i;
    }

  g_strfreev (split);

  g_assert (dir != NULL);

  return dir;
}

static gboolean
cached_dir_add_entry (CachedDir  *dir,
                      const char *basename,
                      const char *path)
{
  DesktopEntry *entry;

  entry = desktop_entry_new (path);
  if (entry == NULL)
    return FALSE;

  dir->entries = g_slist_prepend (dir->entries, entry);

  return TRUE;
}

static gboolean
cached_dir_update_entry (CachedDir  *dir,
                         const char *basename,
                         const char *path)
{
  GSList *tmp;

  tmp = dir->entries;
  while (tmp != NULL)
    {
      if (strcmp (desktop_entry_get_basename (tmp->data), basename) == 0)
        {
          if (!desktop_entry_reload (tmp->data))
	    {
	      dir->entries = g_slist_delete_link (dir->entries, tmp);
	    }

          return TRUE;
        }

      tmp = tmp->next;
    }

  return cached_dir_add_entry (dir, basename, path);
}

static gboolean
cached_dir_remove_entry (CachedDir  *dir,
                         const char *basename)
{
  GSList *tmp;

  tmp = dir->entries;
  while (tmp != NULL)
    {
      if (strcmp (desktop_entry_get_basename (tmp->data), basename) == 0)
        {
          desktop_entry_unref (tmp->data);
          dir->entries = g_slist_delete_link (dir->entries, tmp);
          return TRUE;
        }

      tmp = tmp->next;
    }

  return FALSE;
}

static gboolean
cached_dir_add_subdir (CachedDir  *dir,
                       const char *basename,
                       const char *path)
{
  CachedDir *subdir;

  subdir = cached_dir_new (basename);

  if (!cached_dir_load_entries_recursive (subdir, path))
    {
      cached_dir_free (subdir);
      return FALSE;
    }

  menu_verbose ("Caching dir \"%s\"\n", basename);

  subdir->parent = dir;
  dir->subdirs = g_slist_prepend (dir->subdirs, subdir);

  return TRUE;
}

static gboolean
cached_dir_remove_subdir (CachedDir  *dir,
                          const char *basename)
{
  GSList *tmp;

  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      CachedDir *subdir = tmp->data;

      if (strcmp (subdir->name, basename) == 0)
        {
	  cached_dir_free (tmp->data);
          dir->subdirs = g_slist_delete_link (dir->subdirs, tmp);
          return TRUE;
        }

      tmp = tmp->next;
    }

  return FALSE;
}

static void
cached_dir_invoke_monitors (CachedDir *dir)
{
  GSList *tmp;

  tmp = dir->monitors;
  while (tmp != NULL)
    {
      CachedDirMonitor *monitor = tmp->data;
      GSList           *next    = tmp->next;

      monitor->callback (monitor->ed, monitor->user_data);

      tmp = next;
    }

  if (dir->parent)
    {
      cached_dir_invoke_monitors (dir->parent);
    }
}

static void
handle_cached_dir_changed (GnomeVFSMonitorHandle    *handle,
                           const char               *monitor_uri,
                           const char               *info_uri,
                           GnomeVFSMonitorEventType  event,
                           CachedDir                *dir)
{
  gboolean  handled = FALSE;
  char     *path;
  char     *basename;

  if (event != GNOME_VFS_MONITOR_EVENT_CREATED &&
      event != GNOME_VFS_MONITOR_EVENT_DELETED &&
      event != GNOME_VFS_MONITOR_EVENT_CHANGED)
    return;

  if (!(path = gnome_vfs_get_local_path_from_uri (info_uri)))
    return;

  menu_verbose ("Notified of '%s' %s - invalidating cache\n",
                path,
                event == GNOME_VFS_MONITOR_EVENT_CREATED ? ("created") :
                event == GNOME_VFS_MONITOR_EVENT_DELETED ? ("deleted") :
                event == GNOME_VFS_MONITOR_EVENT_CHANGED ? ("changed") : ("unknown-event"));

  basename = g_path_get_basename (path);

  if (g_str_has_suffix (basename, ".desktop") ||
      g_str_has_suffix (basename, ".directory"))
    {
      switch (event)
        {
        case GNOME_VFS_MONITOR_EVENT_CREATED:
          handled = cached_dir_add_entry (dir, basename, path);
          break;

        case GNOME_VFS_MONITOR_EVENT_CHANGED:
          handled = cached_dir_update_entry (dir, basename, path);
          break;

        case GNOME_VFS_MONITOR_EVENT_DELETED:
          handled = cached_dir_remove_entry (dir, basename);
          break;

        default:
          g_assert_not_reached ();
          break;
        }
    }
  else /* Try recursing */
    {
      switch (event)
        {
        case GNOME_VFS_MONITOR_EVENT_CREATED:
          handled = cached_dir_add_subdir (dir, basename, path);
          break;

        case GNOME_VFS_MONITOR_EVENT_CHANGED:
          break;

        case GNOME_VFS_MONITOR_EVENT_DELETED:
          handled = cached_dir_remove_subdir (dir, basename);
          break;

        default:
          g_assert_not_reached ();
          break;
        }
    }

  g_free (basename);
  g_free (path);

  if (handled)
    {
      cached_dir_invoke_monitors (dir);
    }
}

static void
cached_dir_ensure_monitor (CachedDir  *dir,
                           const char *dirname)
{
  char *uri;

  if (dir->monitor != NULL || dir->no_monitor_support)
    return;

  uri = gnome_vfs_get_uri_from_local_path (dirname);

  if (gnome_vfs_monitor_add (&dir->monitor,
                             uri,
                             GNOME_VFS_MONITOR_DIRECTORY,
                             (GnomeVFSMonitorCallback) handle_cached_dir_changed,
                             dir) != GNOME_VFS_OK)
    {
      dir->no_monitor_support = TRUE;
    }

  g_free (uri);
}

static gboolean
cached_dir_load_entries_recursive (CachedDir  *dir,
                                   const char *dirname)
{
  DIR           *dp;
  struct dirent *dent;
  GString       *fullpath;
  gsize          fullpath_len;

  g_assert (dir != NULL);

  if (dir->have_read_entries)
    return TRUE;

  menu_verbose ("Attempting to read entries from %s (full path %s)\n",
                dir->name, dirname);

  dp = opendir (dirname);
  if (dp == NULL)
    {
      menu_verbose ("Unable to list directory \"%s\"\n",
                    dirname);
      return FALSE;
    }

  cached_dir_clear_entries (dir);
  cached_dir_ensure_monitor (dir, dirname);

  fullpath = g_string_new (dirname);
  if (fullpath->str[fullpath->len - 1] != '/')
    g_string_append_c (fullpath, '/');

  fullpath_len = fullpath->len;

  while ((dent = readdir (dp)) != NULL)
    {
      /* ignore . and .. */
      if (dent->d_name[0] == '.' &&
          (dent->d_name[1] == '\0' ||
           (dent->d_name[1] == '.' &&
            dent->d_name[2] == '\0')))
        continue;

      g_string_append (fullpath, dent->d_name);

      if (g_str_has_suffix (dent->d_name, ".desktop") ||
          g_str_has_suffix (dent->d_name, ".directory"))
        {
          cached_dir_add_entry (dir, dent->d_name, fullpath->str);
        }
      else /* Try recursing */
        {
          cached_dir_add_subdir (dir, dent->d_name, fullpath->str);
        }

      g_string_truncate (fullpath, fullpath_len);
    }

  closedir (dp);

  g_string_free (fullpath, TRUE);

  dir->have_read_entries = TRUE;

  return TRUE;
}

static char *
cached_dir_get_full_path (CachedDir *dir)
{
  GString *str;
  GSList  *parents = NULL;
  GSList  *tmp;

  if (dir == NULL || dir->parent == NULL)
    return g_strdup ("/");

  while (dir->parent != NULL)
    {
      parents = g_slist_prepend (parents, dir->name);
      dir = dir->parent;
    }

  str = g_string_new ("/");

  tmp = parents;
  while (tmp != NULL)
    {
      g_string_append (str, tmp->data);
      g_string_append_c (str, '/');

      tmp = tmp->next;
    }

  g_slist_free (parents);

  return g_string_free (str, FALSE);
}

static CachedDir *
cached_dir_load (const char *canonical_path)
{
  CachedDir *retval;

  menu_verbose ("Loading cached dir \"%s\"\n", canonical_path);

  retval = cached_dir_lookup (canonical_path);

  cached_dir_load_entries_recursive (retval, canonical_path);

  return retval;
}

static void
cached_dir_add_monitor (CachedDir                 *dir,
                        EntryDirectory            *ed,
                        EntryDirectoryChangedFunc  callback,
                        gpointer                   user_data)
{
  CachedDirMonitor *monitor;
  GSList           *tmp;

  tmp = dir->monitors;
  while (tmp != NULL)
    {
      monitor = tmp->data;

      if (monitor->ed == ed &&
          monitor->callback == callback &&
          monitor->user_data == user_data)
        break;

      tmp = tmp->next;
    }

  if (tmp == NULL)
    {
      monitor            = g_new0 (CachedDirMonitor, 1);
      monitor->ed        = ed;
      monitor->callback  = callback;
      monitor->user_data = user_data;

      dir->monitors = g_slist_append (dir->monitors, monitor);
    }
}

static void
cached_dir_remove_monitor (CachedDir                 *dir,
                           EntryDirectory            *ed,
                           EntryDirectoryChangedFunc  callback,
                           gpointer                   user_data)
{
  GSList *tmp;

  tmp = dir->monitors;
  while (tmp != NULL)
    {
      CachedDirMonitor *monitor = tmp->data;
      GSList           *next = tmp->next;

      if (monitor->ed == ed &&
          monitor->callback == callback &&
          monitor->user_data == user_data)
        {
          dir->monitors = g_slist_delete_link (dir->monitors, tmp);
          g_free (monitor);
        }

      tmp = next;
    }
}

static void
cached_dir_ensure_loaded (CachedDir *dir)
{
  char *path;

  if (dir->have_read_entries)
    return;

  path = cached_dir_get_full_path (dir);
  cached_dir_load_entries_recursive (dir, path);
  g_free (path);
}

static GSList*
cached_dir_get_subdirs (CachedDir *dir)
{
  cached_dir_ensure_loaded (dir);

  return dir->subdirs;
}

static GSList*
cached_dir_get_entries (CachedDir   *dir)
{
  cached_dir_ensure_loaded (dir);

  return dir->entries;
}

/*
 * Entry directories
 */

static EntryDirectory *
entry_directory_new_full (DesktopEntryType  entry_type,
                          const char       *path,
                          gboolean          is_legacy,
                          const char       *legacy_prefix)
{
  EntryDirectory *ed;
  CachedDir      *cd;
  char           *canonical;

  menu_verbose ("Loading entry directory \"%s\" (legacy %s)\n",
                path,
                is_legacy ? "<yes>" : "<no>");

  canonical = menu_canonicalize_file_name (path, FALSE);
  if (canonical == NULL)
    {
      menu_verbose ("Failed to canonicalize \"%s\": %s\n",
                    path, g_strerror (errno));
      return NULL;
    }

  cd = cached_dir_load (canonical);
  g_assert (cd != NULL);

  ed = g_new0 (EntryDirectory, 1);

  ed->dir           = cd;
  ed->legacy_prefix = g_strdup (legacy_prefix);
  ed->entry_type    = entry_type;
  ed->is_legacy     = is_legacy != FALSE;
  ed->refcount      = 1;

  g_free (canonical);

  return ed;
}

EntryDirectory *
entry_directory_new (DesktopEntryType  entry_type,
                     const char       *path)
{
  return entry_directory_new_full (entry_type, path, FALSE, NULL);
}

EntryDirectory *
entry_directory_new_legacy (DesktopEntryType  entry_type,
                            const char       *path,
                            const char       *legacy_prefix)
{
  return entry_directory_new_full (entry_type, path, TRUE, legacy_prefix);
}

EntryDirectory *
entry_directory_ref (EntryDirectory *ed)
{
  g_return_val_if_fail (ed != NULL, NULL);
  g_return_val_if_fail (ed->refcount > 0, NULL);

  ed->refcount++;

  return ed;
}

void
entry_directory_unref (EntryDirectory *ed)
{
  g_return_if_fail (ed != NULL);
  g_return_if_fail (ed->refcount > 0);

  if (--ed->refcount == 0)
    {
      ed->dir        = NULL;
      ed->entry_type = DESKTOP_ENTRY_INVALID;
      ed->is_legacy  = FALSE;

      g_free (ed->legacy_prefix);
      ed->legacy_prefix = NULL;

      g_free (ed);
    }
}

static void
entry_directory_add_monitor (EntryDirectory            *ed,
                             EntryDirectoryChangedFunc  callback,
                             gpointer                   user_data)
{
  cached_dir_add_monitor (ed->dir, ed, callback, user_data);
}

static void
entry_directory_remove_monitor (EntryDirectory            *ed,
                                EntryDirectoryChangedFunc  callback,
                                gpointer                   user_data)
{
  cached_dir_remove_monitor (ed->dir, ed, callback, user_data);
}

static DesktopEntry *
entry_directory_get_desktop (EntryDirectory *ed,
                             const char     *file_id)
{
  DesktopEntry *entry;
  const char   *unprefixed_id;

  if (ed->entry_type != DESKTOP_ENTRY_DESKTOP)
    return NULL;

  unprefixed_id = NULL;

  if (ed->is_legacy && ed->legacy_prefix)
    {
      if (!g_str_has_prefix (file_id, ed->legacy_prefix))
        return NULL;

      unprefixed_id = file_id + strlen (ed->legacy_prefix);

      if (unprefixed_id[0] != '-')
	return NULL;

      unprefixed_id++;
    }

  entry = cached_dir_find_file_id (ed->dir,
                                   unprefixed_id ? unprefixed_id : file_id,
                                   ed->is_legacy);
  if (entry == NULL || desktop_entry_get_type (entry) != DESKTOP_ENTRY_DESKTOP)
    return NULL;

  if (ed->is_legacy && !desktop_entry_has_categories (entry))
    {
      entry = desktop_entry_copy (entry);
      desktop_entry_add_legacy_category (entry);
      return entry;
    }

  return desktop_entry_ref (entry);
}

static DesktopEntry *
entry_directory_get_directory (EntryDirectory *ed,
                               const char     *relative_path)
{
  DesktopEntry *entry;

  if (ed->entry_type != DESKTOP_ENTRY_DIRECTORY)
    return NULL;

  entry = cached_dir_find_relative_path (ed->dir, relative_path);
  if (entry == NULL || desktop_entry_get_type (entry) != DESKTOP_ENTRY_DIRECTORY)
    return NULL;

  return desktop_entry_ref (entry);
}

static char *
get_desktop_file_id_from_path (EntryDirectory *ed,
                               const char     *relative_path)
{
  char *retval;

  if (!ed->is_legacy)
    {
      retval = g_strdelimit (g_strdup (relative_path), "/", '-');
    }
  else
    {
      char *basename;

      basename = g_path_get_basename (relative_path);

      if (ed->legacy_prefix)
        {
          retval = g_strjoin ("-", ed->legacy_prefix, basename, NULL);
          g_free (basename);
        }
      else
        {
          retval = basename;
        }
    }

  return retval;
}

typedef gboolean (* EntryDirectoryForeachFunc) (EntryDirectory  *ed,
                                                DesktopEntry    *entry,
                                                const char      *relative_path,
                                                const char      *file_id,
                                                DesktopEntrySet *set,
                                                gpointer         user_data);

static gboolean
entry_directory_foreach_recursive (EntryDirectory            *ed,
                                   CachedDir                 *cd,
                                   GString                   *relative_path,
                                   EntryDirectoryForeachFunc  func,
                                   DesktopEntrySet           *set,
                                   gpointer                   user_data)
{
  GSList *tmp;
  int     relative_path_len;

  relative_path_len = relative_path->len;

  tmp = cached_dir_get_entries (cd);
  while (tmp != NULL)
    {
      DesktopEntry *entry = tmp->data;

      if (desktop_entry_get_type (entry) == ed->entry_type)
        {
          gboolean  ret;
          char     *file_id;

          g_string_append (relative_path,
                           desktop_entry_get_basename (entry));

          file_id = NULL;
          if (ed->entry_type == DESKTOP_ENTRY_DESKTOP)
            file_id = get_desktop_file_id_from_path (ed, relative_path->str);

          ret = func (ed, entry, relative_path->str, file_id, set, user_data);

          g_free (file_id);

          g_string_truncate (relative_path, relative_path_len);

          if (!ret)
            return FALSE;
        }

      tmp = tmp->next;
    }

  tmp = cached_dir_get_subdirs (cd);
  while (tmp != NULL)
    {
      CachedDir *subdir = tmp->data;

      g_string_append (relative_path, subdir->name);
      g_string_append_c (relative_path, '/');

      if (!entry_directory_foreach_recursive (ed,
                                              subdir,
                                              relative_path,
                                              func,
                                              set,
                                              user_data))
        return FALSE;

      g_string_truncate (relative_path, relative_path_len);

      tmp = tmp->next;
    }

  return TRUE;
}

static void
entry_directory_foreach (EntryDirectory            *ed,
                         EntryDirectoryForeachFunc  func,
                         DesktopEntrySet           *set,
                         gpointer                   user_data)
{
  GString *path;

  path = g_string_new (NULL);

  entry_directory_foreach_recursive (ed,
                                     ed->dir,
                                     path,
                                     func,
                                     set,
                                     user_data);

  g_string_free (path, TRUE);
}

void
entry_directory_get_flat_contents (EntryDirectory   *ed,
                                   DesktopEntrySet  *desktop_entries,
                                   DesktopEntrySet  *directory_entries,
                                   GSList          **subdirs)
{
  GSList *tmp;

  if (subdirs)
    *subdirs = NULL;

  tmp = cached_dir_get_entries (ed->dir);
  while (tmp != NULL)
    {
      DesktopEntry *entry = tmp->data;
      const char   *basename;

      basename = desktop_entry_get_basename (entry);

      if (desktop_entries &&
          desktop_entry_get_type (entry) == DESKTOP_ENTRY_DESKTOP)
        {
          char *file_id;

          file_id = get_desktop_file_id_from_path (ed, basename);

          desktop_entry_set_add_entry (desktop_entries,
                                       entry,
                                       file_id);

          g_free (file_id);
        }

      if (directory_entries &&
          desktop_entry_get_type (entry) == DESKTOP_ENTRY_DIRECTORY)
        {
          desktop_entry_set_add_entry (directory_entries,
                                       entry,
                                       g_strdup (basename));
        }

      tmp = tmp->next;
    }

  if (subdirs)
    {
      tmp = cached_dir_get_subdirs (ed->dir);
      while (tmp != NULL)
        {
          CachedDir *cd = tmp->data;

          *subdirs = g_slist_prepend (*subdirs, g_strdup (cd->name));

          tmp = tmp->next;
        }
    }

  if (subdirs)
    *subdirs = g_slist_reverse (*subdirs);
}

/*
 * Entry directory lists
 */

EntryDirectoryList *
entry_directory_list_new (void)
{
  EntryDirectoryList *list;

  list = g_new0 (EntryDirectoryList, 1);

  list->refcount = 1;
  list->dirs = NULL;
  list->length = 0;

  return list;
}

EntryDirectoryList *
entry_directory_list_ref (EntryDirectoryList *list)
{
  g_return_val_if_fail (list != NULL, NULL);
  g_return_val_if_fail (list->refcount > 0, NULL);

  list->refcount += 1;

  return list;
}

void
entry_directory_list_unref (EntryDirectoryList *list)
{
  g_return_if_fail (list != NULL);
  g_return_if_fail (list->refcount > 0);

  list->refcount -= 1;
  if (list->refcount == 0)
    {
      g_list_foreach (list->dirs, (GFunc) entry_directory_unref, NULL);
      g_list_free (list->dirs);
      list->dirs = NULL;
      list->length = 0;
      g_free (list);
    }
}

void
entry_directory_list_prepend  (EntryDirectoryList *list,
                               EntryDirectory     *ed)
{
  list->length += 1;
  list->dirs = g_list_prepend (list->dirs,
                               entry_directory_ref (ed));
}

int
entry_directory_list_get_length (EntryDirectoryList *list)
{
  return list->length;
}

void
entry_directory_list_append_list (EntryDirectoryList *list,
                                  EntryDirectoryList *to_append)
{
  GList *tmp;
  GList *new_dirs = NULL;

  if (to_append->length == 0)
    return;

  tmp = to_append->dirs;
  while (tmp != NULL)
    {
      list->length += 1;
      new_dirs = g_list_prepend (new_dirs,
                                 entry_directory_ref (tmp->data));

      tmp = tmp->next;
    }

  new_dirs   = g_list_reverse (new_dirs);
  list->dirs = g_list_concat (list->dirs, new_dirs);
}

DesktopEntry *
entry_directory_list_get_desktop (EntryDirectoryList *list,
                                  const char         *file_id)
{
  DesktopEntry *retval = NULL;
  GList        *tmp;

  tmp = list->dirs;
  while (tmp != NULL)
    {
      if ((retval = entry_directory_get_desktop (tmp->data, file_id)) != NULL)
        break;

      tmp = tmp->next;
    }

  return retval;
}

DesktopEntry *
entry_directory_list_get_directory (EntryDirectoryList *list,
                                    const char         *relative_path)
{
  DesktopEntry *retval = NULL;
  GList        *tmp;

  tmp = list->dirs;
  while (tmp != NULL)
    {
      if ((retval = entry_directory_get_directory (tmp->data, relative_path)) != NULL)
        break;

      tmp = tmp->next;
    }

  return retval;
}

static void
entry_directory_list_add (EntryDirectoryList        *list,
                          EntryDirectoryForeachFunc  func,
                          DesktopEntrySet           *set,
                          gpointer                   user_data)
{
  GList *tmp;

  /* The only tricky thing here is that desktop files later
   * in the search list with the same relative path
   * are "hidden" by desktop files earlier in the path,
   * so we have to do the earlier files first causing
   * the later files to replace the earlier files
   * in the DesktopEntrySet
   *
   * We go from the end of the list so we can just
   * g_hash_table_replace and not have to do two
   * hash lookups (check for existing entry, then insert new
   * entry)
   */

  tmp = g_list_last (list->dirs);
  while (tmp != NULL)
    {
      entry_directory_foreach (tmp->data, func, set, user_data);

      tmp = tmp->prev;
    }
}

static gboolean
get_all_func (EntryDirectory   *ed,
              DesktopEntry     *entry,
              const char       *relative_path,
              const char       *file_id,
              DesktopEntrySet  *set,
              gpointer          user_data)
{
  if (ed->is_legacy && !desktop_entry_has_categories (entry))
    {
      entry = desktop_entry_copy (entry);
      desktop_entry_add_legacy_category (entry);
    }
  else
    {
      entry = desktop_entry_ref (entry);
    }

  desktop_entry_set_add_entry (set,
                               entry,
                               file_id ? file_id : relative_path);
  desktop_entry_unref (entry);

  return TRUE;
}

void
entry_directory_list_get_all_desktops (EntryDirectoryList *list,
                                       DesktopEntrySet    *set)
{
  menu_verbose (" Storing all of list %p in set %p\n",
                list, set);

  entry_directory_list_add (list, get_all_func, set, NULL);
}

void
entry_directory_list_add_monitors (EntryDirectoryList        *list,
                                   EntryDirectoryChangedFunc  callback,
                                   gpointer                   user_data)
{
  GList *tmp;

  tmp = list->dirs;
  while (tmp != NULL)
    {
      entry_directory_add_monitor (tmp->data, callback, user_data);
      tmp = tmp->next;
    }
}

void
entry_directory_list_remove_monitors (EntryDirectoryList        *list,
                                      EntryDirectoryChangedFunc  callback,
                                      gpointer                   user_data)
{
  GList *tmp;

  tmp = list->dirs;
  while (tmp != NULL)
    {
      entry_directory_remove_monitor (tmp->data, callback, user_data);
      tmp = tmp->next;
    }
}
