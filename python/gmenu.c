/*
 * Copyright (C) 2005 Red Hat, Inc.
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

#include <Python.h>
#include <menu-tree.h>
#include <libgnomevfs/gnome-vfs-init.h>

typedef struct
{
  PyObject_HEAD
  MenuTree *tree;
  GSList   *callbacks;
} PyGMenuTree;

typedef struct
{
  PyObject *tree;
  PyObject *callback;
  PyObject *user_data;
} PyGMenuTreeCallback;

typedef struct
{
  PyObject_HEAD
  MenuTreeItem *item;
} PyGMenuTreeItem;

typedef PyGMenuTreeItem PyGMenuTreeDirectory;
typedef PyGMenuTreeItem PyGMenuTreeEntry;
typedef PyGMenuTreeItem PyGMenuTreeSeparator;
typedef PyGMenuTreeItem PyGMenuTreeHeader;
typedef PyGMenuTreeItem PyGMenuTreeAlias;

static PyGMenuTree          *pygmenu_tree_wrap           (MenuTree          *tree);
static PyGMenuTreeDirectory *pygmenu_tree_directory_wrap (MenuTreeDirectory *directory);
static PyGMenuTreeEntry     *pygmenu_tree_entry_wrap     (MenuTreeEntry     *entry);
static PyGMenuTreeSeparator *pygmenu_tree_separator_wrap (MenuTreeSeparator *separator);
static PyGMenuTreeHeader    *pygmenu_tree_header_wrap    (MenuTreeHeader    *header);
static PyGMenuTreeAlias     *pygmenu_tree_alias_wrap     (MenuTreeAlias     *alias);

static PyObject *pygmenu_tree_item_type_invalid   = NULL;
static PyObject *pygmenu_tree_item_type_directory = NULL;
static PyObject *pygmenu_tree_item_type_entry     = NULL;
static PyObject *pygmenu_tree_item_type_separator = NULL;
static PyObject *pygmenu_tree_item_type_header    = NULL;
static PyObject *pygmenu_tree_item_type_alias     = NULL;

static PyObject *pygmenu_tree_flags_none             = NULL;
static PyObject *pygmenu_tree_flags_include_excluded = NULL;
static PyObject *pygmenu_tree_flags_show_empty       = NULL;

static void
pygmenu_tree_item_dealloc (PyGMenuTreeItem *self)
{
  if (self->item != NULL)
    {
      menu_tree_item_set_user_data (self->item, NULL, NULL);
      menu_tree_item_unref (self->item);
      self->item = NULL;
    }

  PyObject_DEL (self);
}

static PyObject *
pygmenu_tree_item_get_type (PyObject *self,
			    PyObject *args)
{
  PyGMenuTreeItem *item;
  PyObject        *retval;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Item.get_type"))
	return NULL;
    }

  item = (PyGMenuTreeItem *) self;

  switch (menu_tree_item_get_type (item->item))
    {
    case MENU_TREE_ITEM_DIRECTORY:
      retval = pygmenu_tree_item_type_directory;
      break;

    case MENU_TREE_ITEM_ENTRY:
      retval = pygmenu_tree_item_type_entry;
      break;

    case MENU_TREE_ITEM_SEPARATOR:
      retval = pygmenu_tree_item_type_separator;
      break;

    case MENU_TREE_ITEM_HEADER:
      retval = pygmenu_tree_item_type_header;
      break;

    case MENU_TREE_ITEM_ALIAS:
      retval = pygmenu_tree_item_type_alias;
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  Py_INCREF (retval);

  return retval;
}

static PyObject *
pygmenu_tree_item_get_parent (PyObject *self,
			      PyObject *args)
{
  PyGMenuTreeItem      *item;
  MenuTreeDirectory    *parent;
  PyGMenuTreeDirectory *retval;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Item.get_parent"))
	return NULL;
    }

  item = (PyGMenuTreeItem *) self;

  parent = menu_tree_item_get_parent (item->item);
  if (parent == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = pygmenu_tree_directory_wrap (parent);

  menu_tree_item_unref (parent);

  return (PyObject *) retval;
}

static struct PyMethodDef pygmenu_tree_item_methods[] =
{
  { "get_type",   pygmenu_tree_item_get_type,   METH_VARARGS },
  { "get_parent", pygmenu_tree_item_get_parent, METH_VARARGS },
  { NULL,         NULL,                         0            }
};

static PyTypeObject PyGMenuTreeItem_Type = 
{
  PyObject_HEAD_INIT(NULL)
  0,                                             /* ob_size */
  "gmenu.Item",                                  /* tp_name */
  sizeof (PyGMenuTreeItem),                      /* tp_basicsize */
  0,                                             /* tp_itemsize */
  (destructor) pygmenu_tree_item_dealloc,        /* tp_dealloc */
  (printfunc)0,                                  /* tp_print */
  (getattrfunc)0,                                /* tp_getattr */
  (setattrfunc)0,                                /* tp_setattr */
  (cmpfunc)0,                                    /* tp_compare */
  (reprfunc)0,                                   /* tp_repr */
  0,                                             /* tp_as_number */
  0,                                             /* tp_as_sequence */
  0,                                             /* tp_as_mapping */
  (hashfunc)0,                                   /* tp_hash */
  (ternaryfunc)0,                                /* tp_call */
  (reprfunc)0,                                   /* tp_str */
  (getattrofunc)0,                               /* tp_getattro */
  (setattrofunc)0,                               /* tp_setattro */
  0,                                             /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,      /* tp_flags */
  NULL,                                          /* Documentation string */
  (traverseproc)0,                               /* tp_traverse */
  (inquiry)0,                                    /* tp_clear */
  (richcmpfunc)0,                                /* tp_richcompare */
  0,                                             /* tp_weaklistoffset */
  (getiterfunc)0,                                /* tp_iter */
  (iternextfunc)0,                               /* tp_iternext */
  pygmenu_tree_item_methods,                     /* tp_methods */
  0,                                             /* tp_members */
  0,                                             /* tp_getset */
  (PyTypeObject *)0,                             /* tp_base */
  (PyObject *)0,                                 /* tp_dict */
  0,                                             /* tp_descr_get */
  0,                                             /* tp_descr_set */
  0,                                             /* tp_dictoffset */
  (initproc)0,                                   /* tp_init */
  0,                                             /* tp_alloc */
  0,                                             /* tp_new */
  0,                                             /* tp_free */
  (inquiry)0,                                    /* tp_is_gc */
  (PyObject *)0,                                 /* tp_bases */
};

