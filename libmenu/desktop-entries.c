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

#include "desktop-entries.h"

#include <string.h>

#include "menu-util.h"

#define DESKTOP_ENTRY_GROUP     "Desktop Entry"
#define KDE_DESKTOP_ENTRY_GROUP "KDE Desktop Entry"

struct DesktopEntry
{
  guint refcount;

  char *path;
  const char *basename;

  char     *name;
  char     *comment;
  char     *icon;

  guint    type              : 2;
  guint    show_in_gnome     : 1;
  guint    nodisplay         : 1;
  guint    hidden            : 1;
  guint    reserved          : 27;
};

typedef struct 
{
  DesktopEntry base;

  GQuark   *categories;

  char     *generic_name;
  char     *full_name;
  char     *exec;
  char     *try_exec;

  guint    terminal          : 1;
  guint    tryexec_evaluated : 1;
  guint    tryexec_failed    : 1;
  guint    reserved          : 29;
} DesktopEntryDesktop;

typedef struct
{
  DesktopEntry base;
} DesktopEntryDirectory;

struct DesktopEntrySet
{
  int         refcount;
  GHashTable *hash;
};

/*
 * Desktop entries
 */

/**
 * unix_basename_from_path:
 * @path: Path string
 * 
 * Returns: A constant pointer into the basename of @path 
 */
static const char *
unix_basename_from_path (const char *path)
{
  const char *basename = g_strrstr (path, "/");
  if (basename)
    return basename + 1;
  else
    return path;
}

static gboolean
key_file_get_show_in_gnome (GKeyFile  *key_file,
			    const char  *desktop_entry_group)
{
  gchar **strv;
  gboolean show_in_gnome = TRUE;
  int i;

  strv = g_key_file_get_string_list (key_file,
                                     desktop_entry_group,
                                     "OnlyShowIn",
                                     NULL,
                                     NULL);
  if (strv)
    {
      show_in_gnome = FALSE;
      for (i = 0; strv[i]; i++)
        {
          if (!strcmp (strv[i], "GNOME"))
            {
              show_in_gnome = TRUE;
              break;
            }
        }
    }
  else
    {
      strv = g_key_file_get_string_list (key_file,
                                         desktop_entry_group,
                                         "NotShowIn",
                                         NULL,
                                         NULL);
      if (strv)
        {
          show_in_gnome = TRUE;
          for (i = 0; strv[i]; i++)
            {
              if (!strcmp (strv[i], "GNOME"))
                {
                  show_in_gnome = FALSE;
                }
            }
        }
    }
  g_strfreev (strv);
  
  return show_in_gnome;
}

static GQuark *
get_categories_from_key_file (GKeyFile     *key_file,
                              const char   *desktop_entry_group)
{
  GQuark  *retval;
  char   **strv;
  gsize    len;
  int      i;

  strv = g_key_file_get_string_list (key_file,
                                     desktop_entry_group,
                                     "Categories",
                                     &len,
                                     NULL);
  if (!strv)
    return NULL;

  retval = g_new0 (GQuark, len + 1);

  for (i = 0; strv[i]; i++)
    retval[i] = g_quark_from_string (strv[i]);

  g_strfreev (strv);

  return retval;
}

