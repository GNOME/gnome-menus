/*
 * Copyright (C) 2003, 2004 Red Hat, Inc.
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

#include "menu-tree.h"

#include <string.h>
#include <errno.h>
#include <libgnomevfs/gnome-vfs.h>


#include "menu-layout.h"
#include "menu-util.h"
#include "canonicalize.h"

/*
 * FIXME: it might be useful to be able to construct a menu
 * tree from a traditional directory based menu hierarchy
 * too.
 */

typedef enum
{
  MENU_TREE_ABSOLUTE = 0,
  MENU_TREE_BASENAME = 1
} MenuTreeType;

struct MenuTree
{
  MenuTreeType type;
  guint        refcount;

  char *basename;
  char *absolute_path;
  char *canonical_path;

  GSList *menu_file_monitors;

  MenuLayoutNode *layout;
  MenuTreeDirectory *root;

  GSList *monitors;

  guint canonical : 1;
};

typedef struct
{
  MenuTreeChangedFunc callback;
  gpointer            user_data;
} MenuTreeMonitor;

struct MenuTreeDirectory
{
  MenuTreeDirectory *parent;

  DesktopEntry *directory_entry;
  char         *name;

  GSList *entries;
  GSList *subdirs;

  guint refcount : 24;
  guint only_unallocated : 1;
};

struct MenuTreeEntry
{
  MenuTreeDirectory *parent;

  DesktopEntry *desktop_entry;
  char         *desktop_file_id;

  guint refcount : 24;
};

static MenuTree *menu_tree_new                  (MenuTreeType    type,
						 const char     *menu_file,
						 gboolean        canonical);
static void      menu_tree_load_layout          (MenuTree       *tree);
static void      menu_tree_force_reload         (MenuTree       *tree);
static void      menu_tree_build_from_layout    (MenuTree       *tree);
static void      menu_tree_force_rebuild        (MenuTree       *tree);
static void      menu_tree_resolve_files        (MenuTree       *tree,
						 MenuLayoutNode *layout);
static void      menu_tree_force_recanonicalize (MenuTree       *tree);
static void      menu_tree_invoke_monitors      (MenuTree       *tree);
     
static void menu_tree_add_menu_file_monitor     (MenuTree   *tree,
						 const char *path,
						 gboolean    existent);
static void menu_tree_remove_menu_file_monitors (MenuTree   *tree);



/*
 * The idea is that we cache the menu tree for either a given
 * menu basename or an absolute menu path.
 * If no files exist in $XDG_DATA_DIRS for the basename or the
 * absolute path doesn't exist we just return (and cache) the
 * empty menu tree.
 * We also add a file monitor for the basename in each dir in
 * $XDG_DATA_DIRS, or the absolute path to the menu file, and
 * re-compute if there are any changes.
 */

static GHashTable *menu_tree_cache = NULL;

static inline char *
get_cache_key (MenuTree *tree)
{
  switch (tree->type)
    {
    case MENU_TREE_ABSOLUTE:
      return tree->canonical ? tree->canonical_path : tree->absolute_path;

    case MENU_TREE_BASENAME:
      return tree->basename;
    }

  return NULL;
}

static void
menu_tree_add_to_cache (MenuTree *tree)
{
  char *cache_key;

  if (menu_tree_cache == NULL)
    {
      menu_tree_cache =
        g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
    }

  cache_key = get_cache_key (tree);

  menu_verbose ("Adding menu tree to cache: %s\n", cache_key);

  g_hash_table_replace (menu_tree_cache, cache_key, tree);
}

static void
menu_tree_remove_from_cache (MenuTree *tree)
{
  char *cache_key;

  cache_key = get_cache_key (tree);

  menu_verbose ("Removing menu tree from cache: %s\n", cache_key);

  g_hash_table_remove (menu_tree_cache, cache_key);

  if (g_hash_table_size (menu_tree_cache) == 0)
    {
      g_hash_table_destroy (menu_tree_cache);
      menu_tree_cache = NULL;
    }
}

static void
handle_nonexistent_menu_file_changed (GnomeVFSMonitorHandle    *handle,
				      const char               *monitor_uri,
				      const char               *info_uri,
				      GnomeVFSMonitorEventType  event,
				      MenuTree                 *tree)
{
  if (event == GNOME_VFS_MONITOR_EVENT_CHANGED ||
      event == GNOME_VFS_MONITOR_EVENT_CREATED)
    {
      menu_verbose ("File \"%s\" %s, marking tree for recanonicalization\n",
                    info_uri,
                    event == GNOME_VFS_MONITOR_EVENT_CREATED ? ("created") : ("changed"));

      menu_tree_force_recanonicalize (tree);
      menu_tree_invoke_monitors (tree);
    }
}

static void
handle_menu_file_changed (GnomeVFSMonitorHandle    *handle,
                          const char               *monitor_uri,
                          const char               *info_uri,
                          GnomeVFSMonitorEventType  event,
                          MenuTree                 *tree)
{
  if (event == GNOME_VFS_MONITOR_EVENT_DELETED)
    {
      menu_verbose ("File \"%s\" deleted, marking tree for recanicalization\n",
                    info_uri);

      menu_tree_force_recanonicalize (tree);
      menu_tree_invoke_monitors (tree);
    }
  else if (event == GNOME_VFS_MONITOR_EVENT_CHANGED ||
	   event == GNOME_VFS_MONITOR_EVENT_CREATED)
    {
      menu_verbose ("File \"%s\" %s, marking layout for reload\n",
                    monitor_uri,
                    event == GNOME_VFS_MONITOR_EVENT_CREATED ? ("created") : ("changed"));

      menu_tree_force_reload (tree);
      menu_tree_invoke_monitors (tree);
    }
}

static void
menu_tree_add_menu_file_monitor (MenuTree   *tree,
                                 const char *path,
				 gboolean    existent)
{
  GnomeVFSMonitorHandle *handle;
  GnomeVFSResult         result;
  char                  *uri;

  menu_verbose ("Adding a menu file monitor for %sexistent \"%s\"\n",
		existent ? "" : "non", path);

  uri = gnome_vfs_get_uri_from_local_path (path);

  handle = NULL;
  if (existent)
    {
      result = gnome_vfs_monitor_add (&handle,
				      uri,
				      GNOME_VFS_MONITOR_FILE,
				      (GnomeVFSMonitorCallback) handle_menu_file_changed,
				      tree);
    }
  else
    {
      result = gnome_vfs_monitor_add (&handle,
				      uri,
				      GNOME_VFS_MONITOR_FILE,
				      (GnomeVFSMonitorCallback) handle_nonexistent_menu_file_changed,
				      tree);
    }

  if (result == GNOME_VFS_OK)
    {
      tree->menu_file_monitors = g_slist_prepend (tree->menu_file_monitors, handle);
    }
  else
    {
      g_assert (handle == NULL);
      menu_verbose ("Failed to add file monitor to for %s: %s\n",
                    path, gnome_vfs_result_to_string (result));
    }

  g_free (uri);
}

static void
menu_tree_remove_menu_file_monitors (MenuTree *tree)
{
  menu_verbose ("Removing all menu file monitors\n");

  g_slist_foreach (tree->menu_file_monitors,
                   (GFunc) gnome_vfs_monitor_cancel,
                   NULL);
  g_slist_free (tree->menu_file_monitors);
  tree->menu_file_monitors = NULL;
}

static MenuTree *
menu_tree_lookup_absolute (const char *absolute)
{
  MenuTree   *tree;
  gboolean    canonical;
  const char *canonical_path;
  char       *freeme;

  menu_verbose ("Looking up absolute path in tree cache: \"%s\"\n", absolute);

  if (menu_tree_cache != NULL &&
      (tree = g_hash_table_lookup (menu_tree_cache, absolute)) != NULL)
    return menu_tree_ref (tree);

  canonical = TRUE;
  canonical_path = freeme = menu_canonicalize_file_name (absolute, FALSE);
  if (canonical_path == NULL)
    {
      menu_verbose ("Failed to canonicalize absolute menu path \"%s\": %s\n",
                    absolute, g_strerror (errno));
      canonical = FALSE;
      canonical_path = absolute;
    }

  if (menu_tree_cache &&
      (tree = g_hash_table_lookup (menu_tree_cache, canonical_path)) != NULL)
    return menu_tree_ref (tree);

  tree = menu_tree_new (MENU_TREE_ABSOLUTE, canonical_path, canonical);

  g_free (freeme);

  return tree;
}

static MenuTree *
menu_tree_lookup_basename (const char *basename)
{
  MenuTree *tree;

  menu_verbose ("Looking up menu file in tree cache: \"%s\"\n", basename);

  if (menu_tree_cache &&
      (tree = g_hash_table_lookup (menu_tree_cache, basename)) != NULL)
    return menu_tree_ref (tree);

  return menu_tree_new (MENU_TREE_BASENAME, basename, FALSE);
}