static PyObject *
pygmenu_tree_directory_get_contents (PyObject *self,
				     PyObject *args)
{
  PyGMenuTreeDirectory *directory;
  PyObject             *retval;
  GSList               *contents;
  GSList               *tmp;

  if (!PyArg_ParseTuple (args, ":gmenu.Directory.get_contents"))
    return NULL;

  directory = (PyGMenuTreeDirectory *) self;

  contents = menu_tree_directory_get_contents (MENU_TREE_DIRECTORY (directory->item));
  if (contents == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = PyList_New (0);

  tmp = contents;
  while (tmp != NULL)
    {
      MenuTreeItem *item = tmp->data;
      PyObject     *pyitem;

      switch (menu_tree_item_get_type (item))
	{
	case MENU_TREE_ITEM_DIRECTORY:
	  pyitem = (PyObject *) pygmenu_tree_directory_wrap (MENU_TREE_DIRECTORY (item));
	  break;

	case MENU_TREE_ITEM_ENTRY:
	  pyitem = (PyObject *) pygmenu_tree_entry_wrap (MENU_TREE_ENTRY (item));
	  break;

	case MENU_TREE_ITEM_SEPARATOR:
	  pyitem = (PyObject *) pygmenu_tree_separator_wrap (MENU_TREE_SEPARATOR (item));
	  break;

	case MENU_TREE_ITEM_HEADER:
	  pyitem = (PyObject *) pygmenu_tree_header_wrap (MENU_TREE_HEADER (item));
	  break;

	case MENU_TREE_ITEM_ALIAS:
	  pyitem = (PyObject *) pygmenu_tree_alias_wrap (MENU_TREE_ALIAS (item));
	  break;

	default:
	  g_assert_not_reached ();
	  break;
	}

      PyList_Append (retval, pyitem);
      Py_DECREF (pyitem);

      menu_tree_item_unref (item);

      tmp = tmp->next;
    }

  g_slist_free (contents);

  return retval;
}

static PyObject *
pygmenu_tree_directory_get_name (PyObject *self,
				 PyObject *args)
{
  PyGMenuTreeDirectory *directory;
  const char           *name;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Directory.get_name"))
	return NULL;
    }

  directory = (PyGMenuTreeDirectory *) self;

  name = menu_tree_directory_get_name (MENU_TREE_DIRECTORY (directory->item));
  if (name == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (name);
}

static PyObject *
pygmenu_tree_directory_get_comment (PyObject *self,
				    PyObject *args)
{
  PyGMenuTreeDirectory *directory;
  const char           *comment;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Directory.get_comment"))
	return NULL;
    }

  directory = (PyGMenuTreeDirectory *) self;

  comment = menu_tree_directory_get_comment (MENU_TREE_DIRECTORY (directory->item));
  if (comment == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (comment);
}

static PyObject *
pygmenu_tree_directory_get_icon (PyObject *self,
				 PyObject *args)
{
  PyGMenuTreeDirectory *directory;
  const char           *icon;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Directory.get_icon"))
	return NULL;
    }

  directory = (PyGMenuTreeDirectory *) self;

  icon = menu_tree_directory_get_icon (MENU_TREE_DIRECTORY (directory->item));
  if (icon == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (icon);
}

static PyObject *
pygmenu_tree_directory_make_path (PyObject *self,
				  PyObject *args)
{
  PyGMenuTreeDirectory *directory;
  PyGMenuTreeEntry     *entry;
  PyObject             *retval;
  char                 *path;

  if (!PyArg_ParseTuple (args, "O:gmenu.Directory.make_path", &entry))
	return NULL;

  directory = (PyGMenuTreeDirectory *) self;

  path = menu_tree_directory_make_path (MENU_TREE_DIRECTORY (directory->item),
					MENU_TREE_ENTRY (entry->item));
  if (path == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = PyString_FromString (path);

  g_free (path);

  return retval;
}

static PyObject *
pygmenu_tree_directory_getattro (PyGMenuTreeDirectory *self,
				 PyObject             *py_attr)
{
  if (PyString_Check (py_attr))
    {
      char *attr;
  
      attr = PyString_AsString (py_attr);

      if (!strcmp (attr, "__members__"))
	{
	  return Py_BuildValue("[sssss]",
			       "type",
			       "parent",
			       "name",
			       "comment",
			       "icon");
	}
      else if (!strcmp (attr, "type"))
	{
	  return pygmenu_tree_item_get_type ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "parent"))
	{
	  return pygmenu_tree_item_get_parent ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "name"))
	{
	  return pygmenu_tree_directory_get_name ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "comment"))
	{
	  return pygmenu_tree_directory_get_comment ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "icon"))
	{
	  return pygmenu_tree_directory_get_icon ((PyObject *) self, NULL);
	}
    }

  return PyObject_GenericGetAttr ((PyObject *) self, py_attr);
}

static struct PyMethodDef pygmenu_tree_directory_methods[] =
{
  { "get_contents", pygmenu_tree_directory_get_contents, METH_VARARGS },
  { "get_name",     pygmenu_tree_directory_get_name,     METH_VARARGS },
  { "get_comment",  pygmenu_tree_directory_get_comment,  METH_VARARGS },
  { "get_icon",     pygmenu_tree_directory_get_icon,     METH_VARARGS },
  { "make_path",    pygmenu_tree_directory_make_path,    METH_VARARGS },
  { NULL,           NULL,                                0            }
};

