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
#include "io_S.h"
#include <errno.h>
#include <hurd/exec.h>
#include <hurd/fshelp.h>
#include <hurd/paths.h>

error_t
S_file_exec_paths (struct protid *pi, task_t task, int flags,
                   const char *path, const char *abspath,
                   const char *argv, mach_msg_type_number_t argvlen,
                   const char *envp, mach_msg_type_number_t envplen,
                   const mach_port_t *fds, mach_msg_type_number_t fdslen,
                   const mach_port_t *portarray,
                   mach_msg_type_number_t portarraylen,
                   const int *intarray,
                   mach_msg_type_number_t intarraylen,
                   const mach_port_t *deallocnames,
                   mach_msg_type_number_t deallocnameslen,
                   const mach_port_t *destroynames,
                   mach_msg_type_number_t destroynameslen)
{
  error_t err;
  io_statbuf_t st;
  struct peropen *npo;
  struct iouser *ncred;
  struct protid *npi;
  mach_port_t exec, nright, orig_right;
  uint16_t n_qids;

  if (!pi)
    return EOPNOTSUPP;
  if (p9_noexec)
    return EACCES;
  if (!(pi->po->user_open_flags & O_EXEC))
    return EBADF;
  if (pi->walk_fid == P9_NO_FID)
    return EBADF;

  err = S_io_stat (pi, &st);
  if (err)
    return err;
  if (!S_ISREG (st.st_mode))
    return EACCES;
  err = fshelp_access (&st, S_IEXEC, pi->user);
  if (err)
    return err;

  /* Now's the fun part: setuid handling */
  if (!p9_nosuid && (st.st_mode & (S_ISUID | S_ISGID)))
    {
      error_t err2;
      int secure;
      mach_port_t auth;

      auth = getauth ();
      if (!MACH_PORT_VALID (auth))
        return EGRATUITOUS;

#ifdef I_LIKE_EXECUTABLE_STACKS
      error_t get_file_ids (struct idvec *uids, struct idvec *gids)
        {
          error_t err;

          err = idvec_merge (uids, pi->user->uids);
          if (err)
            return err;
          return idvec_merge (gids, pi->user->gids);
        }
#else
      void *get_file_ids = NULL;
#endif

      err = fshelp_exec_reauth (st.st_mode & S_ISUID, st.st_uid,
                                st.st_mode & S_ISGID, st.st_gid,
                                auth, get_file_ids,
                                (mach_port_t *) portarray, portarraylen,
                                (mach_port_t *) fds, fdslen,
                                &secure);
      err2 = mach_port_deallocate (mach_task_self (), auth);
      assert_perror_backtrace (err2);
      if (err)
        return err;
      if (secure)
        flags |= EXEC_SECURE | EXEC_NEWTASK;
    }

  /* Make a new peropen so that the exec server can read the file without
     disturbing the original's I/O offset.  */
  npo = p9_make_peropen (pi->po->np, O_READ | O_EXEC, pi->po);
  if (!npo)
    return errno;
  /* FIXME: it would be more correct here to account for suid/sgid.  */
  err = iohelp_dup_iouser (&ncred, pi->user);
  if (err)
    {
      p9_release_peropen (npo);
      return err;
    }
  npi = p9_make_protid (npo, ncred);
  if (!npi)
    {
      err = errno;
      p9_release_peropen (npo);
      iohelp_free_iouser (ncred);
      return err;
    }
  nright = ports_get_right (npi);
  if (nright == MACH_PORT_NULL)
    {
      err = errno;
      ports_port_deref (npi);
      return err;
    }

  /* Duplicate the walk fid */
  npi->walk_fid = p9_fid_alloc ();
  err = p9_rpc (P9_WALK_REQUEST,
                "442", pi->walk_fid, npi->walk_fid, 0,
                "2", &n_qids);
  if (err)
    {
      ports_port_deref (npi);
      return err;
    }

  orig_right = ports_get_send_right (pi);

  /* Call the exec server */
  exec = file_name_lookup (_SERVERS_EXEC, 0, 0);
  if (!exec)
    {
      ports_port_deref (npi);
      return EOPNOTSUPP;
    }
  err = exec_exec_paths (exec, nright, MACH_MSG_TYPE_MAKE_SEND,
                         task, flags, path, abspath,
                         argv, argvlen, envp, envplen,
                         fds, MACH_MSG_TYPE_MOVE_SEND, fdslen,
                         portarray, MACH_MSG_TYPE_MOVE_SEND, portarraylen,
                         intarray, intarraylen,
                         deallocnames, deallocnameslen,
                         destroynames, destroynameslen);
  mach_port_deallocate (mach_task_self (), exec);
  mach_port_deallocate (mach_task_self (), task);
  if (MACH_PORT_VALID (orig_right))
    mach_port_deallocate (mach_task_self (), orig_right);

  /* TODO: Verify that we got the deallocate vs not convention right.  We assume
     that the above call has deallocated the ports if it succeeded, and not if
     it failed, and we succeed/fail in the same cases.  */

  ports_port_deref (npi);
  return err;
}
