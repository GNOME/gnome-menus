# -*- Mode: Python -*-

cdef extern from "glib/gslist.h":
    ctypedef struct GSList:
        void   *data
        GSList *next

    void g_slist_free (GSList *list)
    
cdef extern from "libgnomevfs/gnome-vfs-init.h":
    int gnome_vfs_init        ()
    int gnome_vfs_initialized ()

cdef extern from "menu-tree.h":
    ctypedef struct MenuTree
    ctypedef struct MenuTreeItem
    ctypedef struct MenuTreeDirectory
    ctypedef struct MenuTreeEntry
    ctypedef struct MenuTreeSeparator
    ctypedef struct MenuTreeHeader
    ctypedef struct MenuTreeAlias

    ctypedef void (*MenuTreeChangedFunc) (MenuTree *tree,
                                          void     *user_data)
    
    ctypedef enum MenuTreeItemType:
        MENU_TREE_ITEM_INVALID = 0
        MENU_TREE_ITEM_DIRECTORY
        MENU_TREE_ITEM_ENTRY
        MENU_TREE_ITEM_SEPARATOR
        MENU_TREE_ITEM_HEADER
        MENU_TREE_ITEM_ALIAS
    
    MenuTree *menu_tree_lookup (char *menu_file)

    MenuTree *menu_tree_ref   (MenuTree *tree)
    void      menu_tree_unref (MenuTree *tree)

    MenuTreeDirectory *menu_tree_get_root_directory      (MenuTree *tree)
    MenuTreeDirectory *menu_tree_get_directory_from_path (MenuTree *tree,
                                                          char     *path)

    MenuTreeItem *menu_tree_item_ref   (MenuTreeItem *item)
    void          menu_tree_item_unref (MenuTreeItem *item)

    MenuTreeItemType   menu_tree_item_get_type   (MenuTreeItem *item)
    MenuTreeDirectory *menu_tree_item_get_parent (MenuTreeItem *item)

    GSList *menu_tree_directory_get_contents (MenuTreeDirectory *directory)
    char   *menu_tree_directory_get_name     (MenuTreeDirectory *directory)
    char   *menu_tree_directory_get_comment  (MenuTreeDirectory *directory)
    char   *menu_tree_directory_get_icon     (MenuTreeDirectory *directory)
    char   *menu_tree_directory_make_path    (MenuTreeDirectory *directory,
                                              MenuTreeEntry     *entry)

    char *menu_tree_entry_get_name    (MenuTreeEntry *entry)
    char *menu_tree_entry_get_comment (MenuTreeEntry *entry)
    char *menu_tree_entry_get_icon    (MenuTreeEntry *entry)
    char *menu_tree_entry_get_exec    (MenuTreeEntry *entry)

    char *menu_tree_entry_get_desktop_file_path (MenuTreeEntry *entry)
    char *menu_tree_entry_get_desktop_file_id   (MenuTreeEntry *entry)

    MenuTreeDirectory *menu_tree_header_get_directory (MenuTreeHeader *header)

    MenuTreeDirectory *menu_tree_alias_get_directory (MenuTreeAlias *alias)
    MenuTreeItem      *menu_tree_alias_get_item      (MenuTreeAlias *alias)

    void menu_tree_add_monitor    (MenuTree            *tree,
                                   MenuTreeChangedFunc  callback,
                                   void                *user_data)
    void menu_tree_remove_monitor (MenuTree            *tree,
                                   MenuTreeChangedFunc  callback,
                                   void                *user_data)

TYPE_INVALID    = MENU_TREE_ITEM_INVALID
TYPE_DIRECTORY  = MENU_TREE_ITEM_DIRECTORY
TYPE_ENTRY      = MENU_TREE_ITEM_ENTRY
TYPE_SEPARATOR  = MENU_TREE_ITEM_SEPARATOR
TYPE_HEADER     = MENU_TREE_ITEM_HEADER
TYPE_ALIAS      = MENU_TREE_ITEM_ALIAS

cdef class Item
cdef class Directory (Item)
cdef class Entry (Item)
cdef class Separator (Item)
cdef class Header (Item)
cdef class Alias (Item)