static PyTypeObject PyGMenuTreeDirectory_Type = 
{
  PyObject_HEAD_INIT(NULL)
  0,                                              /* ob_size */
  "gmenu.Directory",                              /* tp_name */
  sizeof (PyGMenuTreeDirectory),                  /* tp_basicsize */
  0,                                              /* tp_itemsize */
  (destructor) pygmenu_tree_item_dealloc,         /* tp_dealloc */
  (printfunc)0,                                   /* tp_print */
  (getattrfunc)0,                                 /* tp_getattr */
  (setattrfunc)0,                                 /* tp_setattr */
  (cmpfunc)0,                                     /* tp_compare */
  (reprfunc)0,                                    /* tp_repr */
  0,                                              /* tp_as_number */
  0,                                              /* tp_as_sequence */
  0,                                              /* tp_as_mapping */
  (hashfunc)0,                                    /* tp_hash */
  (ternaryfunc)0,                                 /* tp_call */
  (reprfunc)0,                                    /* tp_str */
  (getattrofunc) pygmenu_tree_directory_getattro, /* tp_getattro */
  (setattrofunc)0,                                /* tp_setattro */
  0,                                              /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                             /* tp_flags */
  NULL,                                           /* Documentation string */
  (traverseproc)0,                                /* tp_traverse */
  (inquiry)0,                                     /* tp_clear */
  (richcmpfunc)0,                                 /* tp_richcompare */
  0,                                              /* tp_weaklistoffset */
  (getiterfunc)0,                                 /* tp_iter */
  (iternextfunc)0,                                /* tp_iternext */
  pygmenu_tree_directory_methods,                 /* tp_methods */
  0,                                              /* tp_members */
  0,                                              /* tp_getset */
  (PyTypeObject *)0,                              /* tp_base */
  (PyObject *)0,                                  /* tp_dict */
  0,                                              /* tp_descr_get */
  0,                                              /* tp_descr_set */
  0,                                              /* tp_dictoffset */
  (initproc)0,                                    /* tp_init */
  0,                                              /* tp_alloc */
  0,                                              /* tp_new */
  0,                                              /* tp_free */
  (inquiry)0,                                     /* tp_is_gc */
  (PyObject *)0,                                  /* tp_bases */
};

static PyGMenuTreeDirectory *
pygmenu_tree_directory_wrap (MenuTreeDirectory *directory)
{
  PyGMenuTreeDirectory *retval;

  if ((retval = menu_tree_item_get_user_data (MENU_TREE_ITEM (directory))) != NULL)
    {
      Py_INCREF (retval);
      return retval;
    }

  if (!(retval = (PyGMenuTreeDirectory *) PyObject_NEW (PyGMenuTreeDirectory,
							&PyGMenuTreeDirectory_Type)))
    return NULL;

  retval->item = menu_tree_item_ref (directory);

  menu_tree_item_set_user_data (MENU_TREE_ITEM (directory), retval, NULL);

  return retval;
}

static PyObject *
pygmenu_tree_entry_get_name (PyObject *self,
			     PyObject *args)
{
  PyGMenuTreeEntry *entry;
  const char       *name;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Entry.get_name"))
	return NULL;
    }

  entry = (PyGMenuTreeEntry *) self;

  name = menu_tree_entry_get_name (MENU_TREE_ENTRY (entry->item));
  if (name == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (name);
}

static PyObject *
pygmenu_tree_entry_get_comment (PyObject *self,
				PyObject *args)
{
  PyGMenuTreeEntry *entry;
  const char       *comment;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Entry.get_comment"))
	return NULL;
    }

  entry = (PyGMenuTreeEntry *) self;

  comment = menu_tree_entry_get_comment (MENU_TREE_ENTRY (entry->item));
  if (comment == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (comment);
}

static PyObject *
pygmenu_tree_entry_get_icon (PyObject *self,
			     PyObject *args)
{
  PyGMenuTreeEntry *entry;
  const char       *icon;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Entry.get_icon"))
	return NULL;
    }

  entry = (PyGMenuTreeEntry *) self;

  icon = menu_tree_entry_get_icon (MENU_TREE_ENTRY (entry->item));
  if (icon == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (icon);
}

static PyObject *
pygmenu_tree_entry_get_exec (PyObject *self,
			     PyObject *args)
{
  PyGMenuTreeEntry *entry;
  const char       *exec;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Entry.get_exec"))
	return NULL;
    }

  entry = (PyGMenuTreeEntry *) self;

  exec = menu_tree_entry_get_exec (MENU_TREE_ENTRY (entry->item));
  if (exec == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (exec);
}

static PyObject *
pygmenu_tree_entry_get_desktop_file_path (PyObject *self,
					  PyObject *args)
{
  PyGMenuTreeEntry *entry;
  const char       *desktop_file_path;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Entry.get_desktop_file_path"))
	return NULL;
    }

  entry = (PyGMenuTreeEntry *) self;

  desktop_file_path = menu_tree_entry_get_desktop_file_path (MENU_TREE_ENTRY (entry->item));
  if (desktop_file_path == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (desktop_file_path);
}

static PyObject *
pygmenu_tree_entry_get_desktop_file_id (PyObject *self,
					PyObject *args)
{
  PyGMenuTreeEntry *entry;
  const char       *desktop_file_id;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Entry.get_desktop_file_id"))
	return NULL;
    }

  entry = (PyGMenuTreeEntry *) self;

  desktop_file_id = menu_tree_entry_get_desktop_file_id (MENU_TREE_ENTRY (entry->item));
  if (desktop_file_id == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  return PyString_FromString (desktop_file_id);
}

static PyObject *
pygmenu_tree_entry_get_is_excluded (PyObject *self,
				    PyObject *args)
{
  PyGMenuTreeEntry *entry;
  PyObject         *retval;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Entry.get_is_excluded"))
	return NULL;
    }

  entry = (PyGMenuTreeEntry *) self;

  retval = menu_tree_entry_get_is_excluded (MENU_TREE_ENTRY (entry->item)) ? Py_True : Py_False;
  Py_INCREF (retval);

  return retval;
}

