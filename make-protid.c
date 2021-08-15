/* 9P translator

   Copyright (C) 2021 Sergey Bugaev <bugaevc@gmail.com>

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
#include <string.h>

struct protid *
p9_make_protid (struct peropen *po)
{
  error_t err;
  struct protid *pi;

  err = ports_create_port (p9_protid_class, p9_bucket,
                           sizeof (struct protid), &pi);
  if (err)
    {
      errno = err;
      return NULL;
    }

  /* TODO */
  pi->user = NULL;
  /* Consume the reference.  */
  pi->po = po;
  pi->walk_fid = pi->io_fid = P9_NO_FID;
  pi->server_open_flags = 0;
  memset (&pi->dir, 0, sizeof (pi->dir));

  return pi;
}