static gboolean
desktop_entry_load_base (DesktopEntry *entry,
			 GKeyFile     *key_file,
			 const char   *desktop_entry_group,
			 GError      **error)
{
  char *type_str;

  type_str = g_key_file_get_string (key_file, desktop_entry_group, "Type", NULL);
  if (!type_str)
    {
      g_set_error (error,
		   G_KEY_FILE_ERROR,
		   G_KEY_FILE_ERROR_INVALID_VALUE,
		   "\"%s\" contains no \"Type\" key\n", entry->path);
      return FALSE;
    }

  if ((entry->type == DESKTOP_ENTRY_DESKTOP && strcmp (type_str, "Application") != 0) ||
      (entry->type == DESKTOP_ENTRY_DIRECTORY && strcmp (type_str, "Directory") != 0))
    {
      g_set_error (error,
		   G_KEY_FILE_ERROR,
		   G_KEY_FILE_ERROR_INVALID_VALUE,
		   "\"%s\" does not contain the correct \"Type\" value\n", entry->path);
      g_free (type_str);
      return FALSE;
    }

  g_free (type_str);

  entry->name         = g_key_file_get_locale_string (key_file, desktop_entry_group, "Name", NULL, error);
  if (entry->name == NULL)
    return FALSE;

  entry->comment      = g_key_file_get_locale_string (key_file, desktop_entry_group, "Comment", NULL, NULL);
  entry->icon         = g_key_file_get_locale_string (key_file, desktop_entry_group, "Icon", NULL, NULL);
  entry->nodisplay    = g_key_file_get_boolean (key_file,
						 desktop_entry_group,
						 "NoDisplay",
						 NULL);
  entry->hidden       = g_key_file_get_boolean (key_file,
						 desktop_entry_group,
						 "Hidden",
						 NULL);
  entry->show_in_gnome = key_file_get_show_in_gnome (key_file, desktop_entry_group);

  return TRUE;
}

static gboolean
desktop_entry_load_desktop (DesktopEntry  *entry,
			    GKeyFile      *key_file,
			    const char    *desktop_entry_group,
			    GError       **error)
{
  DesktopEntryDesktop *desktop_entry;

  desktop_entry = (DesktopEntryDesktop*)entry;

  desktop_entry->exec = g_key_file_get_string (key_file, desktop_entry_group, "Exec", error);

  if (desktop_entry->exec == NULL)
    return FALSE;

  desktop_entry->categories   = get_categories_from_key_file (key_file, desktop_entry_group);
  desktop_entry->generic_name = g_key_file_get_locale_string (key_file, desktop_entry_group, "GenericName", NULL, NULL);
  desktop_entry->full_name    = g_key_file_get_locale_string (key_file, desktop_entry_group, "X-GNOME-FullName", NULL, NULL);
  desktop_entry->try_exec     = g_key_file_get_string (key_file,
						       desktop_entry_group,
						       "TryExec",
						       NULL);
  /* why are we stripping tryexec but not exec? */
  if (desktop_entry->try_exec != NULL)
    desktop_entry->try_exec = g_strstrip (desktop_entry->try_exec);
  desktop_entry->terminal = g_key_file_get_boolean (key_file, desktop_entry_group, "Terminal", NULL);

  return TRUE;
}

static gboolean
desktop_entry_load (DesktopEntry *entry)
{
  GKeyFile         *key_file;
  GError           *error = NULL;
  gboolean          retval = FALSE;
  const char       *desktop_entry_group;

  key_file = g_key_file_new ();

  if (!g_key_file_load_from_file (key_file, entry->path, 0, &error))
    goto out;

  if (g_key_file_has_group (key_file, DESKTOP_ENTRY_GROUP))
    desktop_entry_group = DESKTOP_ENTRY_GROUP;
  else
    {
      if (g_key_file_has_group (key_file, KDE_DESKTOP_ENTRY_GROUP))
	desktop_entry_group = KDE_DESKTOP_ENTRY_GROUP;
      else
	{
	  g_set_error (&error,
		       G_KEY_FILE_ERROR,
		       G_KEY_FILE_ERROR_INVALID_VALUE,
		       "Desktop file does not have Desktop group");
	  goto out;
	}
    }

  if (!desktop_entry_load_base (entry, key_file, desktop_entry_group, &error))
    goto out;

  if (entry->type == DESKTOP_ENTRY_DESKTOP)
    {
      if (!desktop_entry_load_desktop (entry, key_file, desktop_entry_group, &error))
	goto out;
    }
  else if (entry->type == DESKTOP_ENTRY_DIRECTORY)
    {
    }
  else
    g_assert_not_reached ();
  
  retval = TRUE;
 out:
  if (!retval)
    {
      menu_verbose ("Failed to load \"%s\": %s\n",
		    entry->path, error->message);
      g_clear_error (&error);
    }
  g_key_file_free (key_file);
  return retval;
}