static PyObject *
pygmenu_tree_entry_getattro (PyGMenuTreeEntry *self,
			     PyObject         *py_attr)
{
  if (PyString_Check (py_attr))
    {
      char *attr;
  
      attr = PyString_AsString (py_attr);

      if (!strcmp (attr, "__members__"))
	{
	  return Py_BuildValue("[sssssssss]",
			       "type",
			       "parent",
			       "name",
			       "comment",
			       "icon",
			       "exec_info",
			       "desktop_file_path",
			       "desktop_file_id",
			       "is_excluded");
	}
      else if (!strcmp (attr, "type"))
	{
	  return pygmenu_tree_item_get_type ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "parent"))
	{
	  return pygmenu_tree_item_get_parent ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "name"))
	{
	  return pygmenu_tree_entry_get_name ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "comment"))
	{
	  return pygmenu_tree_entry_get_comment ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "icon"))
	{
	  return pygmenu_tree_entry_get_icon ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "exec_info"))
	{
	  return pygmenu_tree_entry_get_exec ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "desktop_file_path"))
	{
	  return pygmenu_tree_entry_get_desktop_file_path ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "desktop_file_id"))
	{
	  return pygmenu_tree_entry_get_desktop_file_id ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "is_excluded"))
	{
	  return pygmenu_tree_entry_get_is_excluded ((PyObject *) self, NULL);
	}
    }

  return PyObject_GenericGetAttr ((PyObject *) self, py_attr);
}

static struct PyMethodDef pygmenu_tree_entry_methods[] =
{
  { "get_name",              pygmenu_tree_entry_get_name,              METH_VARARGS },
  { "get_comment",           pygmenu_tree_entry_get_comment,           METH_VARARGS },
  { "get_icon",              pygmenu_tree_entry_get_icon,              METH_VARARGS },
  { "get_exec",              pygmenu_tree_entry_get_exec,              METH_VARARGS },
  { "get_desktop_file_path", pygmenu_tree_entry_get_desktop_file_path, METH_VARARGS },
  { "get_desktop_file_id",   pygmenu_tree_entry_get_desktop_file_id,   METH_VARARGS },
  { "get_is_excluded",       pygmenu_tree_entry_get_is_excluded,       METH_VARARGS },
  { NULL,                    NULL,                                     0            }
};

static PyTypeObject PyGMenuTreeEntry_Type = 
{
  PyObject_HEAD_INIT(NULL)
  0,                                             /* ob_size */
  "gmenu.Entry",                                 /* tp_name */
  sizeof (PyGMenuTreeEntry),                     /* tp_basicsize */
  0,                                             /* tp_itemsize */
  (destructor) pygmenu_tree_item_dealloc,        /* tp_dealloc */
  (printfunc)0,                                  /* tp_print */
  (getattrfunc)0,                                /* tp_getattr */
  (setattrfunc)0,                                /* tp_setattr */
  (cmpfunc)0,                                    /* tp_compare */
  (reprfunc)0,                                   /* tp_repr */
  0,                                             /* tp_as_number */
  0,                                             /* tp_as_sequence */
  0,                                             /* tp_as_mapping */
  (hashfunc)0,                                   /* tp_hash */
  (ternaryfunc)0,                                /* tp_call */
  (reprfunc)0,                                   /* tp_str */
  (getattrofunc) pygmenu_tree_entry_getattro,    /* tp_getattro */
  (setattrofunc)0,                               /* tp_setattro */
  0,                                             /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                            /* tp_flags */
  NULL,                                          /* Documentation string */
  (traverseproc)0,                               /* tp_traverse */
  (inquiry)0,                                    /* tp_clear */
  (richcmpfunc)0,                                /* tp_richcompare */
  0,                                             /* tp_weaklistoffset */
  (getiterfunc)0,                                /* tp_iter */
  (iternextfunc)0,                               /* tp_iternext */
  pygmenu_tree_entry_methods,                    /* tp_methods */
  0,                                             /* tp_members */
  0,                                             /* tp_getset */
  (PyTypeObject *)0,                             /* tp_base */
  (PyObject *)0,                                 /* tp_dict */
  0,                                             /* tp_descr_get */
  0,                                             /* tp_descr_set */
  0,                                             /* tp_dictoffset */
  (initproc)0,                                   /* tp_init */
  0,                                             /* tp_alloc */
  0,                                             /* tp_new */
  0,                                             /* tp_free */
  (inquiry)0,                                    /* tp_is_gc */
  (PyObject *)0,                                 /* tp_bases */
};

static PyGMenuTreeEntry *
pygmenu_tree_entry_wrap (MenuTreeEntry *entry)
{
  PyGMenuTreeEntry *retval;

  if ((retval = menu_tree_item_get_user_data (MENU_TREE_ITEM (entry))) != NULL)
    {
      Py_INCREF (retval);
      return retval;
    }

  if (!(retval = (PyGMenuTreeEntry *) PyObject_NEW (PyGMenuTreeEntry,
						    &PyGMenuTreeEntry_Type)))
    return NULL;

  retval->item = menu_tree_item_ref (entry);

  menu_tree_item_set_user_data (MENU_TREE_ITEM (entry), retval, NULL);

  return retval;
}

