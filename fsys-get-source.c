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
#include "fsys_S.h"
#include <string.h>
#include <errno.h>

error_t
S_fsys_get_source (struct port_info *control,
                   char *source)
{
  error_t err;

  if (control != p9_control)
    return EOPNOTSUPP;

  strncpy (source, p9_source, 1024);
  return 0;
}