static gboolean
canonicalize_basename_with_config_dir (MenuTree   *tree,
                                       const char *basename,
                                       const char *config_dir)
{
  char *path;

  path = g_build_filename (config_dir, "menus",  basename,  NULL);

  tree->canonical_path = menu_canonicalize_file_name (path, FALSE);
  if (tree->canonical_path)
    {
      tree->canonical = TRUE;
      menu_tree_add_menu_file_monitor (tree, tree->canonical_path, TRUE);
    }
  else
    {
      menu_tree_add_menu_file_monitor (tree, path, FALSE);
    }

  g_free (path);

  return tree->canonical;
}

static gboolean
menu_tree_canonicalize_path (MenuTree *tree)
{
  if (tree->canonical)
    return TRUE;

  g_assert (tree->canonical_path == NULL);

  if (tree->type == MENU_TREE_BASENAME)
    {
      menu_tree_remove_menu_file_monitors (tree);

      if (!canonicalize_basename_with_config_dir (tree,
						  tree->basename,
						  g_get_user_config_dir ()))
	{
	  const char * const *system_config_dirs;
	  int                 i;

	  system_config_dirs = g_get_system_config_dirs ();

	  i = 0;
	  while (system_config_dirs[i] != NULL)
	    {
	      if (canonicalize_basename_with_config_dir (tree,
							 tree->basename,
							 system_config_dirs[i]))
		break;

	      ++i;
	    }
	}

      if (tree->canonical)
        menu_verbose ("Successfully looked up menu_file for \"%s\": %s\n",
                      tree->basename, tree->canonical_path);
      else
        menu_verbose ("Failed to look up menu_file for \"%s\"\n",
                      tree->basename);
    }
  else /* if (tree->type == MENU_TREE_ABSOLUTE) */
    {
      tree->canonical_path =
        menu_canonicalize_file_name (tree->absolute_path, FALSE);
      if (tree->canonical_path != NULL)
        {
          menu_verbose ("Successfully looked up menu_file for \"%s\": %s\n",
                        tree->absolute_path, tree->canonical_path);

	  /*
	   * Replace the cache entry with the canonicalized version
	   */
          menu_tree_remove_from_cache (tree);

          menu_tree_remove_menu_file_monitors (tree);
          menu_tree_add_menu_file_monitor (tree,
                                           tree->canonical_path,
					   TRUE);

          tree->canonical = TRUE;

          menu_tree_add_to_cache (tree);
        }
      else
        {
          menu_verbose ("Failed to look up menu_file for \"%s\"\n",
                        tree->absolute_path);
        }
    }

  return tree->canonical;
}

static void
menu_tree_force_recanonicalize (MenuTree *tree)
{
  menu_tree_remove_menu_file_monitors (tree);

  if (tree->canonical)
    {
      menu_tree_force_reload (tree);

      g_free (tree->canonical_path);
      tree->canonical_path = NULL;

      tree->canonical = FALSE;
    }
}

MenuTree *
menu_tree_lookup (const char *menu_file)
{
  MenuTree *retval;

  g_return_val_if_fail (menu_file != NULL, NULL);

  if (g_path_is_absolute (menu_file))
    retval = menu_tree_lookup_absolute (menu_file);
  else
    retval = menu_tree_lookup_basename (menu_file);

  g_assert (retval != NULL);

  return retval;
}

static MenuTree *
menu_tree_new (MenuTreeType  type,
               const char   *menu_file,
               gboolean      canonical)
{
  MenuTree *tree;

  tree = g_new0 (MenuTree, 1);

  tree->type     = type;
  tree->refcount = 1;

  if (tree->type == MENU_TREE_BASENAME)
    {
      g_assert (canonical == FALSE);
      tree->basename = g_strdup (menu_file);
    }
  else
    {
      tree->canonical     = canonical != FALSE;
      tree->absolute_path = g_strdup (menu_file);

      if (tree->canonical)
	{
	  tree->canonical_path = g_strdup (menu_file);
	  menu_tree_add_menu_file_monitor (tree, tree->canonical_path, TRUE);
	}
      else
	{
	  menu_tree_add_menu_file_monitor (tree, tree->absolute_path, FALSE);
	}
    }

  menu_tree_add_to_cache (tree);

  return tree;
}

MenuTree *
menu_tree_ref (MenuTree *tree)
{
  g_return_val_if_fail (tree != NULL, NULL);
  g_return_val_if_fail (tree->refcount > 0, NULL);

  tree->refcount++;

  return tree;
}

void
menu_tree_unref (MenuTree *tree)
{
  g_return_if_fail (tree != NULL);
  g_return_if_fail (tree->refcount >= 1);

  if (--tree->refcount > 0)
    return;

  menu_tree_remove_from_cache (tree);

  menu_tree_force_recanonicalize (tree);

  if (tree->basename != NULL)
    g_free (tree->basename);
  tree->basename = NULL;

  if (tree->absolute_path != NULL)
    g_free (tree->absolute_path);
  tree->absolute_path = NULL;

  g_slist_foreach (tree->monitors, (GFunc) g_free, NULL);
  g_slist_free (tree->monitors);
  tree->monitors = NULL;

  g_free (tree);
}

MenuTreeDirectory *
menu_tree_get_root_directory (MenuTree *tree)
{
  g_return_val_if_fail (tree != NULL, NULL);

  if (!tree->root)
    {
      menu_tree_build_from_layout (tree);

      if (!tree->root)
        return NULL;
    }

  return menu_tree_directory_ref (tree->root);
}

static MenuTreeDirectory *
find_path (MenuTreeDirectory *directory,
	   const char        *path)
{
  const char *name;
  char       *slash;
  char       *freeme;
  GSList     *tmp;

  while (path[0] == '/') path++;

  if (path[0] == '\0')
    return directory;

  freeme = NULL;
  slash = strchr (path, '/');
  if (slash)
    {
      name = freeme = g_strndup (path, slash - path);
      path = slash + 1;
    }
  else
    {
      name = path;
      path = NULL;
    }

  tmp = directory->subdirs;
  while (tmp != NULL)
    {
      MenuTreeDirectory *subdir = tmp->data;

      if (!strcmp (name, subdir->name))
	{
	  g_free (freeme);

	  if (path)
	    return find_path (subdir, path);
	  else
	    return subdir;
	}

      tmp = tmp->next;
    }

  g_free (freeme);

  return NULL;
}

MenuTreeDirectory *
menu_tree_get_directory_from_path (MenuTree   *tree,
				   const char *path)
{
  MenuTreeDirectory *root;
  MenuTreeDirectory *directory;

  g_return_val_if_fail (tree != NULL, NULL);
  g_return_val_if_fail (path != NULL, NULL);

  if (path[0] != '/')
    return NULL;

  if (!(root = menu_tree_get_root_directory (tree)))
    return NULL;

  directory = find_path (root, path);

  menu_tree_directory_unref (root);

  return directory ? menu_tree_directory_ref (directory) : NULL;
}

void
menu_tree_add_monitor (MenuTree            *tree,
                       MenuTreeChangedFunc  callback,
                       gpointer             user_data)
{
  MenuTreeMonitor *monitor;
  GSList          *tmp;

  g_return_if_fail (tree != NULL);
  g_return_if_fail (callback != NULL);

  tmp = tree->monitors;
  while (tmp != NULL)
    {
      monitor = tmp->data;

      if (monitor->callback  == callback &&
          monitor->user_data == user_data)
        break;

      tmp = tmp->next;
    }

  if (tmp == NULL)
    {
      monitor = g_new0 (MenuTreeMonitor, 1);

      monitor->callback  = callback;
      monitor->user_data = user_data;

      tree->monitors = g_slist_append (tree->monitors, monitor);
    }
}

void
menu_tree_remove_monitor (MenuTree            *tree,
                          MenuTreeChangedFunc  callback,
                          gpointer             user_data)
{
  GSList *tmp;

  g_return_if_fail (tree != NULL);
  g_return_if_fail (callback != NULL);

  tmp = tree->monitors;
  while (tmp != NULL)
    {
      MenuTreeMonitor *monitor = tmp->data;
      GSList          *next = tmp->next;

      if (monitor->callback  == callback &&
          monitor->user_data == user_data)
        {
          tree->monitors = g_slist_delete_link (tree->monitors, tmp);
          g_free (monitor);
        }

      tmp = next;
    }
}

