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
#include "io_S.h"
#include <fcntl.h>
#include <errno.h>
#include <mach.h>

error_t
S_io_clear_some_openmodes (struct protid *protid,
                           int bits)
{
  struct peropen *po;
  struct node *np;

  if (protid == NULL)
    return EOPNOTSUPP;

  po = protid->po;
  np = po->np;

  pthread_mutex_lock (&np->lock);
  po->user_open_flags &= ~(bits & HONORED_STATE_MODES);
  pthread_mutex_unlock (&np->lock);

  return 0;
}
