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
static gboolean include_excluded = FALSE;

static GOptionEntry options[] = {
  { "monitor",          'm', 0, G_OPTION_ARG_NONE, &monitor,          "Monitor for menu changes",   NULL },
  { "include-excluded", 'i', 0, G_OPTION_ARG_NONE, &include_excluded, "Include <Exclude>d entries", NULL },
  { NULL }
};

static void
append_directory_path (MenuTreeDirectory *directory,
		       GString           *path)
{
  MenuTreeDirectory *parent;

  parent = menu_tree_item_get_parent (MENU_TREE_ITEM (directory));

  if (!parent)
    {
      g_string_append_c (path, '/');
      return;
    }

  append_directory_path (parent, path);

  g_string_append (path, menu_tree_directory_get_name (directory));
  g_string_append_c (path, '/');

  menu_tree_item_unref (parent);
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
print_entry (MenuTreeEntry *entry,
	     const char    *path)
{
  g_print ("%s\t%s\t%s %s\n",
	   path,
	   menu_tree_entry_get_desktop_file_id (entry),
	   menu_tree_entry_get_desktop_file_path (entry),
	   menu_tree_entry_get_is_excluded (entry) ? "<excluded>" : "");
}

static void
print_directory (MenuTreeDirectory *directory)
{
  GSList     *items;
  GSList     *tmp;
  const char *path;
  char       *freeme;

  freeme = make_path (directory);
  if (!strcmp (freeme, "/"))
    path = freeme;
  else
    path = freeme + 1;

  items = menu_tree_directory_get_contents (directory);

  tmp = items;
  while (tmp != NULL)
    {
      MenuTreeItem *item = tmp->data;

      switch (menu_tree_item_get_type (item))
	{
	case MENU_TREE_ITEM_ENTRY:
	  print_entry (MENU_TREE_ENTRY (item), path);
	  break;

	case MENU_TREE_ITEM_DIRECTORY:
	  print_directory (MENU_TREE_DIRECTORY (item));
	  break;

	case MENU_TREE_ITEM_HEADER:
	case MENU_TREE_ITEM_SEPARATOR:
	  break;

	case MENU_TREE_ITEM_ALIAS:
	  {
	    MenuTreeItem *aliased_item;

	    aliased_item = menu_tree_alias_get_item (MENU_TREE_ALIAS (item));
	    if (menu_tree_item_get_type (aliased_item) == MENU_TREE_ITEM_ENTRY)
	      print_entry (MENU_TREE_ENTRY (aliased_item), path);
	  }
	  break;

	default:
	  g_assert_not_reached ();
	  break;
	}

      menu_tree_item_unref (tmp->data);

      tmp = tmp->next;
    }

  g_slist_free (items);

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
  menu_tree_item_unref (root);
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

  tree = menu_tree_lookup ("applications.menu",
			   include_excluded ? MENU_TREE_FLAGS_INCLUDE_EXCLUDED : MENU_TREE_FLAGS_NONE);
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
      menu_tree_item_unref (root);
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