static void
menu_tree_invoke_monitors (MenuTree *tree)
{
  GSList *tmp;

  tmp = tree->monitors;
  while (tmp != NULL)
    {
      MenuTreeMonitor *monitor = tmp->data;
      GSList          *next    = tmp->next;

      monitor->callback (tree, monitor->user_data);

      tmp = next;
    }
}

GSList *
menu_tree_directory_get_entries (MenuTreeDirectory *directory)
{
  GSList *retval;
  GSList *tmp;

  g_return_val_if_fail (directory != NULL, NULL);

  retval = NULL;

  tmp = directory->entries;
  while (tmp != NULL)
    {
      retval = g_slist_prepend (retval,
                                menu_tree_entry_ref (tmp->data));

      tmp = tmp->next;
    }

  return g_slist_reverse (retval);
}

GSList *
menu_tree_directory_get_subdirs (MenuTreeDirectory *directory)
{
  GSList *retval;
  GSList *tmp;

  g_return_val_if_fail (directory != NULL, NULL);

  retval = NULL;

  tmp = directory->subdirs;
  while (tmp != NULL)
    {
      retval = g_slist_prepend (retval,
                                menu_tree_directory_ref (tmp->data));

      tmp = tmp->next;
    }

  return g_slist_reverse (retval);
}

MenuTreeDirectory *
menu_tree_directory_get_parent (MenuTreeDirectory *directory)
{
  g_return_val_if_fail (directory != NULL, NULL);

  return directory->parent ? menu_tree_directory_ref (directory->parent) : NULL;
}

const char *
menu_tree_directory_get_name (MenuTreeDirectory *directory)
{
  g_return_val_if_fail (directory != NULL, NULL);

  if (!directory->directory_entry)
    return directory->name;

  return desktop_entry_get_name (directory->directory_entry);
}

const char *
menu_tree_directory_get_comment (MenuTreeDirectory *directory)
{
  g_return_val_if_fail (directory != NULL, NULL);

  if (!directory->directory_entry)
    return NULL;

  return desktop_entry_get_comment (directory->directory_entry);
}

const char *
menu_tree_directory_get_icon (MenuTreeDirectory *directory)
{
  g_return_val_if_fail (directory != NULL, NULL);

  if (!directory->directory_entry)
    return NULL;

  return desktop_entry_get_icon (directory->directory_entry);
}

static void
append_directory_path (MenuTreeDirectory *directory,
		       GString           *path)
{

  if (!directory->parent)
    {
      g_string_append_c (path, '/');
      return;
    }

  append_directory_path (directory->parent, path);

  g_string_append (path, directory->name);
  g_string_append_c (path, '/');
}

char *
menu_tree_directory_make_path (MenuTreeDirectory *directory,
			       MenuTreeEntry     *entry)
{
  GString *path;

  g_return_val_if_fail (directory != NULL, NULL);

  path = g_string_new (NULL);

  append_directory_path (directory, path);

  if (entry != NULL)
    g_string_append (path,
		     desktop_entry_get_basename (entry->desktop_entry));

  return g_string_free (path, FALSE);
}

MenuTreeDirectory *
menu_tree_entry_get_parent (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return entry->parent ? menu_tree_directory_ref (entry->parent) : NULL;
}

const char *
menu_tree_entry_get_name (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return desktop_entry_get_name (entry->desktop_entry);
}

const char *
menu_tree_entry_get_comment (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return desktop_entry_get_comment (entry->desktop_entry);
}

const char *
menu_tree_entry_get_icon (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return desktop_entry_get_icon (entry->desktop_entry);
}

const char *
menu_tree_entry_get_exec (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return desktop_entry_get_exec (entry->desktop_entry);
}

const char *
menu_tree_entry_get_desktop_file_path (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return desktop_entry_get_path (entry->desktop_entry);
}

const char *
menu_tree_entry_get_desktop_file_id (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return entry->desktop_file_id;
}

static MenuTreeDirectory *
menu_tree_directory_new (MenuTreeDirectory *parent,
                         const char        *name)
{
  MenuTreeDirectory *retval;

  retval = g_new0 (MenuTreeDirectory, 1);

  retval->parent           = parent;
  retval->name             = g_strdup (name);
  retval->directory_entry  = NULL;
  retval->entries          = NULL;
  retval->subdirs          = NULL;
  retval->only_unallocated = FALSE;
  retval->refcount         = 1;

  return retval;
}

MenuTreeDirectory *
menu_tree_directory_ref (MenuTreeDirectory *directory)
{
  g_return_val_if_fail (directory != NULL, NULL);
  g_return_val_if_fail (directory->refcount > 0, NULL);

  directory->refcount++;

  return directory;
}

void
menu_tree_directory_unref (MenuTreeDirectory *directory)
{
  g_return_if_fail (directory != NULL);
  g_return_if_fail (directory->refcount > 0);

  if (--directory->refcount == 0)
    {
      g_slist_foreach (directory->subdirs,
                       (GFunc) menu_tree_directory_unref,
                       NULL);
      g_slist_free (directory->subdirs);
      directory->subdirs = NULL;

      g_slist_foreach (directory->entries,
                       (GFunc) menu_tree_entry_unref,
                       NULL);
      g_slist_free (directory->entries);
      directory->entries = NULL;

      if (directory->directory_entry)
        desktop_entry_unref (directory->directory_entry);
      directory->directory_entry = NULL;

      g_free (directory->name);
      directory->name = NULL;

      directory->parent = NULL;

      g_free (directory);
    }
}

static int
menu_tree_directory_compare (MenuTreeDirectory *a,
                             MenuTreeDirectory *b)
{
  const char *name_a;
  const char *name_b;

  if (a->directory_entry)
    name_a = desktop_entry_get_name (a->directory_entry);
  else
    name_a = a->name;

  if (b->directory_entry)
    name_b = desktop_entry_get_name (b->directory_entry);
  else
    name_b = b->name;

  return g_utf8_collate (name_a, name_b);
}

static MenuTreeEntry *
menu_tree_entry_new (MenuTreeDirectory *parent,
		     DesktopEntry      *desktop_entry,
                     const char        *desktop_file_id)
{
  MenuTreeEntry *retval;

  retval = g_new0 (MenuTreeEntry, 1);

  retval->parent          = menu_tree_directory_ref (parent);
  retval->desktop_entry   = desktop_entry_ref (desktop_entry);
  retval->desktop_file_id = g_strdup (desktop_file_id);
  retval->refcount        = 1;

  return retval;
}

MenuTreeEntry *
menu_tree_entry_ref (MenuTreeEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);
  g_return_val_if_fail (entry->refcount > 0, NULL);

  entry->refcount++;

  return entry;
}

void
menu_tree_entry_unref (MenuTreeEntry *entry)
{
  g_return_if_fail (entry != NULL);
  g_return_if_fail (entry->refcount > 0);

  if (--entry->refcount == 0)
    {
      g_free (entry->desktop_file_id);
      entry->desktop_file_id = NULL;

      if (entry->desktop_entry)
        desktop_entry_unref (entry->desktop_entry);
      entry->desktop_entry = NULL;

      if (entry->parent)
	menu_tree_directory_unref (entry->parent);
      entry->parent = NULL;

      g_free (entry);
    }
}

static int
menu_tree_entry_compare (MenuTreeEntry *a,
                         MenuTreeEntry *b)
{
  return g_utf8_collate (desktop_entry_get_name (a->desktop_entry),
                         desktop_entry_get_name (b->desktop_entry));
}

static MenuLayoutNode *
find_menu_child (MenuLayoutNode *layout)
{
  MenuLayoutNode *child;

  child = menu_layout_node_get_children (layout);
  while (child && menu_layout_node_get_type (child) != MENU_LAYOUT_NODE_MENU)
    child = menu_layout_node_get_next (child);

  return child;
}

static void
merge_resolved_children (MenuTree       *tree,
                         MenuLayoutNode *where,
                         MenuLayoutNode *from)
{
  MenuLayoutNode *insert_after;
  MenuLayoutNode *menu_child;
  MenuLayoutNode *from_child;

  menu_tree_resolve_files (tree, from);

  insert_after = where;
  g_assert (menu_layout_node_get_type (insert_after) != MENU_LAYOUT_NODE_ROOT);
  g_assert (menu_layout_node_get_parent (insert_after) != NULL);

  /* skip root node */
  menu_child = find_menu_child (from);
  g_assert (menu_child != NULL);
  g_assert (menu_layout_node_get_type (menu_child) == MENU_LAYOUT_NODE_MENU);

  /* merge children of toplevel <Menu> */
  from_child = menu_layout_node_get_children (menu_child);
  while (from_child != NULL)
    {
      MenuLayoutNode *next;

      next = menu_layout_node_get_next (from_child);

      menu_verbose ("Merging %p after %p\n", from_child, insert_after);

      switch (menu_layout_node_get_type (from_child))
        {
        case MENU_LAYOUT_NODE_NAME:
          menu_layout_node_unlink (from_child); /* delete this */
          break;

        default:
          menu_layout_node_steal (from_child);
          menu_layout_node_insert_after (insert_after, from_child);
          menu_layout_node_unref (from_child);

          insert_after = from_child;
          break;
        }

      from_child = next;
    }
}