cdef class Item:
    cdef MenuTreeItem *item

    def __new__ (self):
        self.item = NULL
        
    def __dealloc__ (self):
        if self.item != NULL:
            menu_tree_item_unref (self.item)
        self.item = NULL

    cdef void _set_item (self, MenuTreeItem *item):
        if item != NULL:
            item = menu_tree_item_ref (item)
        if self.item != NULL:
            menu_tree_item_unref (self.item)
        self.item = item

    def get_type (self):
        return menu_tree_item_get_type (self.item)
    
    def get_parent (self):
        cdef MenuTreeDirectory *parent
        cdef Directory          retval

        parent = menu_tree_item_get_parent (self.item)
        if parent == NULL:
            return None

        retval = Directory ()
        retval._set_item (<MenuTreeItem *> parent)

        return retval

    def __getattr__ (self, name):
        if name == "type":
            return self.get_type ()
        elif name == "parent":
            return self.get_parent ()
        else:
            raise AttributeError, name
        
cdef class Directory (Item):
    def get_contents (self):
        cdef GSList           *contents
        cdef GSList           *tmp
        cdef MenuTreeItem     *itemp
        cdef MenuTreeItemType  type
        cdef Item              item
        cdef Entry             entry
        cdef Separator         separator
        cdef Header            header
        cdef Alias             alias

        contents = menu_tree_directory_get_contents (<MenuTreeDirectory *> self.item)
        if contents == NULL:
            return None

        retval = []
        
        tmp = contents
        while tmp != NULL:
            itemp = <MenuTreeItem *> tmp.data

            type = menu_tree_item_get_type (itemp)

            if type == MENU_TREE_ITEM_DIRECTORY:
                item = Directory ()
                item._set_item (itemp)
            elif type == MENU_TREE_ITEM_ENTRY:
                item = Entry ()
                item._set_item (itemp)
            elif type == MENU_TREE_ITEM_SEPARATOR:
                item = Separator ()
                item._set_item (itemp)
            elif type == MENU_TREE_ITEM_HEADER:
                item = Header ()
                item._set_item (itemp)
            else: # type == MENU_TREE_ITEM_DIRECTORY:
                item = Alias ()
                item._set_item (itemp)

            retval.append (item)

            menu_tree_item_unref (itemp)

            tmp = tmp.next

        g_slist_free (contents)

        return retval

    def get_name (self):
        cdef char *name
        
        name = menu_tree_directory_get_name (<MenuTreeDirectory *> self.item)
        if name == NULL:
            return None
        else:
            return name
    
    def get_comment (self):
        cdef char *comment
        
        comment = menu_tree_directory_get_comment (<MenuTreeDirectory *> self.item)
        if comment == NULL:
            return None
        else:
            return comment
        
    def get_icon (self):
        cdef char *icon
        
        icon = menu_tree_directory_get_icon (<MenuTreeDirectory *> self.item)
        if icon == NULL:
            return None
        else:
            return icon

    def make_path (self):
        # FIXME: implement
        pass
    
    def __getattr__ (self, name):
        if name == "type":
            return self.get_type ()
        elif name == "parent":
            return self.get_parent ()
        elif name == "name":
            return self.get_name ()
        elif name == "comment":
            return self.get_comment ()
        elif name == "icon":
            return self.get_icon ()
        else:
            raise AttributeError, name

cdef class Entry (Item):
    def get_name (self):
        cdef char *name
        
        name = menu_tree_entry_get_name (<MenuTreeEntry *> self.item)
        if name == NULL:
            return None
        else:
            return name
    
    def get_comment (self):
        cdef char *comment
        
        comment = menu_tree_entry_get_comment (<MenuTreeEntry *> self.item)
        if comment == NULL:
            return None
        else:
            return comment
    
    def get_icon (self):
        cdef char *icon
        
        icon = menu_tree_entry_get_icon (<MenuTreeEntry *> self.item)
        if icon == NULL:
            return None
        else:
            return icon
    
    def get_exec (self):
        cdef char *exec_info
        
        exec_info = menu_tree_entry_get_exec (<MenuTreeEntry *> self.item)
        if exec_info == NULL:
            return None
        else:
            return exec_info

    def get_desktop_file_path (self):
        cdef char *desktop_file_path
        
        desktop_file_path = menu_tree_entry_get_desktop_file_path (<MenuTreeEntry *> self.item)
        if desktop_file_path == NULL:
            return None
        else:
            return desktop_file_path

    def get_desktop_file_id (self):
        cdef char *desktop_file_id
        
        desktop_file_id = menu_tree_entry_get_desktop_file_id (<MenuTreeEntry *> self.item)
        if desktop_file_id == NULL:
            return None
        else:
            return desktop_file_id

    def __getattr__ (self, name):
        if name == "type":
            return self.get_type ()
        elif name == "parent":
            return self.get_parent ()
        elif name == "name":
            return self.get_name ()
        elif name == "comment":
            return self.get_comment ()
        elif name == "icon":
            return self.get_icon ()
        elif name == "exec_info":
            return self.get_exec ()
        elif name == "desktop_file_path":
            return self.get_desktop_file_path ()
        elif name == "desktop_file_id":
            return self.get_desktop_file_id ()
        else:
            raise AttributeError, name

