/*
 * Copyright (C) 2004 Red Hat, Inc.
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
#include <libgnomevfs/gnome-vfs.h>

static gboolean monitor = FALSE;

static GOptionEntry options[] = {
  { "monitor", 'm', 0, G_OPTION_ARG_NONE, &monitor, "Monitor for menu changes", NULL },
  { NULL }
};

static void
append_directory_path (MenuTreeDirectory *directory,
		       GString           *path)
{
  MenuTreeDirectory *parent;

  parent = menu_tree_directory_get_parent (directory);

  if (!parent)
    {
      g_string_append_c (path, '/');
      return;
    }

  append_directory_path (parent, path);

  g_string_append (path, menu_tree_directory_get_name (directory));
  g_string_append_c (path, '/');

  menu_tree_directory_unref (parent);
}

static char *
make_path (MenuTreeDirectory *directory)
{
  GString *path;

  g_return_val_if_fail (directory != NULL, NULL);

  path = g_string_new (NULL);

  append_directory_path (directory, path);

  return g_string_free (path, FALSE);
}

static void
print_directory (MenuTreeDirectory *directory)
{
  GSList     *entries;
  GSList     *subdirs;
  GSList     *tmp;
  const char *path;
  char       *freeme;

  freeme = make_path (directory);
  if (!strcmp (freeme, "/"))
    path = freeme;
  else
    path = freeme + 1;

  entries = menu_tree_directory_get_entries (directory);
  subdirs = menu_tree_directory_get_subdirs (directory);

  tmp = entries;
  while (tmp != NULL)
    {
      MenuTreeEntry *entry = tmp->data;

      g_print ("%s\t%s\t%s\n",
               path,
               menu_tree_entry_get_desktop_file_id (entry),
               menu_tree_entry_get_desktop_file_path (entry));

      menu_tree_entry_unref (entry);

      tmp = tmp->next;
    }

  g_slist_free (entries);

  tmp = subdirs;
  while (tmp != NULL)
    {
      MenuTreeDirectory *subdir = tmp->data;

      print_directory (subdir);

      menu_tree_directory_unref (subdir);

      tmp = tmp->next;
    }

  g_slist_free (subdirs);

  g_free (freeme);
}

static void
handle_tree_changed (MenuTree *tree)
{
  MenuTreeDirectory *root;

  g_print ("\n\n\n==== Menu changed, reloading ====\n\n\n");

  root = menu_tree_get_root_directory (tree);
  if (root == NULL)
    {
      g_warning ("Menu tree is empty");
      return;
    }

  print_directory (root);
  menu_tree_directory_unref (root);
}

int
main (int argc, char **argv)
{
  GOptionContext    *options_context;
  MenuTree          *tree;
  MenuTreeDirectory *root;

  gnome_vfs_init ();

  options_context = g_option_context_new ("- test GNOME's implementation of the Desktop Menu Specification");
  g_option_context_add_main_entries (options_context, options, GETTEXT_PACKAGE);
  g_option_context_parse (options_context, &argc, &argv, NULL);

  tree = menu_tree_lookup ("applications.menu");
  if (tree == NULL)
    {
      g_warning ("Failed to look up applications.menu");
      gnome_vfs_shutdown ();
      return 0;
    }

  root = menu_tree_get_root_directory (tree);
  if (root != NULL)
    {
      print_directory (root);
      menu_tree_directory_unref (root);
    }
  else
    {
      g_warning ("Menu tree is empty");
    }

  if (monitor)
    {
      GMainLoop *main_loop;

      menu_tree_add_monitor (tree,
			     (MenuTreeChangedFunc) handle_tree_changed,
			     NULL);

      main_loop = g_main_loop_new (NULL, FALSE);
      g_main_loop_run (main_loop);
      g_main_loop_unref (main_loop);

      menu_tree_remove_monitor (tree,
				(MenuTreeChangedFunc) handle_tree_changed,
				NULL);

    }

  menu_tree_unref (tree);

  gnome_vfs_shutdown ();

  return 0;
}