static void
load_merge_file (MenuTree       *tree,
                 const char     *filename,
                 gboolean        is_canonical,
                 MenuLayoutNode *where)
{
  MenuLayoutNode *to_merge;
  const char     *canonical;
  char           *freeme = NULL;

  if (!is_canonical)
    {
      canonical = freeme = menu_canonicalize_file_name (filename, FALSE);
      if (canonical == NULL)
        {
          menu_verbose ("Failed to canonicalize merge file path \"%s\": %s\n",
                        filename, g_strerror (errno));
          return;
        }
    }
  else
    {
      canonical = filename;
    }

  menu_verbose ("Merging file \"%s\"\n", canonical);

  to_merge = menu_layout_load (canonical, NULL);
  if (to_merge == NULL)
    {
      menu_verbose ("No menu for file \"%s\" found when merging\n",
                    canonical);
      return;
    }

  menu_tree_add_menu_file_monitor (tree, canonical, TRUE);

  merge_resolved_children (tree, where, to_merge);

  menu_layout_node_unref (to_merge);

  if (freeme)
    g_free (freeme);
}

static void
load_merge_dir (MenuTree       *tree,
                const char     *dirname,
                MenuLayoutNode *where)
{
  GDir       *dir;
  const char *menu_file;

  menu_verbose ("Loading merge dir \"%s\"\n", dirname);

  if ((dir = g_dir_open (dirname, 0, NULL)) == NULL)
    return;

  while ((menu_file = g_dir_read_name (dir)))
    {
      if (g_str_has_suffix (menu_file, ".menu"))
        {
          char *full_path;

          full_path = g_build_filename (dirname, menu_file, NULL);

          load_merge_file (tree, full_path, TRUE, where);

          g_free (full_path);
        }
    }

  g_dir_close (dir);
}

static void
load_merge_dir_with_config_dir (MenuTree       *tree,
                                const char     *config_dir,
                                const char     *dirname,
                                MenuLayoutNode *where)
{
  char *path;

  path = g_build_filename (config_dir, "menus", dirname, NULL);

  load_merge_dir (tree, path, where);

  g_free (path);
}

static void
resolve_merge_file (MenuTree         *tree,
                    MenuLayoutNode   *layout)
{
  char *filename;

  filename = menu_layout_node_get_content_as_path (layout);
  if (filename == NULL)
    {
      menu_verbose ("didn't get node content as a path, not merging file\n");
    }
  else
    {
      load_merge_file (tree, filename, FALSE, layout);

      g_free (filename);
    }

  /* remove the now-replaced node */
  menu_layout_node_unlink (layout);
}

static void
resolve_merge_dir (MenuTree       *tree,
                   MenuLayoutNode *layout)
{
  char *path;

  path = menu_layout_node_get_content_as_path (layout);
  if (path == NULL)
    {
      menu_verbose ("didn't get layout node content as a path, not merging dir\n");
    }
  else
    {
      load_merge_dir (tree, path, layout);

      g_free (path);
    }

  /* remove the now-replaced node */
  menu_layout_node_unlink (layout);
}

static void
add_app_dir (MenuTree       *tree,
             MenuLayoutNode *layout,
             const char     *data_dir)
{
  MenuLayoutNode *tmp;
  char           *dirname;

  tmp = menu_layout_node_new (MENU_LAYOUT_NODE_APP_DIR);
  dirname = g_build_filename (data_dir, "applications", NULL);
  menu_layout_node_set_content (tmp, dirname);
  menu_layout_node_insert_before (layout, tmp);
  menu_layout_node_unref (tmp);

  menu_verbose ("Adding <AppDir>%s</AppDir> in <DefaultAppDirs/>\n",
                dirname);

  g_free (dirname);
}

static void
resolve_default_app_dirs (MenuTree       *tree,
                          MenuLayoutNode *layout)
{
  const char * const *system_data_dirs;
  int                i;

  system_data_dirs = g_get_system_data_dirs ();

  i = 0;
  while (system_data_dirs[i] != NULL)
    {
      add_app_dir (tree, layout, system_data_dirs[i]);

      ++i;
    }

  add_app_dir (tree, layout, g_get_user_data_dir ());

  /* remove the now-replaced node */
  menu_layout_node_unlink (layout);
}

static void
add_directory_dir (MenuTree       *tree,
                   MenuLayoutNode *layout,
                   const char     *data_dir)
{
  MenuLayoutNode *tmp;
  char           *dirname;

  tmp = menu_layout_node_new (MENU_LAYOUT_NODE_DIRECTORY_DIR);
  dirname = g_build_filename (data_dir, "desktop-directories", NULL);
  menu_layout_node_set_content (tmp, dirname);
  menu_layout_node_insert_before (layout, tmp);
  menu_layout_node_unref (tmp);

  menu_verbose ("Adding <DirectoryDir>%s</DirectoryDir> in <DefaultDirectoryDirs/>\n",
                dirname);

  g_free (dirname);
}

static void
resolve_default_directory_dirs (MenuTree       *tree,
                                MenuLayoutNode *layout)
{
  const char * const *system_data_dirs;
  int          i;

  system_data_dirs = g_get_system_data_dirs ();

  i = 0;
  while (system_data_dirs[i] != NULL)
    {
      add_directory_dir (tree, layout, system_data_dirs[i]);

      ++i;
    }

  add_directory_dir (tree, layout, g_get_user_data_dir ());

  /* remove the now-replaced node */
  menu_layout_node_unlink (layout);
}

static void
resolve_default_merge_dirs (MenuTree       *tree,
                            MenuLayoutNode *layout)
{
  MenuLayoutNode     *root;
  const char         *menu_name;
  char               *merge_name;
  const char * const *system_config_dirs;
  int                 i;

  root = menu_layout_node_get_root (layout);
  menu_name = menu_layout_node_root_get_name (root);

  merge_name = g_strconcat (menu_name, "-merged", NULL);

  load_merge_dir_with_config_dir (tree,
                                  g_get_user_config_dir (),
                                  merge_name,
                                  layout);

  system_config_dirs = g_get_system_config_dirs ();

  i = 0;
  while (system_config_dirs[i] != NULL)
    {
      load_merge_dir_with_config_dir (tree,
                                      system_config_dirs[i],
                                      merge_name,
                                      layout);

      ++i;
    }

  g_free (merge_name);

  /* remove the now-replaced node */
  menu_layout_node_unlink (layout);
}

static void
add_filename_include (const char     *desktop_file_id,
                      DesktopEntry   *entry,
                      MenuLayoutNode *include)
{
  if (!desktop_entry_has_categories (entry))
    {
      MenuLayoutNode *node;

      node = menu_layout_node_new (MENU_LAYOUT_NODE_FILENAME);
      menu_layout_node_set_content (node, desktop_file_id);

      menu_layout_node_append_child (include, node);
      menu_layout_node_unref (node);
    }
}

