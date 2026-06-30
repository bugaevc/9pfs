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
#include "fsys_S.h"
#include <errno.h>
#include <mach.h>
#include <hurd/fshelp.h>

error_t
S_fsys_getroot (struct port_info *control,
                mach_port_t dotdot,
                const uid_t *uids, mach_msg_type_number_t nuids,
                const gid_t *gids, mach_msg_type_number_t ngids,
                int flags,
                retry_type *do_retry,
                char *retry_name,
                mach_port_t *retry_port,
                mach_msg_type_name_t *retry_port_type)
{
  error_t err;
  struct node *np;
  struct iouser *cred;
  struct peropen *po;
  struct protid *pi = NULL;

  if (control != p9_control)
    return EOPNOTSUPP;
  if (p9_readonly && (flags & O_WRITE))
    return EROFS;

  flags &= O_HURD;

  np = p9_make_node ();
  if (!np)
    return errno;

  po = p9_make_peropen (np, flags & ~OPENONLY_STATE_MODES, NULL);
  if (!po)
    {
      err = errno;
      goto out;
    }

  po->root_parent = dotdot;
  if (dotdot != MACH_PORT_NULL)
    {
      err = mach_port_mod_refs (mach_task_self (), dotdot,
                                MACH_PORT_RIGHT_SEND, 1);
      assert_perror_backtrace (err);
    }

  err = iohelp_create_complex_iouser (&cred, uids, nuids, gids, ngids);
  if (err)
    {
      p9_release_peropen (po);
      goto out;
    }

  pi = p9_make_protid (po, cred);
  if (!pi)
    {
      err = errno;
      p9_release_peropen (po);
      iohelp_free_iouser (cred);
      goto out;
    }

  pi->walk_fid = p9_fid_alloc ();

  if (p9_version >= P9_VERSION_2000_U)
    err = p9_rpc (P9_ATTACH_REQUEST,
                  "44ss4", pi->walk_fid, -1,
                  p9_user_name ?: "", p9_aname ?: "", p9_uid,
                  "148", &np->qid.type,
                  &np->qid.version, &np->qid.path);
  else
    err = p9_rpc (P9_ATTACH_REQUEST,
                  "44ss", pi->walk_fid, -1,
                  p9_user_name ?: "", p9_aname ?: "",
                  "148", &np->qid.type,
                  &np->qid.version, &np->qid.path);
  if (err)
    {
      /* Don't attempt to clunk the never-created fid.  */
      pi->walk_fid = P9_NO_FID;
      goto out;
    }

  /* If an open mode is requested for the very root,
     apply it now.  */
  err = p9_ensure_open (pi, flags & O_ACCMODE);
  if (err)
    goto out;

  *do_retry = FS_RETRY_NORMAL;
  *retry_port = ports_get_right (pi);
  *retry_port_type = MACH_MSG_TYPE_MAKE_SEND;
  retry_name[0] = '\0';

 out:
  if (pi)
    ports_port_deref (pi);
  p9_nrele (np);

  if (!err)
    mach_port_deallocate (mach_task_self (), dotdot);

  return err;
}