static PyTypeObject PyGMenuTreeSeparator_Type = 
{
  PyObject_HEAD_INIT(NULL)
  0,                                             /* ob_size */
  "gmenu.Separator",                             /* tp_name */
  sizeof (PyGMenuTreeSeparator),                 /* tp_basicsize */
  0,                                             /* tp_itemsize */
  (destructor) pygmenu_tree_item_dealloc,        /* tp_dealloc */
  (printfunc)0,                                  /* tp_print */
  (getattrfunc)0,                                /* tp_getattr */
  (setattrfunc)0,                                /* tp_setattr */
  (cmpfunc)0,                                    /* tp_compare */
  (reprfunc)0,                                   /* tp_repr */
  0,                                             /* tp_as_number */
  0,                                             /* tp_as_sequence */
  0,                                             /* tp_as_mapping */
  (hashfunc)0,                                   /* tp_hash */
  (ternaryfunc)0,                                /* tp_call */
  (reprfunc)0,                                   /* tp_str */
  (getattrofunc)0,                               /* tp_getattro */
  (setattrofunc)0,                               /* tp_setattro */
  0,                                             /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                            /* tp_flags */
  NULL,                                          /* Documentation string */
  (traverseproc)0,                               /* tp_traverse */
  (inquiry)0,                                    /* tp_clear */
  (richcmpfunc)0,                                /* tp_richcompare */
  0,                                             /* tp_weaklistoffset */
  (getiterfunc)0,                                /* tp_iter */
  (iternextfunc)0,                               /* tp_iternext */
  NULL,                                          /* tp_methods */
  0,                                             /* tp_members */
  0,                                             /* tp_getset */
  (PyTypeObject *)0,                             /* tp_base */
  (PyObject *)0,                                 /* tp_dict */
  0,                                             /* tp_descr_get */
  0,                                             /* tp_descr_set */
  0,                                             /* tp_dictoffset */
  (initproc)0,                                   /* tp_init */
  0,                                             /* tp_alloc */
  0,                                             /* tp_new */
  0,                                             /* tp_free */
  (inquiry)0,                                    /* tp_is_gc */
  (PyObject *)0,                                 /* tp_bases */
};

static PyGMenuTreeSeparator *
pygmenu_tree_separator_wrap (MenuTreeSeparator *separator)
{
  PyGMenuTreeSeparator *retval;

  if ((retval = menu_tree_item_get_user_data (MENU_TREE_ITEM (separator))) != NULL)
    {
      Py_INCREF (retval);
      return retval;
    }

  if (!(retval = (PyGMenuTreeSeparator *) PyObject_NEW (PyGMenuTreeSeparator,
							&PyGMenuTreeSeparator_Type)))
    return NULL;

  retval->item = menu_tree_item_ref (separator);

  menu_tree_item_set_user_data (MENU_TREE_ITEM (separator), retval, NULL);

  return retval;
}

static PyObject *
pygmenu_tree_header_get_directory (PyObject *self,
				   PyObject *args)
{
  PyGMenuTreeHeader    *header;
  MenuTreeDirectory    *directory;
  PyGMenuTreeDirectory *retval;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Header.get_directory"))
	return NULL;
    }

  header = (PyGMenuTreeHeader *) self;

  directory = menu_tree_header_get_directory (MENU_TREE_HEADER (header->item));
  if (directory == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = pygmenu_tree_directory_wrap (directory);

  menu_tree_item_unref (directory);

  return (PyObject *) retval;
}

static PyObject *
pygmenu_tree_header_getattro (PyGMenuTreeHeader *self,
			      PyObject          *py_attr)
{
  if (PyString_Check (py_attr))
    {
      char *attr;
  
      attr = PyString_AsString (py_attr);

      if (!strcmp (attr, "__members__"))
	{
	  return Py_BuildValue("[sss]",
			       "type",
			       "parent",
			       "directory");
	}
      else if (!strcmp (attr, "type"))
	{
	  return pygmenu_tree_item_get_type ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "parent"))
	{
	  return pygmenu_tree_item_get_parent ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "directory"))
	{
	  return pygmenu_tree_header_get_directory ((PyObject *) self, NULL);
	}
    }

  return PyObject_GenericGetAttr ((PyObject *) self, py_attr);
}

static struct PyMethodDef pygmenu_tree_header_methods[] =
{
  { "get_directory", pygmenu_tree_header_get_directory, METH_VARARGS },
  { NULL,            NULL,                              0            }
};

static PyTypeObject PyGMenuTreeHeader_Type = 
{
  PyObject_HEAD_INIT(NULL)
  0,                                             /* ob_size */
  "gmenu.Header",                                /* tp_name */
  sizeof (PyGMenuTreeHeader),                    /* tp_basicsize */
  0,                                             /* tp_itemsize */
  (destructor) pygmenu_tree_item_dealloc,        /* tp_dealloc */
  (printfunc)0,                                  /* tp_print */
  (getattrfunc)0,                                /* tp_getattr */
  (setattrfunc)0,                                /* tp_setattr */
  (cmpfunc)0,                                    /* tp_compare */
  (reprfunc)0,                                   /* tp_repr */
  0,                                             /* tp_as_number */
  0,                                             /* tp_as_sequence */
  0,                                             /* tp_as_mapping */
  (hashfunc)0,                                   /* tp_hash */
  (ternaryfunc)0,                                /* tp_call */
  (reprfunc)0,                                   /* tp_str */
  (getattrofunc) pygmenu_tree_header_getattro,   /* tp_getattro */
  (setattrofunc)0,                               /* tp_setattro */
  0,                                             /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                            /* tp_flags */
  NULL,                                          /* Documentation string */
  (traverseproc)0,                               /* tp_traverse */
  (inquiry)0,                                    /* tp_clear */
  (richcmpfunc)0,                                /* tp_richcompare */
  0,                                             /* tp_weaklistoffset */
  (getiterfunc)0,                                /* tp_iter */
  (iternextfunc)0,                               /* tp_iternext */
  pygmenu_tree_header_methods,                   /* tp_methods */
  0,                                             /* tp_members */
  0,                                             /* tp_getset */
  (PyTypeObject *)0,                             /* tp_base */
  (PyObject *)0,                                 /* tp_dict */
  0,                                             /* tp_descr_get */
  0,                                             /* tp_descr_set */
  0,                                             /* tp_dictoffset */
  (initproc)0,                                   /* tp_init */
  0,                                             /* tp_alloc */
  0,                                             /* tp_new */
  0,                                             /* tp_free */
  (inquiry)0,                                    /* tp_is_gc */
  (PyObject *)0,                                 /* tp_bases */
};

static PyGMenuTreeHeader *
pygmenu_tree_header_wrap (MenuTreeHeader *header)
{
  PyGMenuTreeHeader *retval;

  if ((retval = menu_tree_item_get_user_data (MENU_TREE_ITEM (header))) != NULL)
    {
      Py_INCREF (retval);
      return retval;
    }

  if (!(retval = (PyGMenuTreeHeader *) PyObject_NEW (PyGMenuTreeHeader,
						     &PyGMenuTreeHeader_Type)))
    return NULL;

  retval->item = menu_tree_item_ref (header);

  menu_tree_item_set_user_data (MENU_TREE_ITEM (header), retval, NULL);

  return retval;
}

