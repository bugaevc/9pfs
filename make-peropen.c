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

struct peropen *
p9_make_peropen (struct node *np, int flags,
                 struct peropen *context)
{
  error_t err;
  struct peropen *po;

  po = calloc (1, sizeof (struct peropen));
  if (!po)
    return NULL;

  refcount_init (&po->refcount, 1);
  po->np = np;
  p9_nref (np);
  po->user_open_flags = flags;

  if (context)
    {
      po->root_qid_path = context->root_qid_path;
      po->root_parent = context->root_parent;
      if (MACH_PORT_VALID (po->root_parent))
        mach_port_mod_refs (mach_task_self (), po->root_parent,
                            MACH_PORT_RIGHT_SEND, 1);
    }

  return po;
}
