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

#ifndef __MENU_TREE_H__
#define __MENU_TREE_H__

#ifndef MENU_I_KNOW_THIS_IS_UNSTABLE
#error "libgnome-menu should only be used if you understand that it's subject to frequent change, and is not supported as a fixed API/ABI or as part of the platform"
#endif

#include <glib.h>

G_BEGIN_DECLS

typedef struct MenuTree          MenuTree;
typedef struct MenuTreeEntry     MenuTreeEntry;
typedef struct MenuTreeDirectory MenuTreeDirectory;

typedef void (*MenuTreeChangedFunc) (MenuTree *tree,
                                     gpointer  user_data);

MenuTree *menu_tree_lookup (const char *menu_file);

MenuTree *menu_tree_ref   (MenuTree *tree);
void      menu_tree_unref (MenuTree *tree);

MenuTreeDirectory *menu_tree_get_root_directory (MenuTree *tree);
MenuTreeDirectory *menu_tree_get_directory_from_path (MenuTree   *tree,
						      const char *path);

GSList *menu_tree_directory_get_entries (MenuTreeDirectory *directory);
GSList *menu_tree_directory_get_subdirs (MenuTreeDirectory *directory);

MenuTreeDirectory *menu_tree_directory_ref   (MenuTreeDirectory *directory);
void               menu_tree_directory_unref (MenuTreeDirectory *directory);

MenuTreeDirectory *menu_tree_directory_get_parent (MenuTreeDirectory *directory);

const char *menu_tree_directory_get_name     (MenuTreeDirectory *directory);
const char *menu_tree_directory_get_comment  (MenuTreeDirectory *directory);
const char *menu_tree_directory_get_icon     (MenuTreeDirectory *directory);

char *menu_tree_directory_make_path (MenuTreeDirectory *directory,
				     MenuTreeEntry     *entry);


MenuTreeEntry *menu_tree_entry_ref   (MenuTreeEntry *entry);
void           menu_tree_entry_unref (MenuTreeEntry *entry);

MenuTreeDirectory *menu_tree_entry_get_parent (MenuTreeEntry *entry);

const char *menu_tree_entry_get_name    (MenuTreeEntry *entry);
const char *menu_tree_entry_get_comment (MenuTreeEntry *entry);
const char *menu_tree_entry_get_icon    (MenuTreeEntry *entry);
const char *menu_tree_entry_get_exec    (MenuTreeEntry *entry);

const char *menu_tree_entry_get_desktop_file_path (MenuTreeEntry *entry);
const char *menu_tree_entry_get_desktop_file_id   (MenuTreeEntry *entry);

void menu_tree_add_monitor    (MenuTree            *tree,
                               MenuTreeChangedFunc  callback,
                               gpointer             user_data);
void menu_tree_remove_monitor (MenuTree            *tree,
                               MenuTreeChangedFunc  callback,
                               gpointer             user_data);

G_END_DECLS

#endif /* __MENU_TREE_H__ */
