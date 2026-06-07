/* 9P translator

   Copyright (C) 2021-2026 Sergey Bugaev <bugaevc@gmail.com>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>. */

/* Proudly written in GNU nano. */

#define _GNU_SOURCE 1

#include "9pfs.h"
#include <refcount.h>

struct node *
p9_make_node (void)
{
  error_t err;
  struct node *np;

  np = calloc (1, sizeof (struct node));
  if (!np)
    return NULL;

  refcounts_init (&np->refcounts, 1, 0);
  err = pthread_mutex_init (&np->lock, NULL);
  if (err)
    {
      errno = err;
      free (np);
      return NULL;
    }

  /* Start with the overall limit.  */
  np->max_message_size = p9_max_message_size;

  return np;
}
