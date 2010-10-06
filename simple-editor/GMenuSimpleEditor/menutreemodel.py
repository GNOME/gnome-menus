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
import gobject
from gi.repository import Gtk
from gi.repository import GdkPixbuf
import gmenu

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

def load_icon_from_path (icon_path):
    if os.path.isfile (icon_path):
        try:
            return GdkPixbuf.new_from_file_at_size (icon_path, 24, 24)
        except:
            pass
    return None

def load_icon_from_data_dirs (icon_value):
    data_dirs = None
    if os.environ.has_key ("XDG_DATA_DIRS"):
        data_dirs = os.environ["XDG_DATA_DIRS"]
    if not data_dirs:
        data_dirs = "/usr/local/share/:/usr/share/"

    for data_dir in data_dirs.split (":"):
        retval = load_icon_from_path (os.path.join (data_dir, "pixmaps", icon_value))
        if retval:
            return retval
        retval = load_icon_from_path (os.path.join (data_dir, "icons", icon_value))
        if retval:
            return retval
    
    return None

def load_icon (icon_theme, icon_value):
    if not icon_value:
        return

    if os.path.isabs (icon_value):
        icon = load_icon_from_path (icon_value)
        if icon:
            return icon
        icon_name = os.path.basename (icon_value)
    else:
        icon_name = icon_value
    
    if icon_name.endswith (".png"):
        icon_name = icon_name[:-len (".png")]
    elif icon_name.endswith (".xpm"):
        icon_name = icon_name[:-len (".xpm")]
    elif icon_name.endswith (".svg"):
        icon_name = icon_name[:-len (".svg")]
    
    try:
        return icon_theme.load_icon (icon_name, 24, 0)
    except:
        return load_icon_from_data_dirs (icon_value)

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
        Gtk.TreeStore.__init__ (self, bool, str, str, GdkPixbuf.Pixbuf, str, bool, bool)

        self.entries_list_iter = None
        
        self.icon_theme = Gtk.IconTheme.get_default ()

        if (len (menu_files) < 1):
            menu_files = ["applications.menu", "settings.menu"]

        for menu_file in menu_files:
            if menu_file == "applications.menu" and os.environ.has_key ("XDG_MENU_PREFIX"):
                menu_file = os.environ["XDG_MENU_PREFIX"] + menu_file

            tree = gmenu.lookup_tree (menu_file, gmenu.FLAGS_INCLUDE_EXCLUDED)
            tree.sort_key = gmenu.SORT_DISPLAY_NAME
            self.__append_directory (tree.root, None, False, menu_file)

            system_file = lookup_system_menu_file (menu_file)
            if system_file:
                system_tree = gmenu.lookup_tree (system_file, gmenu.FLAGS_INCLUDE_EXCLUDED)
                system_tree.sort_key = gmenu.SORT_DISPLAY_NAME
                self.__append_directory (system_tree.root, None, True, menu_file)

    def __append_directory (self, directory, parent_iter, system, menu_file):
        if not directory:
            return
        
        (has_iter, iter) = self.iter_children (parent_iter)
        while has_iter:
            if self.get_value(iter, self.COLUMN_ID) == directory.menu_id:
                break
            has_iter = self.iter_next (iter)

        if not has_iter:
            row = (False, directory.menu_id, directory.name, load_icon (self.icon_theme, directory.icon), menu_file, False, False)
            iter = self.append (parent_iter, row)

        if system:
            self.set_value (iter, self.COLUMN_SYSTEM_VISIBLE, True)
        else:
            self.set_value (iter, self.COLUMN_USER_VISIBLE, True)

        for child_item in directory.contents:
            if isinstance (child_item, gmenu.Directory):
                self.__append_directory (child_item, iter, system, None)
                
            if not isinstance (child_item, gmenu.Entry):
                continue
            
            (has_iter, child_iter) = self.iter_children (iter)
            while has_iter:
                if child_item.type == gmenu.TYPE_ENTRY and \
                   self.get_value(child_iter, self.COLUMN_IS_ENTRY) and \
                   self.get_value(child_iter, self.COLUMN_ID) == child_item.desktop_file_id:
                        break
                has_iter = self.iter_next (child_iter)

            if not has_iter:
                row = (True, child_item.desktop_file_id, child_item.display_name, load_icon (self.icon_theme, child_item.icon), None, False, False)
                child_iter = self.append (iter, row)

            if system:
                self.set_value (child_iter, self.COLUMN_SYSTEM_VISIBLE, not child_item.is_excluded,)
            else:
                self.set_value (child_iter, self.COLUMN_USER_VISIBLE, not child_item.is_excluded,)