DesktopEntry *
desktop_entry_new (const char *path)
{
  DesktopEntryType  type;
  DesktopEntry     *retval;

  menu_verbose ("Loading desktop entry \"%s\"\n", path);

  if (g_str_has_suffix (path, ".desktop"))
    {
      type = DESKTOP_ENTRY_DESKTOP;
      retval = (DesktopEntry*)g_new0 (DesktopEntryDesktop, 1);
    }
  else if (g_str_has_suffix (path, ".directory"))
    {
      type = DESKTOP_ENTRY_DIRECTORY;
      retval = (DesktopEntry*)g_new0 (DesktopEntryDirectory, 1);
    }
  else
    {
      menu_verbose ("Unknown desktop entry suffix in \"%s\"\n",
                    path);
      return NULL;
    }

  retval->refcount = 1;
  retval->type     = type;
  retval->path     = g_strdup (path);
  retval->basename = unix_basename_from_path (retval->path);

  if (!desktop_entry_load (retval))
    {
      desktop_entry_unref (retval);
      return NULL;
    }

  return retval;
}

DesktopEntry *
desktop_entry_reload (DesktopEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  menu_verbose ("Re-loading desktop entry \"%s\"\n", entry->path);

  g_free (entry->name);
  entry->name = NULL;

  g_free (entry->comment);
  entry->comment = NULL;

  g_free (entry->icon);
  entry->icon = NULL;

  if (entry->type == DESKTOP_ENTRY_DESKTOP)
    {
      DesktopEntryDesktop *entry_desktop = (DesktopEntryDesktop *) entry;

      g_free (entry_desktop->categories);
      entry_desktop->categories = NULL;

      g_free (entry_desktop->generic_name);
      entry_desktop->generic_name = NULL;

      g_free (entry_desktop->full_name);
      entry_desktop->full_name = NULL;

      g_free (entry_desktop->exec);
      entry_desktop->exec = NULL;

      g_free (entry_desktop->try_exec);
      entry_desktop->try_exec = NULL;
    }
  else if (entry->type == DESKTOP_ENTRY_DIRECTORY)
    {
    }
  else
    g_assert_not_reached ();

  if (!desktop_entry_load (entry))
    {
      desktop_entry_unref (entry);
      return NULL;
    }
  return entry;
}

DesktopEntry *
desktop_entry_ref (DesktopEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);
  g_return_val_if_fail (entry->refcount > 0, NULL);

  entry->refcount += 1;

  return entry;
}

DesktopEntry *
desktop_entry_copy (DesktopEntry *entry)
{
  DesktopEntry *retval;
  int           i;

  menu_verbose ("Copying desktop entry \"%s\"\n",
                entry->basename);

  if (entry->type == DESKTOP_ENTRY_DESKTOP)
    retval = (DesktopEntry*)g_new0 (DesktopEntryDesktop, 1);
  else if (entry->type == DESKTOP_ENTRY_DIRECTORY)
    retval = (DesktopEntry*)g_new0 (DesktopEntryDirectory, 1);
  else
    g_assert_not_reached ();

  retval->refcount     = 1;
  retval->type         = entry->type;
  retval->path         = g_strdup (entry->path);
  retval->basename     = unix_basename_from_path (retval->path);
  retval->name         = g_strdup (entry->name);
  retval->comment      = g_strdup (entry->comment);
  retval->icon         = g_strdup (entry->icon);

  retval->show_in_gnome     = entry->show_in_gnome; 
  retval->nodisplay         = entry->nodisplay;
  retval->hidden            = entry->hidden;

  if (retval->type == DESKTOP_ENTRY_DESKTOP)
    {
      DesktopEntryDesktop *desktop_entry = (DesktopEntryDesktop*) entry;
      DesktopEntryDesktop *retval_desktop_entry = (DesktopEntryDesktop*) retval;

      retval_desktop_entry->generic_name = g_strdup (desktop_entry->generic_name);
      retval_desktop_entry->full_name    = g_strdup (desktop_entry->full_name);
      retval_desktop_entry->exec         = g_strdup (desktop_entry->exec);
      retval_desktop_entry->try_exec     = g_strdup (desktop_entry->try_exec);
      retval_desktop_entry->terminal          = desktop_entry->terminal;
      retval_desktop_entry->tryexec_evaluated = desktop_entry->tryexec_evaluated;
      retval_desktop_entry->tryexec_failed    = desktop_entry->tryexec_failed;
      
      i = 0;
      if (desktop_entry->categories != NULL)
	{
	  for (; desktop_entry->categories[i]; i++);
	}

      retval_desktop_entry->categories = g_new0 (GQuark, i + 1);
      
      i = 0;
      if (desktop_entry->categories != NULL)
	{
	  for (; desktop_entry->categories[i]; i++)
	    retval_desktop_entry->categories[i] = desktop_entry->categories[i];
	}
    }

  return retval;
}