static gboolean
add_menu_for_legacy_dir (MenuLayoutNode *parent,
                         const char     *legacy_dir,
                	 const char     *relative_path,
                         const char     *legacy_prefix,
                         const char     *menu_name)
{
  EntryDirectory  *ed;
  DesktopEntrySet *desktop_entries;
  GSList          *subdirs;
  gboolean         menu_added;

  ed = entry_directory_new_legacy (DESKTOP_ENTRY_INVALID, legacy_dir, legacy_prefix);
  if (!ed)
    return FALSE;

  subdirs = NULL;
  desktop_entries = desktop_entry_set_new ();
  entry_directory_get_flat_contents (ed,
                                     desktop_entries,
                                     NULL,
                                     &subdirs);
  entry_directory_unref (ed);

  menu_added = FALSE;
  if (desktop_entry_set_get_count (desktop_entries) > 0 || subdirs)
    {
      MenuLayoutNode *menu;
      MenuLayoutNode *node;
      GString        *subdir_path;
      GString        *subdir_relative;
      GSList         *tmp;
      int             legacy_dir_len;
      int             relative_path_len;

      menu = menu_layout_node_new (MENU_LAYOUT_NODE_MENU);
      menu_layout_node_append_child (parent, menu);

      menu_added = TRUE;

      g_assert (menu_name != NULL);

      node = menu_layout_node_new (MENU_LAYOUT_NODE_NAME);
      menu_layout_node_set_content (node, menu_name);
      menu_layout_node_append_child (menu, node);
      menu_layout_node_unref (node);

      node = menu_layout_node_new (MENU_LAYOUT_NODE_DIRECTORY);
      if (relative_path != NULL)
        {
          char *directory_entry_path;

          directory_entry_path = g_strdup_printf ("%s/.directory", relative_path);
          menu_layout_node_set_content (node, directory_entry_path);
          g_free (directory_entry_path);
        }
      else
        {
          menu_layout_node_set_content (node, ".directory");
        }
      menu_layout_node_append_child (menu, node);
      menu_layout_node_unref (node);

      if (desktop_entry_set_get_count (desktop_entries) > 0)
	{
	  MenuLayoutNode *include;

	  include = menu_layout_node_new (MENU_LAYOUT_NODE_INCLUDE);
	  menu_layout_node_append_child (menu, include);

	  desktop_entry_set_foreach (desktop_entries,
				     (DesktopEntrySetForeachFunc) add_filename_include,
				     include);

	  menu_layout_node_unref (include);
	}

      subdir_path = g_string_new (legacy_dir);
      legacy_dir_len = strlen (legacy_dir);

      subdir_relative = g_string_new (relative_path);
      relative_path_len = relative_path ? strlen (relative_path) : 0;

      tmp = subdirs;
      while (tmp != NULL)
        {
          const char *subdir = tmp->data;

          g_string_append_c (subdir_path, '/');
          g_string_append (subdir_path, subdir);

	  if (relative_path_len)
	    {
	      g_string_append_c (subdir_relative, '/');
	    }
          g_string_append (subdir_relative, subdir);

          add_menu_for_legacy_dir (menu,
                                   subdir_path->str,
				   subdir_relative->str,
                                   legacy_prefix,
                                   subdir);

          g_string_truncate (subdir_relative, relative_path_len);
          g_string_truncate (subdir_path, legacy_dir_len);

          tmp = tmp->next;
        }

      g_string_free (subdir_path, TRUE);

      menu_layout_node_unref (menu);
    }

  desktop_entry_set_unref (desktop_entries);

  g_slist_foreach (subdirs, (GFunc) g_free, NULL);
  g_slist_free (subdirs);

  return menu_added;
}

static void
resolve_legacy_dir (MenuTree       *tree,
                    MenuLayoutNode *legacy)
{
  MenuLayoutNode *to_merge;
  MenuLayoutNode *menu;

  to_merge = menu_layout_node_new (MENU_LAYOUT_NODE_ROOT);

  menu = menu_layout_node_get_parent (legacy);
  g_assert (menu_layout_node_get_type (menu) == MENU_LAYOUT_NODE_MENU);

  if (add_menu_for_legacy_dir (to_merge,
                               menu_layout_node_get_content (legacy),
			       NULL,
                               menu_layout_node_legacy_dir_get_prefix (legacy),
                               menu_layout_node_menu_get_name (menu)))
    {
      merge_resolved_children (tree, legacy, to_merge);
    }

  menu_layout_node_unref (to_merge);
}

static void
add_legacy_dir (MenuTree       *tree,
                MenuLayoutNode *layout,
                const char     *data_dir)
{
  MenuLayoutNode *legacy;
  char           *dirname;

  dirname = g_build_filename (data_dir, "applnk", NULL);

  legacy = menu_layout_node_new (MENU_LAYOUT_NODE_LEGACY_DIR);
  menu_layout_node_set_content (legacy, dirname);
  menu_layout_node_legacy_dir_set_prefix (legacy, "kde");
  menu_layout_node_insert_before (layout, legacy);

  menu_verbose ("Adding <LegacyDir>%s</LegacyDir> in <KDELegacyDirs/>\n",
                dirname);

  resolve_legacy_dir (tree, legacy);

  menu_layout_node_unref (legacy);

  g_free (dirname);
}

static void
resolve_kde_legacy_dirs (MenuTree       *tree,
                         MenuLayoutNode *layout)
{
  const char * const *system_data_dirs;
  int                 i;

  system_data_dirs = g_get_system_data_dirs ();

  i = 0;
  while (system_data_dirs[i] != NULL)
    {
      add_legacy_dir (tree, layout, system_data_dirs[i]);

      ++i;
    }

  add_legacy_dir (tree, layout, g_get_user_data_dir ());

  /* remove the now-replaced node */
  menu_layout_node_unlink (layout);
}

/* FIXME
 * if someone does <MergeFile>A.menu</MergeFile> inside
 * A.menu, or a more elaborate loop involving multiple
 * files, we'll just get really hosed and eat all the RAM
 * we can find
 */
static void
menu_tree_resolve_files (MenuTree       *tree,
                         MenuLayoutNode *layout)
{
  MenuLayoutNode *child;

  menu_verbose ("Resolving files in: ");
  menu_debug_print_layout (layout, TRUE);

  switch (menu_layout_node_get_type (layout))
    {
    case MENU_LAYOUT_NODE_MERGE_FILE:
      resolve_merge_file (tree, layout);
      break;

    case MENU_LAYOUT_NODE_MERGE_DIR:
      resolve_merge_dir (tree, layout);
      break;

    case MENU_LAYOUT_NODE_DEFAULT_APP_DIRS:
      resolve_default_app_dirs (tree, layout);
      break;

    case MENU_LAYOUT_NODE_DEFAULT_DIRECTORY_DIRS:
      resolve_default_directory_dirs (tree, layout);
      break;

    case MENU_LAYOUT_NODE_DEFAULT_MERGE_DIRS:
      resolve_default_merge_dirs (tree, layout);
      break;

    case MENU_LAYOUT_NODE_LEGACY_DIR:
      resolve_legacy_dir (tree, layout);
      break;

    case MENU_LAYOUT_NODE_KDE_LEGACY_DIRS:
      resolve_kde_legacy_dirs (tree, layout);
      break;

    case MENU_LAYOUT_NODE_PASSTHROUGH:
    case MENU_LAYOUT_NODE_LAYOUT:
    case MENU_LAYOUT_NODE_DEFAULT_LAYOUT:
      /* Just get rid of these, we don't need the memory usage */
      menu_layout_node_unlink (layout);
      break;

    default:
      /* Recurse */
      child = menu_layout_node_get_children (layout);
      while (child != NULL)
        {
          MenuLayoutNode *next = menu_layout_node_get_next (child);

          menu_tree_resolve_files (tree, child);

          child = next;
        }
      break;
    }
}

static void
move_children (MenuLayoutNode *from,
               MenuLayoutNode *to)
{
  MenuLayoutNode *from_child;
  MenuLayoutNode *insert_before;

  insert_before = menu_layout_node_get_children (to);
  from_child    = menu_layout_node_get_children (from);

  while (from_child != NULL)
    {
      MenuLayoutNode *next;

      next = menu_layout_node_get_next (from_child);

      menu_layout_node_steal (from_child);

      if (menu_layout_node_get_type (from_child) == MENU_LAYOUT_NODE_NAME)
        {
          ; /* just drop the Name in the old <Menu> */
        }
      else if (insert_before)
        {
          menu_layout_node_insert_before (insert_before, from_child);
          g_assert (menu_layout_node_get_next (from_child) == insert_before);
          insert_before = from_child;
        }
      else
        {
          menu_layout_node_prepend_child (to, from_child);
          g_assert (menu_layout_node_get_children (to) == from_child);
          insert_before = from_child;
        }

      menu_layout_node_unref (from_child);

      from_child = next;
    }
}

static int
null_safe_strcmp (const char *a,
                  const char *b)
{
  if (a == NULL && b == NULL)
    return 0;
  else if (a == NULL)
    return -1;
  else if (b == NULL)
    return 1;
  else
    return strcmp (a, b);
}

static int
node_compare_func (const void *a,
                   const void *b)
{
  MenuLayoutNode *node_a = (MenuLayoutNode*) a;
  MenuLayoutNode *node_b = (MenuLayoutNode*) b;
  MenuLayoutNodeType t_a = menu_layout_node_get_type (node_a);
  MenuLayoutNodeType t_b = menu_layout_node_get_type (node_b);

  if (t_a < t_b)
    return -1;
  else if (t_a > t_b)
    return 1;
  else
    {
      const char *c_a = menu_layout_node_get_content (node_a);
      const char *c_b = menu_layout_node_get_content (node_b);

      return null_safe_strcmp (c_a, c_b);
    }
}

