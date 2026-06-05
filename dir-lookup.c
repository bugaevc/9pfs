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
#include "9p-rpc.h"
#include "fs_S.h"
#include <string.h>
#include <errno.h>
#include <hurd/iohelp.h>

#define MAX_PARTS 16

error_t
S_dir_lookup (struct protid *pi, const char *name, int flags,
              mode_t mode, retry_type *do_retry, char *retry_name,
              mach_port_t *result, mach_msg_type_name_t *result_type)
{
  error_t err;
  const char *part, *slash;
  uint16_t n_parts = 0;
  const char *parts[MAX_PARTS + 1];
  int mustbedir = 0;
  uint16_t n_qids;
  struct p9_qid *qids = NULL;
  struct node *nnp;
  uint32_t prev_fid, next_fid;
  struct peropen *npo;
  struct protid *npi = NULL;
  struct iouser *ncred;

  if (!pi)
    return EOPNOTSUPP;

  prev_fid = pi->walk_fid;

  while (*name == '/')
    name++;

  part = name;
  do
    {
      slash = strchrnul (part, '/');
      if ((slash == part) || (slash == part + 1 && *part == '.'))
        {
          /* The part is either empty or a single dot.  */
          if (*slash)
            {
              /* There are more parts, so just skip this one.  */
              part = slash + 1;
              continue;
            }
          mustbedir = 1;
        }
      else
        parts[n_parts++] = part;

      if (n_parts == MAX_PARTS || !*slash)
        {
          parts[n_parts] = NULL;
          next_fid = p9_fid_alloc ();

          err = p9_rpc (P9_WALK_REQUEST,
                        "442S", prev_fid, next_fid, n_parts, parts,
                        "Q", &n_qids, &qids);
          /* Clunk the previous fid, we don't need it anymore in either case */
          if (prev_fid != pi->walk_fid)
            p9_rpc (P9_CLUNK_REQUEST,
                    "4", prev_fid, "");
          if (err)
            return err;

          prev_fid = next_fid;
        }

      if (!*slash)
        {
          nnp = p9_make_node ();
          if (!nnp)
            {
              err = errno;
              goto out;
            }

          npo = p9_make_peropen (nnp, flags, NULL);
          if (!npo)
            {
              err = errno;
              goto out;
            }

          err = iohelp_dup_iouser (&ncred, pi->user);
          if (err)
            {
              p9_release_peropen (npo);
              goto out;
            }

          npi = p9_make_protid (npo, ncred);
          if (!npi)
            {
              err = errno;
              p9_release_peropen (npo);
              iohelp_free_iouser (ncred);
              goto out;
            }

          npi->walk_fid = prev_fid;
          // TODO: guard against n_qids == 0
          nnp->qid = qids[n_qids - 1];

          *do_retry = FS_RETRY_NORMAL;
          *result = ports_get_right (npi);
          *result_type = MACH_MSG_TYPE_MAKE_SEND;
          retry_name[0] = '\0';
          goto out;
        }

      part = slash + 1;
    }
  while (*slash);


out:
  free (qids);
  if (npi)
    ports_port_deref (npi);
  if (nnp)
    p9_nrele (nnp);

  return err;
}
