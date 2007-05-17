/* Return the canonical absolute name of a given file.
   Copyright (C) 1996-2001, 2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   Copyright (C) 2002 Red Hat, Inc. (trivial port to GLib)

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <config.h>

#include "canonicalize.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <stddef.h>

/* Return the canonical absolute name of file NAME.  A canonical name
   does not contain any `.', `..' components nor any repeated path
   separators ('/') or symlinks.  All path components must exist.  If
   RESOLVED is null, the result is malloc'd; otherwise, if the
   canonical name is PATH_MAX chars or more, returns null with `errno'
   set to ENAMETOOLONG; if the name fits in fewer than PATH_MAX chars,
   returns the name in RESOLVED.  If the name cannot be resolved and
   RESOLVED is non-NULL, it contains the path of the first component
   that cannot be resolved.  If the path can be resolved, RESOLVED
   holds the same value as the value returned.  */

static char*
menu_realpath (const char *name, char *resolved)
{
  char *rpath, *dest, *extra_buf = NULL;
  const char *start, *end, *rpath_limit;
  long int path_max;
  int num_links = 0;

  if (name == NULL)
    {
      /* As per Single Unix Specification V2 we must return an error if
         either parameter is a null pointer.  We extend this to allow
         the RESOLVED parameter to be NULL in case the we are expected to
         allocate the room for the return value.  */
      errno = EINVAL;
      return NULL;
    }

  if (name[0] == '\0')
    {
      /* As per Single Unix Specification V2 we must return an error if
         the name argument points to an empty string.  */
      errno = ENOENT;
      return NULL;
    }

#ifdef PATH_MAX
  path_max = PATH_MAX;
#else
  path_max = pathconf (name, _PC_PATH_MAX);
  if (path_max <= 0)
    path_max = 1024;
#endif

  rpath = resolved ? g_alloca (path_max) : g_malloc (path_max);
  rpath_limit = rpath + path_max;

  if (name[0] != G_DIR_SEPARATOR)
    {
      if (!getcwd (rpath, path_max))
        {
          rpath[0] = '\0';
          goto error;
        }
      dest = strchr (rpath, '\0');
    }
  else
    {
      rpath[0] = G_DIR_SEPARATOR;
      dest = rpath + 1;
    }

  for (start = end = name; *start; start = end)
    {
      struct stat st;
      int n;

      /* Skip sequence of multiple path-separators.  */
      while (*start == G_DIR_SEPARATOR)
        ++start;

      /* Find end of path component.  */
      for (end = start; *end && *end != G_DIR_SEPARATOR; ++end)
        /* Nothing.  */;

      if (end - start == 0)
        break;
      else if (end - start == 1 && start[0] == '.')
        /* nothing */;
      else if (end - start == 2 && start[0] == '.' && start[1] == '.')
        {
          /* Back up to previous component, ignore if at root already.  */
          if (dest > rpath + 1)
            while ((--dest)[-1] != G_DIR_SEPARATOR);
        }
      else
        {
          size_t new_size;

          if (dest[-1] != G_DIR_SEPARATOR)
            *dest++ = G_DIR_SEPARATOR;

          if (dest + (end - start) >= rpath_limit)
            {
              ptrdiff_t dest_offset = dest - rpath;
              char *new_rpath;

              if (resolved)
                {
#ifdef ENAMETOOLONG
                  errno = ENAMETOOLONG;
#else
                  /* Uh... just pick something */
                  errno = EINVAL;
#endif
                  if (dest > rpath + 1)
                    dest--;
                  *dest = '\0';
                  goto error;
                }
              new_size = rpath_limit - rpath;
              if (end - start + 1 > path_max)
                new_size += end - start + 1;
              else
                new_size += path_max;
              new_rpath = (char *) realloc (rpath, new_size);
              if (new_rpath == NULL)
                goto error;
              rpath = new_rpath;
              rpath_limit = rpath + new_size;

              dest = rpath + dest_offset;
            }

          memcpy (dest, start, end - start);
          dest = dest + (end - start);
          *dest = '\0';

          if (stat (rpath, &st) < 0)
            goto error;

          if (S_ISLNK (st.st_mode))
            {
              char *buf = alloca (path_max);
              size_t len;

              if (++num_links > MAXSYMLINKS)
                {
                  errno = ELOOP;
                  goto error;
                }

              n = readlink (rpath, buf, path_max);
              if (n < 0)
                goto error;
              buf[n] = '\0';

              if (!extra_buf)
                extra_buf = g_alloca (path_max);

              len = strlen (end);
              if ((long int) (n + len) >= path_max)
                {
#ifdef ENAMETOOLONG
                  errno = ENAMETOOLONG;
#else
                  /* Uh... just pick something */
                  errno = EINVAL;
#endif
                  goto error;
                }

              /* Careful here, end may be a pointer into extra_buf... */
              g_memmove (&extra_buf[n], end, len + 1);
              name = end = memcpy (extra_buf, buf, n);

              if (buf[0] == G_DIR_SEPARATOR)
                dest = rpath + 1;       /* It's an absolute symlink */
              else
                /* Back up to previous component, ignore if at root already: */
                if (dest > rpath + 1)
                  while ((--dest)[-1] != G_DIR_SEPARATOR);
            }
        }
    }
  if (dest > rpath + 1 && dest[-1] == G_DIR_SEPARATOR)
    --dest;
  *dest = '\0';

  return resolved ? memcpy (resolved, rpath, dest - rpath + 1) : rpath;

error:
  if (resolved)
    strcpy (resolved, rpath);
  else
    g_free (rpath);
  return NULL;
}

char *
menu_canonicalize_file_name (const char *name,
                             gboolean    allow_missing_basename)
{
  char *retval;

  retval = menu_realpath (name, NULL);

  /* We could avoid some system calls by using the second
   * argument to realpath() instead of doing realpath
   * all over again, but who cares really. we'll see if
   * it's ever in a profile.
   */
  if (allow_missing_basename && retval == NULL)
    {
      char *dirname;
      char *canonical_dirname;
      dirname = g_path_get_dirname (name);
      canonical_dirname = menu_realpath (dirname, NULL);
      g_free (dirname);
      if (canonical_dirname)
        {
          char *basename;
          basename = g_path_get_basename (name);
          retval = g_build_filename (canonical_dirname, basename, NULL);
          g_free (basename);
          g_free (canonical_dirname);
        }
    }

  return retval;
}
