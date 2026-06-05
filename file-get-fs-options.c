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
#include <argz.h>
#include <hurd/iohelp.h>

error_t
S_file_get_fs_options (struct protid *pi, char **options,
                       mach_msg_type_number_t *options_size)
{
  error_t err;
  char *argz = 0;
  size_t argz_len = 0;

  if (!pi)
    return EOPNOTSUPP;

  err = argz_add (&argz, &argz_len, program_invocation_name);
  if (err)
    return err;

  err = p9_append_args (&argz, &argz_len);
  if (err)
    {
      free (argz);
      return err;
    }

  return iohelp_return_malloced_buffer (argz, argz_len,
                                        options, options_size);
}
