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
#include "fs_S.h"
#include <errno.h>

error_t
S_file_get_storage_info (struct protid *pi, mach_port_t **ports,
                         mach_msg_type_name_t *ports_type,
                         mach_msg_type_number_t *ports_size,
                         int **ints,
                         mach_msg_type_number_t *ints_size,
                         loff_t **offsets,
                         mach_msg_type_number_t *offsets_size,
                         char **data,
                         mach_msg_type_number_t *data_size)
{
  if (!pi)
    return EOPNOTSUPP;

  /* TODO */
  return EROFS;
}
