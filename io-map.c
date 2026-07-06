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
#include "io_S.h"
#include <hurd/pager.h>
#include <errno.h>

error_t
S_io_map (struct protid *protid,
          mach_port_t *rdobj,
          mach_msg_type_name_t *rdtype,
          mach_port_t *wrobj,
          mach_msg_type_name_t *wrtype)
{
  error_t err;
  int flags;
  struct node *np;
  struct pager *pager;
  boolean_t deref_pager = FALSE;

  if (!protid)
    return EOPNOTSUPP;

  flags = protid->po->user_open_flags & O_RDWR;
  if (!flags)
    {
      /* Return null ports, successfully.  The client
         translates this to EACCES from mmap.  */
      *rdobj = *wrobj = MACH_PORT_NULL;
      *rdtype = *wrtype = MACH_MSG_TYPE_COPY_SEND;
      return 0;
    }

  np = protid->po->np;
  pthread_mutex_lock (&np->lock);

  pager = np->pager;
  if (!pager)
    {
      uint16_t n_qids;

      assert_backtrace (np->pager_fid == P9_NO_FID);
      np->pager_fid = p9_fid_alloc ();
      err = p9_rpc (P9_WALK_REQUEST,
                    "442", protid->walk_fid, np->pager_fid, 0,
                    "2", &n_qids);
      if (err)
        {
          pthread_mutex_unlock (&np->lock);
          return err;
        }
      /* FIXME: Do this more robustly, perhaps let the pager hold its own protid */
      {
        struct p9_qid qid;
        uint32_t max_message_size;

        if (p9_version >= P9_VERSION_2000_L)
          err = p9_rpc (P9_LOPEN_REQUEST,
                        "44", np->pager_fid, 0, /* Linux's O_RDONLY */
                        "1484", &qid.type, &qid.version, &qid.path,
                        &max_message_size);
        else
          err = p9_rpc (P9_OPEN_REQUEST,
                        "41", np->pager_fid, 0, /* Plan 9 O_RDONLY */
                        "1484", &qid.type, &qid.version, &qid.path,
                        &max_message_size);
        if (err)
          {
            p9_rpc (P9_CLUNK_REQUEST,
                    "4", np->pager_fid, "");
            np->pager_fid = P9_NO_FID;
            pthread_mutex_unlock (&np->lock);
            return err;
          }
      }

      /* XXX: MEMORY_OBJECT_COPY_DELAY is not correct here, since the remote
         server could also be modifying the files and we wouldn't know.  */
      pager = pager_create ((struct user_pager_info *) np, p9_pager_bucket,
                            FALSE, MEMORY_OBJECT_COPY_DELAY, FALSE);
      if (!pager)
        {
          err = errno;
          p9_rpc (P9_CLUNK_REQUEST,
                  "4", np->pager_fid, "");
          np->pager_fid = P9_NO_FID;
          pthread_mutex_unlock (&np->lock);
          return err;
        }
      deref_pager = TRUE;

      /* Pager holds a strong reference to its node.  The node holds
         a weak reference back to the pager.  */
      ports_port_ref_weak (pager);
      p9_nref (np);
      np->pager = pager;
    }

  switch (flags)
    {
    case O_RDWR:
      /* Call pager_get_port twice to account for two make-sends.  */
      *wrobj = pager_get_port (pager);
      *rdobj = pager_get_port (pager);
      *rdtype = *wrtype = MACH_MSG_TYPE_MAKE_SEND;
      break;
    case O_READ:
      *wrobj = MACH_PORT_NULL;
      *rdobj = pager_create_ro_port (pager);
      *rdtype = *wrtype = MACH_MSG_TYPE_MOVE_SEND;
      break;
    case O_WRITE:
      /* We'll return null in *rdport, but that's mostly informative,
         it doesn't really prevent the client from reading.  */
      *wrobj = pager_get_port (pager);
      *rdobj = MACH_PORT_NULL;
      *rdtype = *wrtype = MACH_MSG_TYPE_MAKE_SEND;
      break;
    default:
      assert_backtrace (0);
    }

  pthread_mutex_unlock (&np->lock);

  if (deref_pager)
    ports_port_deref (pager);

  return 0;
}