static PyObject *
pygmenu_tree_alias_get_directory (PyObject *self,
				  PyObject *args)
{
  PyGMenuTreeAlias     *alias;
  MenuTreeDirectory    *directory;
  PyGMenuTreeDirectory *retval;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Alias.get_directory"))
	return NULL;
    }

  alias = (PyGMenuTreeAlias *) self;

  directory = menu_tree_alias_get_directory (MENU_TREE_ALIAS (alias->item));
  if (directory == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = pygmenu_tree_directory_wrap (directory);

  menu_tree_item_unref (directory);

  return (PyObject *) retval;
}

static PyObject *
pygmenu_tree_alias_get_item (PyObject *self,
			     PyObject *args)
{
  PyGMenuTreeAlias *alias;
  MenuTreeItem     *item;
  PyObject         *retval;

  if (args != NULL)
    {
      if (!PyArg_ParseTuple (args, ":gmenu.Alias.get_item"))
	return NULL;
    }

  alias = (PyGMenuTreeAlias *) self;

  item = menu_tree_alias_get_item (MENU_TREE_ALIAS (alias->item));
  if (item == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  switch (menu_tree_item_get_type (item))
    {
    case MENU_TREE_ITEM_DIRECTORY:
      retval = (PyObject *) pygmenu_tree_directory_wrap (MENU_TREE_DIRECTORY (item));
      break;

    case MENU_TREE_ITEM_ENTRY:
      retval = (PyObject *) pygmenu_tree_entry_wrap (MENU_TREE_ENTRY (item));
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  menu_tree_item_unref (item);

  return retval;
}

static PyObject *
pygmenu_tree_alias_getattro (PyGMenuTreeAlias *self,
			     PyObject         *py_attr)
{
  if (PyString_Check (py_attr))
    {
      char *attr;
  
      attr = PyString_AsString (py_attr);

      if (!strcmp (attr, "__members__"))
	{
	  return Py_BuildValue("[ssss]",
			       "type",
			       "parent",
			       "directory",
			       "item");
	}
      else if (!strcmp (attr, "type"))
	{
	  return pygmenu_tree_item_get_type ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "parent"))
	{
	  return pygmenu_tree_item_get_parent ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "directory"))
	{
	  return pygmenu_tree_alias_get_directory ((PyObject *) self, NULL);
	}
      else if (!strcmp (attr, "item"))
	{
	  return pygmenu_tree_alias_get_item ((PyObject *) self, NULL);
	}
    }

  return PyObject_GenericGetAttr ((PyObject *) self, py_attr);
}

static struct PyMethodDef pygmenu_tree_alias_methods[] =
{
  { "get_directory", pygmenu_tree_alias_get_directory, METH_VARARGS },
  { "get_item",      pygmenu_tree_alias_get_item,      METH_VARARGS },
  { NULL,            NULL,                             0            }
};

static PyTypeObject PyGMenuTreeAlias_Type = 
{
  PyObject_HEAD_INIT(NULL)
  0,                                             /* ob_size */
  "gmenu.Alias",                                 /* tp_name */
  sizeof (PyGMenuTreeAlias),                     /* tp_basicsize */
  0,                                             /* tp_itemsize */
  (destructor) pygmenu_tree_item_dealloc,        /* tp_dealloc */
  (printfunc)0,                                  /* tp_print */
  (getattrfunc)0,                                /* tp_getattr */
  (setattrfunc)0,                                /* tp_setattr */
  (cmpfunc)0,                                    /* tp_compare */
  (reprfunc)0,                                   /* tp_repr */
  0,                                             /* tp_as_number */
  0,                                             /* tp_as_sequence */
  0,                                             /* tp_as_mapping */
  (hashfunc)0,                                   /* tp_hash */
  (ternaryfunc)0,                                /* tp_call */
  (reprfunc)0,                                   /* tp_str */
  (getattrofunc) pygmenu_tree_alias_getattro,    /* tp_getattro */
  (setattrofunc)0,                               /* tp_setattro */
  0,                                             /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                            /* tp_flags */
  NULL,                                          /* Documentation string */
  (traverseproc)0,                               /* tp_traverse */
  (inquiry)0,                                    /* tp_clear */
  (richcmpfunc)0,                                /* tp_richcompare */
  0,                                             /* tp_weaklistoffset */
  (getiterfunc)0,                                /* tp_iter */
  (iternextfunc)0,                               /* tp_iternext */
  pygmenu_tree_alias_methods,                    /* tp_methods */
  0,                                             /* tp_members */
  0,                                             /* tp_getset */
  (PyTypeObject *)0,                             /* tp_base */
  (PyObject *)0,                                 /* tp_dict */
  0,                                             /* tp_descr_get */
  0,                                             /* tp_descr_set */
  0,                                             /* tp_dictoffset */
  (initproc)0,                                   /* tp_init */
  0,                                             /* tp_alloc */
  0,                                             /* tp_new */
  0,                                             /* tp_free */
  (inquiry)0,                                    /* tp_is_gc */
  (PyObject *)0,                                 /* tp_bases */
};

static PyGMenuTreeAlias *
pygmenu_tree_alias_wrap (MenuTreeAlias *alias)
{
  PyGMenuTreeAlias *retval;

  if ((retval = menu_tree_item_get_user_data (MENU_TREE_ITEM (alias))) != NULL)
    {
      Py_INCREF (retval);
      return retval;
    }

  if (!(retval = (PyGMenuTreeAlias *) PyObject_NEW (PyGMenuTreeAlias,
						    &PyGMenuTreeAlias_Type)))
    return NULL;

  retval->item = menu_tree_item_ref (alias);

  menu_tree_item_set_user_data (MENU_TREE_ITEM (alias), retval, NULL);

  return retval;
}

