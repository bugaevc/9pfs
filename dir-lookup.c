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
#include "9p-rpc.h"
#include "fs_S.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
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
  uint16_t n_qids;
  struct p9_qid *qids = NULL;
  struct node *nnp;
  uint32_t prev_fid, next_fid;
  struct peropen *npo;
  struct protid *npi = NULL;
  struct iouser *ncred;
  boolean_t have_created = FALSE;
  uint32_t max_message_size;
  boolean_t must_be_dir = FALSE;

  if (!pi)
    return EOPNOTSUPP;
  if (p9_readonly && (flags & (O_WRITE | O_EXCL)))
    return EROFS;

  /* TODO: should we also fail-fast on O_CREAT even in case
     the file does exist?  */

  flags &= O_HURD;
  prev_fid = pi->walk_fid;

  while (*name == '/')
    name++;

  part = name;
  while (TRUE)
    {
      /* Look for the next part of path.  */
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
          must_be_dir = TRUE;
        }
      else
        parts[n_parts++] = part;

      /* If we got to the end, or accumulated enough parts,
         perform the walk RPC.  */
      if (n_parts == MAX_PARTS || !*slash)
        {
          boolean_t create;

          /* If O_CREAT is specified, avoid looking up the very last
             part just yet.  */
          create = !*slash && (flags & O_CREAT);
          if (create)
            {
              /* TODO: is this the expected behavior? */
              if (must_be_dir)
                return EINVAL;
              n_parts--;
            }
          parts[n_parts] = NULL;
          next_fid = p9_fid_alloc ();

          err = p9_rpc (P9_WALK_REQUEST,
                        "442S", prev_fid, next_fid, n_parts, parts,
                        "Q", &n_qids, &qids);
          /* Clunk the previous fid, we don't need it anymore
             in either case.  */
          if (prev_fid != pi->walk_fid)
            p9_rpc (P9_CLUNK_REQUEST,
                    "4", prev_fid, "");
          if (err)
            return err;

          prev_fid = next_fid;

          if (create)
            {
              /* Low look up the last part.  Try walk, then create, then walk again.
                 This way, we succeed if the file exists but could not be created
                 (and the error code needs not be EEXIST), and also multiple clients
                 opening the file with O_CREAT concurrently get the expected behavior.  */
              free (qids);
              qids = NULL;

              if (!(flags & O_EXCL))
                {
                  err = p9_rpc (P9_WALK_REQUEST,
                                "442s", prev_fid, prev_fid, 1, part,
                                "Q", &n_qids, &qids);

                  if (!err)
                    goto found;
                  else if (err != ENOENT)
                    {
                      p9_rpc (P9_CLUNK_REQUEST,
                              "4", prev_fid, "");
                      return err;
                    }
                }

              n_qids = 1;
              qids = malloc (sizeof (struct p9_qid));

              if (p9_version >= P9_VERSION_2000_L)
                err = p9_rpc (P9_LCREATE_REQUEST,
                              "4s444", next_fid, part,
                              p9_open_flags_to_lflags (flags),
                              mode, getegid (),
                              "1484", &qids[0].type,
                              &qids[0].version, &qids[0].path,
                              &max_message_size);
              else
                err = p9_rpc (P9_CREATE_REQUEST,
                              "4s41", next_fid, part,
                              mode, p9_open_flags_to_core_mode (flags),
                              "1484", &qids[0].type,
                              &qids[0].version, &qids[0].path,
                              &max_message_size);

              if (!err)
                {
                  have_created = TRUE;
                  goto found;
                }
              else if ((flags & O_EXCL) || err != EEXIST)
                {
                  p9_rpc (P9_CLUNK_REQUEST,
                          "4", next_fid, "");
                  return err;
                }

              /* We tried to walk to it and got ENOENT, tried to create it and
                 got EEXIST.  Somebody must have created it concurrently, try
                 to walk one more time.  */
              free (qids);
              err = p9_rpc (P9_WALK_REQUEST,
                            "442s", prev_fid, prev_fid, 1, part,
                            "Q", &n_qids, &qids);
              if (err)
                return err;
            }

          /* Prepare for the next round.  */
          n_parts = 0;
        }

      /* If this was the last part of the path, proceed to set up
         the node, the peropen and the protid.  */
      if (!*slash)
        {
found:
          nnp = p9_make_node ();
          if (!nnp)
            {
              err = errno;
              goto out;
            }

          npo = p9_make_peropen (nnp, flags & ~OPENONLY_STATE_MODES, pi->po);
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

          if (n_qids > 0)
            nnp->qid = qids[n_qids - 1];

          if (have_created)
            {
              npi->io_fid = prev_fid;
              next_fid = p9_fid_alloc ();
              free (qids);
              qids = NULL;
              err = p9_rpc (P9_WALK_REQUEST,
                            "442", prev_fid, next_fid, 0,
                            "Q", &n_qids, &qids);
              if (err)
                goto out;
              npi->walk_fid = next_fid;
              npi->server_open_flags |= (flags & O_ACCMODE);
              if (max_message_size > 0 &&
                  max_message_size < p9_max_message_size)
                nnp->max_message_size = max_message_size;
              else
                nnp->max_message_size = p9_max_message_size;
            }
          else
            {
              npi->walk_fid = prev_fid;
              err = p9_ensure_open (npi, flags & O_ACCMODE);
              if (err)
                goto out;
            }

          *do_retry = FS_RETRY_NORMAL;
          *result = ports_get_right (npi);
          *result_type = MACH_MSG_TYPE_MAKE_SEND;
          retry_name[0] = '\0';
          goto out;
        }

      part = slash + 1;
    }


out:
  free (qids);
  if (npi)
    ports_port_deref (npi);
  if (nnp)
    p9_nrele (nnp);

  return err;
}