cdef class Separator (Item):
    pass

cdef class Header (Item):
    def get_directory (self):
        cdef MenuTreeDirectory *directory
        cdef Directory          retval

        directory = menu_tree_header_get_directory (<MenuTreeHeader *> self.item)
        if directory == NULL:
            return None

        retval = Directory ()
        retval._set_item (<MenuTreeItem *> directory)

        return retval
    
    def __getattr__ (self, name):
        if name == "type":
            return self.get_type ()
        elif name == "parent":
            return self.get_parent ()
        elif name == "directory":
            return self.get_directory ()
        else:
            raise AttributeError, name

cdef class Alias (Item):
    def get_directory (self):
        cdef MenuTreeDirectory *directory
        cdef Directory          retval

        directory = menu_tree_alias_get_directory (<MenuTreeAlias *> self.item)
        if directory == NULL:
            return None

        retval = Directory ()
        retval._set_item (<MenuTreeItem *> directory)

        return retval

    def get_item (self):
        cdef MenuTreeItem     *item
        cdef MenuTreeItemType  type
        cdef Item              retval

        item = menu_tree_alias_get_item (<MenuTreeAlias *> self.item)
        if item == NULL:
            return None

        type = menu_tree_item_get_type (self.item)
        if type == MENU_TREE_ITEM_DIRECTORY:
            retval = Directory ()
        elif type == MENU_TREE_ITEM_ENTRY:
            retval = Entry ()
        elif type == MENU_TREE_ITEM_SEPARATOR:
            retval = Separator ()
        elif type == MENU_TREE_ITEM_HEADER:
            retval = Header ()
        else: # type == MENU_TREE_ITEM_ALIAS:
            retval = Alias ()

        retval._set_item (<MenuTreeItem *> item)

        return retval
    
    def __getattr__ (self, name):
        if name == "type":
            return self.get_type ()
        elif name == "parent":
            return self.get_parent ()
        elif name == "directory":
            return self.get_directory ()
        elif name == "item":
            return self.get_item ()
        else:
            raise AttributeError, name

cdef class Tree:
    cdef MenuTree *tree

    def __new__ (self):
        self.tree = NULL
        
    def __dealloc__ (self):
        if self.tree != NULL:
            menu_tree_unref (self.tree)
        self.tree = NULL

    cdef void _set_tree (self, MenuTree *tree):
        if tree != NULL:
            tree = menu_tree_ref (tree)
        if self.tree != NULL:
            menu_tree_unref (self.tree)
        self.tree = tree

    def get_root_directory (self):
        cdef MenuTreeDirectory *root
        cdef Directory          retval

        root = menu_tree_get_root_directory (self.tree)
        if root == NULL:
            return None

        retval = Directory ()
        retval._set_item (<MenuTreeItem *> root)

        return retval

    def get_directory_from_path (self, path):
        cdef MenuTreeDirectory *directory
        cdef Directory          retval

        directory = menu_tree_get_directory_from_path (self.tree, path)
        if directory == NULL:
            return None

        retval = Directory ()
        retval._set_item (<MenuTreeItem *> directory)

        return retval

    def add_monitor (self, callback, user_data = None):
        # FIXME: implement
        pass

    def remove_monitor (self, callback, user_data = None):
        # FIXME: implement
        pass

def lookup_tree (menu_file):
    cdef MenuTree *tree
    cdef Tree      retval

    if not gnome_vfs_initialized ():
        gnome_vfs_init ()

    tree = menu_tree_lookup (menu_file)

    retval = Tree ()
    retval._set_tree (tree)
    menu_tree_unref (tree)

    return retval