static int
node_menu_compare_func (const void *a,
                        const void *b)
{
  MenuLayoutNode *node_a = (MenuLayoutNode*) a;
  MenuLayoutNode *node_b = (MenuLayoutNode*) b;
  MenuLayoutNode *parent_a = menu_layout_node_get_parent (node_a);
  MenuLayoutNode *parent_b = menu_layout_node_get_parent (node_b);

  if (parent_a < parent_b)
    return -1;
  else if (parent_a > parent_b)
    return 1;
  else
    return null_safe_strcmp (menu_layout_node_menu_get_name (node_a),
                             menu_layout_node_menu_get_name (node_b));
}

/* Sort to remove move nodes with the same "old" field */
static int
node_move_compare_func (const void *a,
                        const void *b)
{
  MenuLayoutNode *node_a = (MenuLayoutNode*) a;
  MenuLayoutNode *node_b = (MenuLayoutNode*) b;
  MenuLayoutNode *parent_a = menu_layout_node_get_parent (node_a);
  MenuLayoutNode *parent_b = menu_layout_node_get_parent (node_b);

  if (parent_a < parent_b)
    return -1;
  else if (parent_a > parent_b)
    return 1;
  else
    return null_safe_strcmp (menu_layout_node_move_get_old (node_a),
                             menu_layout_node_move_get_old (node_b));
}

static void
menu_tree_strip_duplicate_children (MenuTree       *tree,
                                    MenuLayoutNode *layout)
{
  MenuLayoutNode *child;
  GSList         *simple_nodes;
  GSList         *menu_layout_nodes;
  GSList         *move_nodes;
  GSList         *prev;
  GSList         *tmp;

  /* to strip dups, we find all the child nodes where
   * we want to kill dups, sort them,
   * then nuke the adjacent nodes that are equal
   */

  simple_nodes = NULL;
  menu_layout_nodes = NULL;
  move_nodes = NULL;

  child = menu_layout_node_get_children (layout);
  while (child != NULL)
    {
      switch (menu_layout_node_get_type (child))
        {
          /* These are dups if their content is the same */
        case MENU_LAYOUT_NODE_APP_DIR:
        case MENU_LAYOUT_NODE_DIRECTORY_DIR:
        case MENU_LAYOUT_NODE_DIRECTORY:
          simple_nodes = g_slist_prepend (simple_nodes, child);
          break;

          /* These have to be merged in a more complicated way,
           * and then recursed
           */
        case MENU_LAYOUT_NODE_MENU:
          menu_layout_nodes = g_slist_prepend (menu_layout_nodes, child);
          break;

          /* These have to be merged in a different more complicated way */
        case MENU_LAYOUT_NODE_MOVE:
          move_nodes = g_slist_prepend (move_nodes, child);
          break;

        default:
          break;
        }

      child = menu_layout_node_get_next (child);
    }

  /* Note that the lists are all backward. So we want to keep
   * the items that are earlier in the list, because they were
   * later in the file
   */

  /* stable sort the simple nodes */
  simple_nodes = g_slist_sort (simple_nodes,
                               node_compare_func);

  prev = NULL;
  tmp = simple_nodes;
  while (tmp != NULL)
    {
      if (prev)
        {
          MenuLayoutNode *p = prev->data;
          MenuLayoutNode *n = tmp->data;

          if (node_compare_func (p, n) == 0)
            {
              /* nuke it! */
              menu_layout_node_unlink (n);
            }
        }

      prev = tmp;
      tmp = tmp->next;
    }

  g_slist_free (simple_nodes);
  simple_nodes = NULL;

  /* stable sort the menu nodes (the sort includes the
   * parents of the nodes in the comparison). Remember
   * the list is backward.
   */
  menu_layout_nodes = g_slist_sort (menu_layout_nodes,
                             node_menu_compare_func);

  prev = NULL;
  tmp = menu_layout_nodes;
  while (tmp != NULL)
    {
      if (prev)
        {
          MenuLayoutNode *p = prev->data;
          MenuLayoutNode *n = tmp->data;

          if (node_menu_compare_func (p, n) == 0)
            {
              /* Move children of first menu to the start of second
               * menu and nuke the first menu
               */
              move_children (n, p);
              menu_layout_node_unlink (n);
            }
        }

      prev = tmp;
      tmp = tmp->next;
    }

  g_slist_free (menu_layout_nodes);
  menu_layout_nodes = NULL;

  /* Remove duplicate <Move> nodes */

  if (move_nodes != NULL)
    {
      /* stable sort the move nodes by <Old> (the sort includes the
       * parents of the nodes in the comparison)
       */
      move_nodes = g_slist_sort (move_nodes,
                                 node_move_compare_func);

      prev = NULL;
      tmp = move_nodes;
      while (tmp != NULL)
        {
          if (prev)
            {
              MenuLayoutNode *p = prev->data;
              MenuLayoutNode *n = tmp->data;

              if (node_move_compare_func (p, n) == 0)
                {
                  /* Same <Old>, so delete the first one */
                  menu_verbose ("Removing duplicate move old = %s new = %s leaving old = %s new = %s\n",
                                menu_layout_node_move_get_old (n),
                                menu_layout_node_move_get_new (n),
                                menu_layout_node_move_get_old (p),
                                menu_layout_node_move_get_new (p));
                  menu_layout_node_unlink (n);
                }
            }

          prev = tmp;
          tmp = tmp->next;
        }

      g_slist_free (move_nodes);
      move_nodes = NULL;
    }

  /* Recursively clean up all children */
  child = menu_layout_node_get_children (layout);
  while (child != NULL)
    {
      if (menu_layout_node_get_type (child) == MENU_LAYOUT_NODE_MENU)
        menu_tree_strip_duplicate_children (tree, child);

      child = menu_layout_node_get_next (child);
    }
}

static MenuLayoutNode *
find_submenu (MenuLayoutNode *layout,
              const char     *path,
              gboolean        create_if_not_found)
{
  MenuLayoutNode *child;
  const char     *slash;
  const char     *next_path;
  char           *name;

  menu_verbose (" (splitting \"%s\")\n", path);

  if (path[0] == '\0' || path[0] == '/')
    return NULL;

  slash = strchr (path, '/');
  if (slash != NULL)
    {
      name = g_strndup (path, slash - path);
      next_path = slash + 1;
      if (*next_path == '\0')
        next_path = NULL;
    }
  else
    {
      name = g_strdup (path);
      next_path = NULL;
    }

  child = menu_layout_node_get_children (layout);
  while (child != NULL)
    {
      switch (menu_layout_node_get_type (child))
        {
        case MENU_LAYOUT_NODE_MENU:
          {
            if (strcmp (name, menu_layout_node_menu_get_name (child)) == 0)
              {
                menu_verbose ("MenuNode %p found for path component \"%s\"\n",
                              child, name);

                g_free (name);

                if (!next_path)
                  {
                    menu_verbose (" Found menu node %p parent is %p\n",
                                  child, layout);
                    return child;
                  }

                return find_submenu (child, next_path, create_if_not_found);
              }
          }
          break;

        default:
          break;
        }

      child = menu_layout_node_get_next (child);
    }

  if (create_if_not_found)
    {
      MenuLayoutNode *name_node;

      child = menu_layout_node_new (MENU_LAYOUT_NODE_MENU);
      menu_layout_node_append_child (layout, child);

      name_node = menu_layout_node_new (MENU_LAYOUT_NODE_NAME);
      menu_layout_node_set_content (name_node, name);
      menu_layout_node_append_child (child, name_node);
      menu_layout_node_unref (name_node);

      menu_verbose (" Created menu node %p parent is %p\n",
                    child, layout);

      menu_layout_node_unref (child);
      g_free (name);

      if (!next_path)
        return child;

      return find_submenu (child, next_path, create_if_not_found);
    }
  else
    {
      g_free (name);
      return NULL;
    }
}

/* To call this you first have to strip duplicate children once,
 * otherwise when you move a menu Foo to Bar then you may only
 * move one of Foo, not all the merged Foo.
 */
