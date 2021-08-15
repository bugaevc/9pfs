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
#include "io_S.h"
#include <errno.h>

error_t
S_io_select (struct protid *protid, int *select_type)
{
  if (!protid)
    return EOPNOTSUPP;

  /* We always pretend to be ready to do any sort of I/O.
     This is a blatant lie for devices and sockets, but 9P
     doesn't seem to provide us with any means to select without
     actually performing the I/O.  */
  *select_type &= ~SELECT_URG;
  return 0;
}
