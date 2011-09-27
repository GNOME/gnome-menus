#
# Copyright (C) 2005 Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

import os
import os.path
from gi.repository import GObject
from gi.repository import Gio
from gi.repository import Gtk
from gi.repository import GdkPixbuf
from gi.repository import GMenu

def lookup_system_menu_file (menu_file):
    conf_dirs = None
    if os.environ.has_key ("XDG_CONFIG_DIRS"):
        conf_dirs = os.environ["XDG_CONFIG_DIRS"]
    if not conf_dirs:
        conf_dirs = "/etc/xdg"

    for conf_dir in conf_dirs.split (":"):
        menu_file_path = os.path.join (conf_dir, "menus", menu_file)
        if os.path.isfile (menu_file_path):
            return menu_file_path
    
    return None

class MenuTreeModel (Gtk.TreeStore):
    (
        COLUMN_IS_ENTRY,
        COLUMN_ID,
        COLUMN_NAME,
        COLUMN_ICON,
        COLUMN_MENU_FILE,
        COLUMN_SYSTEM_VISIBLE,
        COLUMN_USER_VISIBLE
    ) = range (7)

    def __init__ (self, menu_files):
        Gtk.TreeStore.__init__ (self, bool, str, str, Gio.Icon, str, bool, bool)

        self.entries_list_iter = None
        
        if (len (menu_files) < 1):
            menu_files = ["applications.menu"]

        for menu_file in menu_files:
            if menu_file == "applications.menu" and os.environ.has_key ("XDG_MENU_PREFIX"):
                menu_file = os.environ["XDG_MENU_PREFIX"] + menu_file

            tree = GObject.new (GMenu.Tree, menu_basename = menu_file, flags = GMenu.TreeFlags.INCLUDE_EXCLUDED|GMenu.TreeFlags.SORT_DISPLAY_NAME)
            tree.load_sync ()

            self.__append_directory (tree.get_root_directory (), None, False, menu_file)

            system_file = lookup_system_menu_file (menu_file)
            if system_file:
                system_tree = GObject.new (GMenu.Tree, menu_path = system_file, flags = GMenu.TreeFlags.INCLUDE_EXCLUDED|GMenu.TreeFlags.SORT_DISPLAY_NAME)
                system_tree.load_sync ()

                self.__append_directory (system_tree.get_root_directory (), None, True, menu_file)

    def __append_directory (self, directory, parent_iter, system, menu_file):
        if not directory:
            return
        
        iter = self.iter_children (parent_iter)
        while iter is not None:
            if self.get_value(iter, self.COLUMN_ID) == directory.get_menu_id ():
                break
            iter = self.iter_next (iter)

        if iter is None:
            row = (False, directory.get_menu_id (), directory.get_name (), directory.get_icon (), menu_file, False, False)
            iter = self.append (parent_iter, row)

        if system:
            self.set_value (iter, self.COLUMN_SYSTEM_VISIBLE, True)
        else:
            self.set_value (iter, self.COLUMN_USER_VISIBLE, True)

        dir_iter = directory.iter ()
        next_type = dir_iter.next ()
        while next_type != GMenu.TreeItemType.INVALID:
            current_type = next_type
            next_type = dir_iter.next ()

            if current_type == GMenu.TreeItemType.DIRECTORY:
                child_item = dir_iter.get_directory ()
                self.__append_directory (child_item, iter, system, None)
                
            if current_type != GMenu.TreeItemType.ENTRY:
                continue
            
            child_item = dir_iter.get_entry ()

            child_iter = self.iter_children (iter)
            while child_iter is not None:
                if self.get_value(child_iter, self.COLUMN_IS_ENTRY) and \
                   self.get_value(child_iter, self.COLUMN_ID) == child_item.get_desktop_file_id ():
                        break
                child_iter = self.iter_next (child_iter)

            if child_iter is None:
                app_info = child_item.get_app_info ()
                row = (True, child_item.get_desktop_file_id (), app_info.get_display_name (), app_info.get_icon (), None, False, False)
                child_iter = self.append (iter, row)

            if system:
                self.set_value (child_iter, self.COLUMN_SYSTEM_VISIBLE, not child_item.get_is_excluded (),)
            else:
                self.set_value (child_iter, self.COLUMN_USER_VISIBLE, not child_item.get_is_excluded (),)