static void
menu_tree_execute_moves (MenuTree       *tree,
                         MenuLayoutNode *layout,
                         gboolean       *need_remove_dups_p)
{
  MenuLayoutNode *child;
  gboolean        need_remove_dups;
  GSList         *move_nodes;
  GSList         *prev;
  GSList         *tmp;

  need_remove_dups = FALSE;

  move_nodes = NULL;

  child = menu_layout_node_get_children (layout);
  while (child != NULL)
    {
      switch (menu_layout_node_get_type (child))
        {
        case MENU_LAYOUT_NODE_MENU:
          /* Recurse - we recurse first and process the current node
           * second, as the spec dictates.
           */
          menu_tree_execute_moves (tree, child, &need_remove_dups);
          break;

        case MENU_LAYOUT_NODE_MOVE:
          move_nodes = g_slist_prepend (move_nodes, child);
          break;

        default:
          break;
        }

      child = menu_layout_node_get_next (child);
    }

  prev = NULL;
  tmp = move_nodes;
  while (tmp != NULL)
    {
      MenuLayoutNode *move_node = tmp->data;
      MenuLayoutNode *old_node;
      GSList         *next = tmp->next;
      const char     *old;
      const char     *new;

      old = menu_layout_node_move_get_old (move_node);
      new = menu_layout_node_move_get_new (move_node);
      g_assert (old != NULL && new != NULL);

      menu_verbose ("executing <Move> old = \"%s\" new = \"%s\"\n",
                    old, new);

      old_node = find_submenu (layout, old, FALSE);
      if (old_node != NULL)
        {
          MenuLayoutNode *new_node;

          /* here we can create duplicates anywhere below the
           * node
           */
          need_remove_dups = TRUE;

          /* look up new node creating it and its parents if
           * required
           */
          new_node = find_submenu (layout, new, TRUE);
          g_assert (new_node != NULL);

          move_children (old_node, new_node);

          menu_layout_node_unlink (old_node);
        }

      menu_layout_node_unlink (move_node);

      tmp = next;
    }

  g_slist_free (move_nodes);

  /* This oddness is to ensure we only remove dups once,
   * at the root, instead of recursing the tree over
   * and over.
   */
  if (need_remove_dups_p)
    *need_remove_dups_p = need_remove_dups;
  else if (need_remove_dups)
    menu_tree_strip_duplicate_children (tree, layout);
}

static void
menu_tree_load_layout (MenuTree *tree)
{
  GError *error;

  if (tree->layout)
    return;

  if (!menu_tree_canonicalize_path (tree))
    return;

  menu_verbose ("Loading menu layout from \"%s\"\n",
                tree->canonical_path);

  error = NULL;
  tree->layout = menu_layout_load (tree->canonical_path, &error);
  if (tree->layout == NULL)
    {
      g_warning ("Error loading menu layout from \"%s\": %s",
                 tree->canonical_path, error->message);
      g_error_free (error);
      return;
    }

  menu_tree_resolve_files (tree, tree->layout);
  menu_tree_strip_duplicate_children (tree, tree->layout);
  menu_tree_execute_moves (tree, tree->layout, NULL);
}

static void
menu_tree_force_reload (MenuTree *tree)
{
  menu_tree_force_rebuild (tree);

  if (tree->layout)
    menu_layout_node_unref (tree->layout);
  tree->layout = NULL;
}

static DesktopEntrySet *
process_include_rules (MenuLayoutNode     *layout,
                       EntryDirectoryList *list)
{
  DesktopEntrySet *set = NULL;

  switch (menu_layout_node_get_type (layout))
    {
    case MENU_LAYOUT_NODE_AND:
      {
        MenuLayoutNode *child;

	menu_verbose ("Processing <And>\n");

        child = menu_layout_node_get_children (layout);
        while (child != NULL)
          {
            DesktopEntrySet *child_set;

            child_set = process_include_rules (child, list);

            if (set == NULL)
              {
                set = child_set;
              }
            else
              {
                desktop_entry_set_intersection (set, child_set);
                desktop_entry_set_unref (child_set);
              }

            /* as soon as we get empty results, we can bail,
             * because it's an AND
             */
            if (desktop_entry_set_get_count (set) == 0)
              break;

            child = menu_layout_node_get_next (child);
          }
	menu_verbose ("Processed <And>\n");
      }
      break;

    case MENU_LAYOUT_NODE_OR:
      {
        MenuLayoutNode *child;

	menu_verbose ("Processing <Or>\n");

        child = menu_layout_node_get_children (layout);
        while (child != NULL)
          {
            DesktopEntrySet *child_set;

            child_set = process_include_rules (child, list);

            if (set == NULL)
              {
                set = child_set;
              }
            else
              {
                desktop_entry_set_union (set, child_set);
                desktop_entry_set_unref (child_set);
              }

            child = menu_layout_node_get_next (child);
          }
	menu_verbose ("Processed <Or>\n");
      }
      break;

    case MENU_LAYOUT_NODE_NOT:
      {
        /* First get the OR of all the rules */
        MenuLayoutNode *child;

	menu_verbose ("Processing <Not>\n");

        child = menu_layout_node_get_children (layout);
        while (child != NULL)
          {
            DesktopEntrySet *child_set;

            child_set = process_include_rules (child, list);

            if (set == NULL)
              {
                set = child_set;
              }
            else
              {
                desktop_entry_set_union (set, child_set);
                desktop_entry_set_unref (child_set);
              }

            child = menu_layout_node_get_next (child);
          }

        if (set != NULL)
          {
            /* Now invert the result */
            entry_directory_list_invert_set (list, set);
          }
	menu_verbose ("Processed <Not>\n");
      }
      break;

    case MENU_LAYOUT_NODE_ALL:
      menu_verbose ("Processing <All>\n");
      set = desktop_entry_set_new ();
      entry_directory_list_get_all_desktops (list, set);
      menu_verbose ("Processed <All>\n");
      break;

    case MENU_LAYOUT_NODE_FILENAME:
      {
        DesktopEntry *entry;

	menu_verbose ("Processing <Filename>%s</Filename>\n",
		      menu_layout_node_get_content (layout));

        entry = entry_directory_list_get_desktop (list,
                                                  menu_layout_node_get_content (layout));
        if (entry != NULL)
          {
            set = desktop_entry_set_new ();
            desktop_entry_set_add_entry (set,
                                         entry,
                                         menu_layout_node_get_content (layout));
            desktop_entry_unref (entry);
          }
	menu_verbose ("Processed <Filename>%s</Filename>\n",
		      menu_layout_node_get_content (layout));
      }
      break;

    case MENU_LAYOUT_NODE_CATEGORY:
      menu_verbose ("Processing <Category>%s</Category>\n",
		    menu_layout_node_get_content (layout));
      set = desktop_entry_set_new ();
      entry_directory_list_get_by_category (list,
                                            menu_layout_node_get_content (layout),
                                            set);
      menu_verbose ("Processed <Category>%s</Category>\n",
		    menu_layout_node_get_content (layout));
      break;

    default:
      break;
    }

  if (set == NULL)
    set = desktop_entry_set_new (); /* create an empty set */

  menu_verbose ("Matched %d entries\n", desktop_entry_set_get_count (set));

  return set;
}

static void
entries_listify_foreach (const char        *desktop_file_id,
                         DesktopEntry      *desktop_entry,
                         MenuTreeDirectory *directory)
{
  MenuTreeEntry *entry;

  entry = menu_tree_entry_new (directory, desktop_entry, desktop_file_id);

  directory->entries = g_slist_prepend (directory->entries,
                                        entry);
}