void
desktop_entry_unref (DesktopEntry *entry)
{
  g_return_if_fail (entry != NULL);
  g_return_if_fail (entry->refcount > 0);

  entry->refcount -= 1;
  if (entry->refcount != 0)
    return;

  g_free (entry->path);
  entry->path = NULL;
  
  g_free (entry->name);
  entry->name = NULL;

  g_free (entry->comment);
  entry->comment = NULL;

  g_free (entry->icon);
  entry->icon = NULL;


  if (entry->type == DESKTOP_ENTRY_DESKTOP)
    {
      DesktopEntryDesktop *desktop_entry = (DesktopEntryDesktop*) entry;
      g_free (desktop_entry->categories);
      g_free (desktop_entry->generic_name);
      g_free (desktop_entry->full_name);
      g_free (desktop_entry->exec);
      g_free (desktop_entry->try_exec);
    }
  else if (entry->type == DESKTOP_ENTRY_DIRECTORY)
    {
    }
  else
    g_assert_not_reached ();

  g_free (entry);
}

DesktopEntryType
desktop_entry_get_type (DesktopEntry *entry)
{
  return entry->type;
}

const char *
desktop_entry_get_path (DesktopEntry *entry)
{
  return entry->path;
}

const char *
desktop_entry_get_basename (DesktopEntry *entry)
{
  return entry->basename;
}

const char *
desktop_entry_get_name (DesktopEntry *entry)
{
  return entry->name;
}

const char *
desktop_entry_get_generic_name (DesktopEntry *entry)
{
  if (entry->type != DESKTOP_ENTRY_DESKTOP)
    return NULL;
  return ((DesktopEntryDesktop*)entry)->generic_name;
}

const char *
desktop_entry_get_full_name (DesktopEntry *entry)
{
  if (entry->type != DESKTOP_ENTRY_DESKTOP)
    return NULL;
  return ((DesktopEntryDesktop*)entry)->full_name;
}

const char *
desktop_entry_get_comment (DesktopEntry *entry)
{
  return entry->comment;
}

const char *
desktop_entry_get_icon (DesktopEntry *entry)
{
  return entry->icon;
}

const char *
desktop_entry_get_exec (DesktopEntry *entry)
{
  if (entry->type != DESKTOP_ENTRY_DESKTOP)
    return NULL;
  return ((DesktopEntryDesktop*)entry)->exec;
}

gboolean
desktop_entry_get_launch_in_terminal (DesktopEntry *entry)
{
  if (entry->type != DESKTOP_ENTRY_DESKTOP)
    return FALSE;
  return ((DesktopEntryDesktop*)entry)->terminal;
}

gboolean
desktop_entry_get_hidden (DesktopEntry *entry)
{
  return entry->hidden;
}

gboolean
desktop_entry_get_no_display (DesktopEntry *entry)
{
  return entry->nodisplay;
}

gboolean
desktop_entry_get_show_in_gnome (DesktopEntry *entry)
{
  return entry->show_in_gnome;
}

gboolean
desktop_entry_get_tryexec_failed (DesktopEntry *entry)
{
  DesktopEntryDesktop *desktop_entry;

  if (entry->type != DESKTOP_ENTRY_DESKTOP)
    return FALSE;
  desktop_entry = (DesktopEntryDesktop*) entry;

  if (desktop_entry->try_exec == NULL)
    return FALSE;

  if (!desktop_entry->tryexec_evaluated)
    {
      char *path;

      desktop_entry->tryexec_evaluated = TRUE;

      path = g_find_program_in_path (desktop_entry->try_exec);

      desktop_entry->tryexec_failed = (path == NULL);

      g_free (path);
    }

  return desktop_entry->tryexec_failed;
}