static PyObject *
pygmenu_tree_get_root_directory (PyObject *self,
				 PyObject *args)
{
  PyGMenuTree          *tree;
  MenuTreeDirectory    *directory;
  PyGMenuTreeDirectory *retval;

  if (!PyArg_ParseTuple (args, ":gmenu.Tree.get_root_directory"))
    return NULL;

  tree = (PyGMenuTree *) self;

  directory = menu_tree_get_root_directory (tree->tree);
  if (directory == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = pygmenu_tree_directory_wrap (directory);

  menu_tree_item_unref (directory);

  return (PyObject *) retval;
}

static PyObject *
pygmenu_tree_get_directory_from_path (PyObject *self,
				      PyObject *args)
{
  PyGMenuTree          *tree;
  MenuTreeDirectory    *directory;
  PyGMenuTreeDirectory *retval;
  char                 *path;

  if (!PyArg_ParseTuple (args, "s:gmenu.Tree.get_directory_from_path", &path))
    return NULL;

  tree = (PyGMenuTree *) self;

  directory = menu_tree_get_directory_from_path (tree->tree, path);
  if (directory == NULL)
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = pygmenu_tree_directory_wrap (directory);

  menu_tree_item_unref (directory);

  return (PyObject *) retval;
}

static PyGMenuTreeCallback *
pygmenu_tree_callback_new (PyObject *tree,
			   PyObject *callback,
			   PyObject *user_data)
{
  PyGMenuTreeCallback *retval;

  retval = g_new0 (PyGMenuTreeCallback, 1);

  Py_INCREF (tree);
  retval->tree = tree;

  Py_INCREF (callback);
  retval->callback = callback;

  Py_XINCREF (user_data);
  retval->user_data = user_data;

  return retval;
}

static void
pygmenu_tree_callback_free (PyGMenuTreeCallback *callback)
{
  Py_XDECREF (callback->user_data);
  callback->user_data = NULL;

  Py_DECREF (callback->callback);
  callback->callback = NULL;

  Py_DECREF (callback->tree);
  callback->tree = NULL;

  g_free (callback);
}

static void
pygmenu_tree_handle_monitor_callback (MenuTree            *tree,
				      PyGMenuTreeCallback *callback)
{
  PyObject *args;
  PyObject *ret;

  args = PyTuple_New (callback->user_data ? 2 : 1);

  Py_INCREF (callback->tree);
  PyTuple_SET_ITEM (args, 0, callback->tree);

  if (callback->user_data != NULL)
    {
      Py_INCREF (callback->user_data);
      PyTuple_SET_ITEM (args, 1, callback->user_data);
    }

  ret = PyObject_CallObject (callback->callback, args);

  Py_XDECREF (ret);
  Py_DECREF (args);
}

static PyObject *
pygmenu_tree_add_monitor (PyObject *self,
			  PyObject *args)
{
  PyGMenuTree         *tree;
  PyGMenuTreeCallback *callback;
  PyObject            *pycallback;
  PyObject            *pyuser_data;

  if (!PyArg_ParseTuple (args, "O|O:gmenu.Tree.add_monitor", &pycallback, &pyuser_data))
    return NULL;

  tree = (PyGMenuTree *) self;

  callback = pygmenu_tree_callback_new (self, pycallback, pyuser_data);

  tree->callbacks = g_slist_append (tree->callbacks, callback);

  {
    MenuTreeDirectory *dir = menu_tree_get_root_directory (tree->tree);
    menu_tree_item_unref (dir);
  }

  menu_tree_add_monitor (tree->tree,
			 (MenuTreeChangedFunc) pygmenu_tree_handle_monitor_callback,
			 callback);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
pygmenu_tree_remove_monitor (PyObject *self,
			     PyObject *args)
{

  PyGMenuTree *tree;
  PyObject    *pycallback;
  PyObject    *pyuser_data;
  GSList      *tmp;

  if (!PyArg_ParseTuple (args, "O|O:gmenu.Tree.remove_monitor", &pycallback, &pyuser_data))
    return NULL;

  tree = (PyGMenuTree *) self;

  tmp = tree->callbacks;
  while (tmp != NULL)
    {
      PyGMenuTreeCallback *callback = tmp->data;
      GSList              *next     = tmp->next;

      if (callback->callback  == pycallback &&
	  callback->user_data == pyuser_data)
	{
	  tree->callbacks = g_slist_delete_link (tree->callbacks, tmp);
	  pygmenu_tree_callback_free (callback);
	}

      tmp = next;
    }

  Py_INCREF (Py_None);
  return Py_None;
}

static void
pygmenu_tree_dealloc (PyGMenuTree *self)
{
  g_slist_foreach (self->callbacks,
		   (GFunc) pygmenu_tree_callback_free,
		   NULL);
  g_slist_free (self->callbacks);
  self->callbacks = NULL;

  if (self->tree != NULL)
    menu_tree_unref (self->tree);
  self->tree = NULL;

  PyObject_DEL (self);
}

static struct PyMethodDef pygmenu_tree_methods[] =
{
  { "get_root_directory",      pygmenu_tree_get_root_directory,      METH_VARARGS },
  { "get_directory_from_path", pygmenu_tree_get_directory_from_path, METH_VARARGS },
  { "add_monitor",             pygmenu_tree_add_monitor,             METH_VARARGS },
  { "remove_monitor",          pygmenu_tree_remove_monitor,          METH_VARARGS },
  { NULL,                      NULL,                                 0            }
};

static PyTypeObject PyGMenuTree_Type = 
{
  PyObject_HEAD_INIT(NULL)
  0,                                  /* ob_size */
  "gmenu.Tree",                       /* tp_name */
  sizeof (PyGMenuTree),               /* tp_basicsize */
  0,                                  /* tp_itemsize */
  (destructor) pygmenu_tree_dealloc,  /* tp_dealloc */
  (printfunc)0,                       /* tp_print */
  (getattrfunc)0,                     /* tp_getattr */
  (setattrfunc)0,                     /* tp_setattr */
  (cmpfunc)0,                         /* tp_compare */
  (reprfunc)0,                        /* tp_repr */
  0,                                  /* tp_as_number */
  0,                                  /* tp_as_sequence */
  0,                                  /* tp_as_mapping */
  (hashfunc)0,                        /* tp_hash */
  (ternaryfunc)0,                     /* tp_call */
  (reprfunc)0,                        /* tp_str */
  (getattrofunc)0,                    /* tp_getattro */
  (setattrofunc)0,                    /* tp_setattro */
  0,                                  /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,                 /* tp_flags */
  NULL,                               /* Documentation string */
  (traverseproc)0,                    /* tp_traverse */
  (inquiry)0,                         /* tp_clear */
  (richcmpfunc)0,                     /* tp_richcompare */
  0,                                  /* tp_weaklistoffset */
  (getiterfunc)0,                     /* tp_iter */
  (iternextfunc)0,                    /* tp_iternext */
  pygmenu_tree_methods,               /* tp_methods */
  0,                                  /* tp_members */
  0,                                  /* tp_getset */
  (PyTypeObject *)0,                  /* tp_base */
  (PyObject *)0,                      /* tp_dict */
  0,                                  /* tp_descr_get */
  0,                                  /* tp_descr_set */
  0,                                  /* tp_dictoffset */
  (initproc)0,                        /* tp_init */
  0,                                  /* tp_alloc */
  0,                                  /* tp_new */
  0,                                  /* tp_free */
  (inquiry)0,                         /* tp_is_gc */
  (PyObject *)0,                      /* tp_bases */
};

static PyGMenuTree *
pygmenu_tree_wrap (MenuTree *tree)
{
  PyGMenuTree *retval;

  if ((retval = menu_tree_get_user_data (tree)) != NULL)
    {
      Py_INCREF (retval);
      return retval;
    }

  if (!(retval = (PyGMenuTree *) PyObject_NEW (PyGMenuTree, &PyGMenuTree_Type)))
    return NULL;

  retval->tree = menu_tree_ref (tree);

  menu_tree_set_user_data (tree, retval, NULL);

  return retval;
}

static PyObject *
pygmenu_lookup_tree (PyObject *self,
		     PyObject *args)
{
  char        *menu_file;
  MenuTree    *tree;
  PyGMenuTree *retval;
  int          flags;

  flags = MENU_TREE_FLAGS_NONE;

  if (!PyArg_ParseTuple (args, "s|i:gmenu.lookup_tree", &menu_file, &flags))
    return NULL;

  if (!gnome_vfs_initialized ())
    gnome_vfs_init ();

  if (!(tree = menu_tree_lookup (menu_file, flags)))
    {
      Py_INCREF (Py_None);
      return Py_None;
    }

  retval = pygmenu_tree_wrap (tree);

  menu_tree_unref (tree);

  return (PyObject *) retval;
}

static struct PyMethodDef pygmenu_methods[] =
{
  { "lookup_tree", pygmenu_lookup_tree, METH_VARARGS },
  { NULL,          NULL,                0            }
};

void initgmenu (void);

DL_EXPORT (void)
initgmenu (void)
{
  PyObject *mod;

  mod = Py_InitModule4 ("gmenu", pygmenu_methods, 0, 0, PYTHON_API_VERSION);


#define REGISTER_TYPE(t, n)                          \
  t.ob_type = &PyType_Type;                          \
  PyType_Ready (&t);                                 \
  PyObject_SetAttrString (mod, n, (PyObject *) &t);

  REGISTER_TYPE (PyGMenuTree_Type,     "Tree")
  REGISTER_TYPE (PyGMenuTreeItem_Type, "Item")

#define REGISTER_ITEM_TYPE(t, n)                     \
  t.ob_type = &PyType_Type;                          \
  t.tp_base = &PyGMenuTreeItem_Type;                 \
  PyType_Ready (&t);                                 \
  PyObject_SetAttrString (mod, n, (PyObject *) &t);

  REGISTER_ITEM_TYPE (PyGMenuTreeDirectory_Type, "Directory")
  REGISTER_ITEM_TYPE (PyGMenuTreeEntry_Type,     "Entry")
  REGISTER_ITEM_TYPE (PyGMenuTreeSeparator_Type, "Separator")
  REGISTER_ITEM_TYPE (PyGMenuTreeHeader_Type,    "Header")
  REGISTER_ITEM_TYPE (PyGMenuTreeAlias_Type,     "Alias")

#define REGISTER_ENUM_CONSTANT(p, v, s)              \
  p = PyInt_FromLong (v);                            \
  PyObject_SetAttrString (mod, s, p);

  REGISTER_ENUM_CONSTANT (pygmenu_tree_item_type_invalid,   MENU_TREE_ITEM_INVALID,   "TYPE_INVALID")
  REGISTER_ENUM_CONSTANT (pygmenu_tree_item_type_directory, MENU_TREE_ITEM_DIRECTORY, "TYPE_DIRECTORY")
  REGISTER_ENUM_CONSTANT (pygmenu_tree_item_type_entry,     MENU_TREE_ITEM_ENTRY,     "TYPE_ENTRY")
  REGISTER_ENUM_CONSTANT (pygmenu_tree_item_type_separator, MENU_TREE_ITEM_SEPARATOR, "TYPE_SEPARATOR")
  REGISTER_ENUM_CONSTANT (pygmenu_tree_item_type_header,    MENU_TREE_ITEM_HEADER,    "TYPE_HEADER")
  REGISTER_ENUM_CONSTANT (pygmenu_tree_item_type_alias,     MENU_TREE_ITEM_ALIAS,     "TYPE_ALIAS")

  REGISTER_ENUM_CONSTANT (pygmenu_tree_flags_none,             MENU_TREE_FLAGS_NONE,             "FLAGS_NONE")
  REGISTER_ENUM_CONSTANT (pygmenu_tree_flags_include_excluded, MENU_TREE_FLAGS_INCLUDE_EXCLUDED, "FLAGS_INCLUDE_EXCLUDED")
  REGISTER_ENUM_CONSTANT (pygmenu_tree_flags_show_empty,       MENU_TREE_FLAGS_SHOW_EMPTY,       "FLAGS_SHOW_EMPTY")
}