static MenuTreeDirectory *
process_layout (MenuTree          *tree,
                MenuTreeDirectory *parent,
                MenuLayoutNode    *layout,
                DesktopEntrySet   *allocated)
{
  EntryDirectoryList *app_dirs;
  EntryDirectoryList *dir_dirs;
  MenuLayoutNode     *layout_iter;
  MenuTreeDirectory  *directory;
  DesktopEntrySet    *entries;
  gboolean            deleted;
  DesktopEntrySet    *allocated_set;
  gboolean            only_unallocated;
  GSList             *tmp;

  g_assert (menu_layout_node_get_type (layout) == MENU_LAYOUT_NODE_MENU);
  g_assert (menu_layout_node_menu_get_name (layout) != NULL);

  directory = menu_tree_directory_new (parent,
                                       menu_layout_node_menu_get_name (layout));

  menu_verbose ("=== Menu name = %s ===\n", directory->name);


  deleted = FALSE;
  only_unallocated = FALSE;

  app_dirs = menu_layout_node_menu_get_app_dirs (layout);
  dir_dirs = menu_layout_node_menu_get_directory_dirs (layout);

  entries = desktop_entry_set_new ();
  allocated_set = desktop_entry_set_new ();

  layout_iter = menu_layout_node_get_children (layout);
  while (layout_iter != NULL)
    {
      switch (menu_layout_node_get_type (layout_iter))
        {
        case MENU_LAYOUT_NODE_MENU:
          /* recurse */
          {
            MenuTreeDirectory *child_dir;

	    menu_verbose ("Processing <Menu>\n");

            child_dir = process_layout (tree,
                                        directory,
                                        layout_iter,
                                        allocated);
            if (child_dir)
              directory->subdirs = g_slist_prepend (directory->subdirs,
                                                    child_dir);

	    menu_verbose ("Processed <Menu>\n");
          }
          break;

        case MENU_LAYOUT_NODE_INCLUDE:
          {
            /* The match rule children of the <Include> are
             * independent (logical OR) so we can process each one by
             * itself
             */
            MenuLayoutNode *rule;

	    menu_verbose ("Processing <Include> (%d entries)\n",
			  desktop_entry_set_get_count (entries));

            rule = menu_layout_node_get_children (layout_iter);
            while (rule != NULL)
              {
                DesktopEntrySet *rule_set;

                rule_set = process_include_rules (rule, app_dirs);
                if (rule_set != NULL)
                  {
                    desktop_entry_set_union (entries, rule_set);
                    desktop_entry_set_union (allocated_set, rule_set);
                    desktop_entry_set_unref (rule_set);
                  }

                rule = menu_layout_node_get_next (rule);
              }

	    menu_verbose ("Processed <Include> (%d entries)\n",
			  desktop_entry_set_get_count (entries));
          }
          break;

        case MENU_LAYOUT_NODE_EXCLUDE:
          {
            /* The match rule children of the <Exclude> are
             * independent (logical OR) so we can process each one by
             * itself
             */
            MenuLayoutNode *rule;

	    menu_verbose ("Processing <Exclude> (%d entries)\n",
			  desktop_entry_set_get_count (entries));

            rule = menu_layout_node_get_children (layout_iter);
            while (rule != NULL)
              {
                DesktopEntrySet *rule_set;

                rule_set = process_include_rules (rule, app_dirs);
                if (rule_set != NULL)
                  {
                    desktop_entry_set_subtract (entries, rule_set);
                    desktop_entry_set_unref (rule_set);
                  }

                rule = menu_layout_node_get_next (rule);
              }

	    menu_verbose ("Processed <Exclude> (%d entries)\n",
			  desktop_entry_set_get_count (entries));
          }
          break;

        case MENU_LAYOUT_NODE_DIRECTORY:
          {
            DesktopEntry *entry;

	    menu_verbose ("Processing <Directory>%s</Directory>\n",
			  menu_layout_node_get_content (layout_iter));

	    /*
             * The last <Directory> to exist wins, so we always try overwriting
             */
            entry = entry_directory_list_get_directory (dir_dirs,
                                                        menu_layout_node_get_content (layout_iter));

            if (entry != NULL)
              {
                if (!desktop_entry_get_hidden (entry))
                  {
                    if (directory->directory_entry)
                      desktop_entry_unref (directory->directory_entry);
                    directory->directory_entry = entry; /* pass ref ownership */
                  }
                else
                  {
                    desktop_entry_unref (entry);
                  }
              }

            menu_verbose ("Processed <Directory> new directory entry = %p\n",
                          directory->directory_entry);
          }
          break;

        case MENU_LAYOUT_NODE_DELETED:
	  menu_verbose ("Processed <Deleted/>\n");
          deleted = TRUE;
          break;

        case MENU_LAYOUT_NODE_NOT_DELETED:
	  menu_verbose ("Processed <NotDeleted/>\n");
          deleted = FALSE;
          break;

        case MENU_LAYOUT_NODE_ONLY_UNALLOCATED:
	  menu_verbose ("Processed <OnlyUnallocated/>\n");
          only_unallocated = TRUE;
          break;

        case MENU_LAYOUT_NODE_NOT_ONLY_UNALLOCATED:
	  menu_verbose ("Processed <NotOnlyUnallocated/>\n");
          only_unallocated = FALSE;
          break;

        default:
          break;
        }

      layout_iter = menu_layout_node_get_next (layout_iter);
    }

  directory->only_unallocated = only_unallocated;

  if (!directory->only_unallocated)
    desktop_entry_set_union (allocated, allocated_set);

  desktop_entry_set_unref (allocated_set);

  if (directory->directory_entry)
    {
      if (desktop_entry_get_no_display (directory->directory_entry))
        {
          menu_verbose ("Not showing menu %s because NoDisplay=true\n",
                        desktop_entry_get_name (directory->directory_entry));
          deleted = TRUE;
        }

      if (!desktop_entry_get_show_in_gnome (directory->directory_entry))
        {
          menu_verbose ("Not showing menu %s because OnlyShowIn!=GNOME or NotShowIn=GNOME\n",
                        desktop_entry_get_name (directory->directory_entry));
          deleted = TRUE;
        }
    }

  if (deleted)
    {
      desktop_entry_set_unref (entries);
      menu_tree_directory_unref (directory);
      return NULL;
    }

  directory->entries = NULL;
  desktop_entry_set_foreach (entries,
                             (DesktopEntrySetForeachFunc) entries_listify_foreach,
                             directory);
  desktop_entry_set_unref (entries);

  tmp = directory->entries;
  while (tmp != NULL)
    {
      MenuTreeEntry *entry = tmp->data;
      GSList        *next  = tmp->next;
      gboolean       delete = FALSE;

      if (desktop_entry_get_hidden (entry->desktop_entry))
        {
          menu_verbose ("Deleting %s because Hidden=true\n",
                        desktop_entry_get_name (entry->desktop_entry));
          delete = TRUE;
        }

      if (desktop_entry_get_no_display (entry->desktop_entry))
        {
          menu_verbose ("Deleting %s because NoDisplay=true\n",
                        desktop_entry_get_name (entry->desktop_entry));
          delete = TRUE;
        }

      if (!desktop_entry_get_show_in_gnome (entry->desktop_entry))
        {
          menu_verbose ("Deleting %s because OnlyShowIn!=GNOME or NotShowIn=GNOME\n",
                        desktop_entry_get_name (entry->desktop_entry));
          delete = TRUE;
        }

      if (desktop_entry_get_tryexec_failed (entry->desktop_entry))
        {
          menu_verbose ("Deleting %s because TryExec failed\n",
                        desktop_entry_get_name (entry->desktop_entry));
          delete = TRUE;
        }

      if (delete)
        {
          directory->entries = g_slist_delete_link (directory->entries,
                                                   tmp);
          menu_tree_entry_unref (entry);
        }

      tmp = next;
    }

  g_assert (directory->name != NULL);

  return directory;
}

static void
process_only_unallocated (MenuTreeDirectory *directory,
			  DesktopEntrySet   *allocated)
{
  GSList *tmp;

  /* For any directory marked only_unallocated, we have to remove any
   * entries that were in fact allocated.
   */

  if (directory->only_unallocated)
    {
      tmp = directory->entries;
      while (tmp != NULL)
        {
          MenuTreeEntry *entry = tmp->data;
          GSList        *next  = tmp->next;

          if (desktop_entry_set_lookup (allocated, entry->desktop_file_id))
            {
              directory->entries = g_slist_delete_link (directory->entries,
                                                        tmp);
              menu_tree_entry_unref (entry);
            }

          tmp = next;
        }
    }

  directory->entries = g_slist_sort (directory->entries,
                                     (GCompareFunc) menu_tree_entry_compare);

  tmp = directory->subdirs;
  while (tmp != NULL)
    {
      MenuTreeDirectory *subdir = tmp->data;
      GSList            *next = tmp->next;

      process_only_unallocated (subdir, allocated);

      if (subdir->subdirs == NULL && subdir->entries == NULL)
        {
          directory->subdirs = g_slist_delete_link (directory->subdirs,
                                                    tmp);
          menu_tree_directory_unref (subdir);
        }

      tmp = next;
    }

  directory->subdirs = g_slist_sort (directory->subdirs,
                                     (GCompareFunc) menu_tree_directory_compare);
}

static void
handle_entries_changed (MenuLayoutNode *layout,
                        MenuTree       *tree)
{
  if (tree->layout == layout)
    {
      menu_tree_force_rebuild (tree);
      menu_tree_invoke_monitors (tree);
    }
}

static void
menu_tree_build_from_layout (MenuTree *tree)
{
  DesktopEntrySet *allocated;

  if (tree->root)
    return;

  menu_tree_load_layout (tree);
  if (!tree->layout)
    return;

  menu_verbose ("Building menu tree from layout\n");

  allocated = desktop_entry_set_new ();

  tree->root = process_layout (tree,
                               NULL,
                               find_menu_child (tree->layout),
                               allocated);
  if (tree->root)
    {
      process_only_unallocated (tree->root, allocated);

      menu_layout_node_root_add_entries_monitor (tree->layout,
                                                 (MenuLayoutNodeEntriesChangedFunc) handle_entries_changed,
                                                 tree);
    }

  desktop_entry_set_unref (allocated);
}

static void
menu_tree_force_rebuild (MenuTree *tree)
{
  if (tree->root)
    {
      menu_tree_directory_unref (tree->root);
      tree->root = NULL;

      g_assert (tree->layout != NULL);

      menu_layout_node_root_remove_entries_monitor (tree->layout,
                                                    (MenuLayoutNodeEntriesChangedFunc) handle_entries_changed,
                                                    tree);
    }
}