gboolean
desktop_entry_has_categories (DesktopEntry *entry)
{
  DesktopEntryDesktop *desktop_entry;
  if (entry->type != DESKTOP_ENTRY_DESKTOP)
    return FALSE;

  desktop_entry = (DesktopEntryDesktop*) entry;
  return (desktop_entry->categories != NULL && desktop_entry->categories[0] != 0);
}

gboolean
desktop_entry_has_category (DesktopEntry *entry,
                            const char   *category)
{
  GQuark quark;
  int    i;
  DesktopEntryDesktop *desktop_entry;

  if (entry->type != DESKTOP_ENTRY_DESKTOP)
    return FALSE;

  desktop_entry = (DesktopEntryDesktop*) entry;

  if (desktop_entry->categories == NULL)
    return FALSE;

  if (!(quark = g_quark_try_string (category)))
    return FALSE;

  for (i = 0; desktop_entry->categories[i]; i++)
    {
      if (quark == desktop_entry->categories[i])
        return TRUE;
    }

  return FALSE;
}

void
desktop_entry_add_legacy_category (DesktopEntry *entry)
{
  GQuark *categories;
  int     i;
  DesktopEntryDesktop *desktop_entry;

  g_return_if_fail (entry->type == DESKTOP_ENTRY_DESKTOP);

  desktop_entry = (DesktopEntryDesktop*) entry;

  menu_verbose ("Adding Legacy category to \"%s\"\n",
                entry->basename);

  i = 0;
  if (desktop_entry->categories != NULL)
    {
      for (; desktop_entry->categories[i]; i++);
    }

  categories = g_new0 (GQuark, i + 2);

  i = 0;
  if (desktop_entry->categories != NULL)
    {
      for (; desktop_entry->categories[i]; i++)
        categories[i] = desktop_entry->categories[i];
    }

  categories[i] = g_quark_from_string ("Legacy");

  g_free (desktop_entry->categories);
  desktop_entry->categories = categories;
}

/*
 * Entry sets
 */

DesktopEntrySet *
desktop_entry_set_new (void)
{
  DesktopEntrySet *set;

  set = g_new0 (DesktopEntrySet, 1);
  set->refcount = 1;

  menu_verbose (" New entry set %p\n", set);

  return set;
}

DesktopEntrySet *
desktop_entry_set_ref (DesktopEntrySet *set)
{
  g_return_val_if_fail (set != NULL, NULL);
  g_return_val_if_fail (set->refcount > 0, NULL);

  set->refcount += 1;

  return set;
}

void
desktop_entry_set_unref (DesktopEntrySet *set)
{
  g_return_if_fail (set != NULL);
  g_return_if_fail (set->refcount > 0);

  set->refcount -= 1;
  if (set->refcount == 0)
    {
      menu_verbose (" Deleting entry set %p\n", set);

      if (set->hash)
        g_hash_table_destroy (set->hash);
      set->hash = NULL;

      g_free (set);
    }
}

void
desktop_entry_set_add_entry (DesktopEntrySet *set,
                             DesktopEntry    *entry,
                             const char      *file_id)
{
  menu_verbose (" Adding to set %p entry %s\n", set, file_id);

  if (set->hash == NULL)
    {
      set->hash = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         (GDestroyNotify) desktop_entry_unref);
    }

  g_hash_table_replace (set->hash,
                        g_strdup (file_id),
                        desktop_entry_ref (entry));
}

DesktopEntry *
desktop_entry_set_lookup (DesktopEntrySet *set,
                          const char      *file_id)
{
  if (set->hash == NULL)
    return NULL;

  return g_hash_table_lookup (set->hash, file_id);
}

typedef struct
{
  DesktopEntrySetForeachFunc func;
  gpointer                   user_data;
} EntryHashForeachData;

static void
entry_hash_foreach (const char           *file_id,
                    DesktopEntry         *entry,
                    EntryHashForeachData *fd)
{
  fd->func (file_id, entry, fd->user_data);
}

void
desktop_entry_set_foreach (DesktopEntrySet            *set,
                           DesktopEntrySetForeachFunc  func,
                           gpointer                    user_data)
{
  g_return_if_fail (set != NULL);
  g_return_if_fail (func != NULL);

  if (set->hash != NULL)
    {
      EntryHashForeachData fd;

      fd.func      = func;
      fd.user_data = user_data;

      g_hash_table_foreach (set->hash,
                            (GHFunc) entry_hash_foreach,
                            &fd);
    }
}

static void
desktop_entry_set_clear (DesktopEntrySet *set)
{
  menu_verbose (" Clearing set %p\n", set);

  if (set->hash != NULL)
    {
      g_hash_table_destroy (set->hash);
      set->hash = NULL;
    }
}

int
desktop_entry_set_get_count (DesktopEntrySet *set)
{
  if (set->hash == NULL)
    return 0;

  return g_hash_table_size (set->hash);
}

static void
union_foreach (const char      *file_id,
               DesktopEntry    *entry,
               DesktopEntrySet *set)
{
  /* we are iterating over "with" adding anything not
   * already in "set". We unconditionally overwrite
   * the stuff in "set" because we can assume
   * two entries with the same name are equivalent.
   */
  desktop_entry_set_add_entry (set, entry, file_id);
}

void
desktop_entry_set_union (DesktopEntrySet *set,
                         DesktopEntrySet *with)
{
  menu_verbose (" Union of %p and %p\n", set, with);

  if (desktop_entry_set_get_count (with) == 0)
    return; /* A fast simple case */

  g_hash_table_foreach (with->hash,
                        (GHFunc) union_foreach,
                        set);
}

typedef struct
{
  DesktopEntrySet *set;
  DesktopEntrySet *with;
} IntersectData;

static gboolean
intersect_foreach_remove (const char    *file_id,
                          DesktopEntry  *entry,
                          IntersectData *id)
{
  /* Remove everything in "set" which is not in "with" */

  if (g_hash_table_lookup (id->with->hash, file_id) != NULL)
    return FALSE;

  menu_verbose (" Removing from %p entry %s\n", id->set, file_id);

  return TRUE; /* return TRUE to remove */
}

void
desktop_entry_set_intersection (DesktopEntrySet *set,
                                DesktopEntrySet *with)
{
  IntersectData id;

  menu_verbose (" Intersection of %p and %p\n", set, with);

  if (desktop_entry_set_get_count (set) == 0 ||
      desktop_entry_set_get_count (with) == 0)
    {
      /* A fast simple case */
      desktop_entry_set_clear (set);
      return;
    }

  id.set  = set;
  id.with = with;

  g_hash_table_foreach_remove (set->hash,
                               (GHRFunc) intersect_foreach_remove,
                               &id);
}

typedef struct
{
  DesktopEntrySet *set;
  DesktopEntrySet *other;
} SubtractData;

static gboolean
subtract_foreach_remove (const char   *file_id,
                         DesktopEntry *entry,
                         SubtractData *sd)
{
  /* Remove everything in "set" which is not in "other" */

  if (g_hash_table_lookup (sd->other->hash, file_id) == NULL)
    return FALSE;

  menu_verbose (" Removing from %p entry %s\n", sd->set, file_id);

  return TRUE; /* return TRUE to remove */
}

void
desktop_entry_set_subtract (DesktopEntrySet *set,
                            DesktopEntrySet *other)
{
  SubtractData sd;

  menu_verbose (" Subtract from %p set %p\n", set, other);

  if (desktop_entry_set_get_count (set) == 0 ||
      desktop_entry_set_get_count (other) == 0)
    return; /* A fast simple case */

  sd.set   = set;
  sd.other = other;

  g_hash_table_foreach_remove (set->hash,
                               (GHRFunc) subtract_foreach_remove,
                               &sd);
}

void
desktop_entry_set_swap_contents (DesktopEntrySet *a,
                                 DesktopEntrySet *b)
{
  GHashTable *tmp;

  menu_verbose (" Swap contents of %p and %p\n", a, b);

  tmp = a->hash;
  a->hash = b->hash;
  b->hash = tmp;
}
