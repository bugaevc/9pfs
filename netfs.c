/*
 *
 * This file is not compiled!
 *
 * No really, it's for reference only. 9pfs initially used netfs, that implementation is here.
 * I've been writing a new implementation that does not use netfs. This file is left here as
 * a reference for now; we can steal pieces of functionality over to the new implementation.
 *
 */


#define _GNU_SOURCE 1

#include "9pfs.h"
#include "9p-rpc.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <hurd/netfs.h>

#define P9_STAT_BASIC "2241484448ssss"
#define NO_FID ((uint32_t) -1)

char *netfs_server_name = "9pfs";
char *netfs_server_version = "0.1.0";
int netfs_maxsymlinks = 16;

enum p9_version p9_version;

static uint32_t next_fid;

static uint32_t
allocate_fid ()
{
  return __atomic_add_fetch (&next_fid, 1, __ATOMIC_RELAXED);
}

struct dir_buffer
{
  void *base;
  void *ptr;
  size_t allocated_size;
  size_t remaining_size;
};

struct netnode
{
  char *path;  /* NULL if root */
  time_t last_stat;
  uint32_t walk_fid, io_fid;
  struct p9_qid qid;
  size_t max_message_size;
  int open_flags;

  struct
  {
    int next_entry;
    uint64_t next_offset;
    struct dir_buffer buffer;
  } dir;
};

static void
init_netnode (struct netnode *nn)
{
  nn->path = NULL;
  nn->last_stat = 0;
  nn->walk_fid = nn->io_fid = NO_FID;
  memset (&nn->qid, 0, sizeof (nn->qid));
  nn->max_message_size = 0;
  nn->open_flags = 0;

  nn->dir.next_entry = 0;
  nn->dir.next_offset = 0;
  nn->dir.buffer.base = NULL;
  nn->dir.buffer.ptr = NULL;
  nn->dir.buffer.allocated_size = 0;
  nn->dir.buffer.remaining_size = 0;
}

error_t
p9_init_fs (void)
{
  error_t err;
  uint32_t max_message_size = 16 * 1024 * 1024;
  char *remote_version;
  struct netnode *root_nn;

  err = p9_rpc (P9_VERSION_REQUEST,
                "4s", max_message_size, "9P2000.L",
                "4s", &max_message_size, &remote_version);
  if (err)
    return err;

  if (strcmp (remote_version, "9P2000.L") == 0)
    p9_version = P9_VERSION_2000_L;
  else if (strcmp (remote_version, "9P2000.u") == 0)
    p9_version = P9_VERSION_2000_U;
  else
    p9_version = P9_VERSION_2000;

  if (max_message_size == 0)
    {
      error (0, 0, "remote server sent max message size of 0");
      return EIO;
    }

  netfs_root_node = netfs_make_node_alloc (sizeof (struct netnode));
  root_nn = netfs_node_netnode (netfs_root_node);
  init_netnode (root_nn);

  root_nn->max_message_size = max_message_size;
  root_nn->walk_fid = allocate_fid ();

  if (p9_version >= P9_VERSION_2000_U)
    err = p9_rpc (P9_ATTACH_REQUEST,
                  "44ss4", root_nn->walk_fid, -1,
                  p9_user_name ?: "", p9_aname ?: "", p9_uid,
                  "148", &root_nn->qid.type,
                  &root_nn->qid.version, &root_nn->qid.path);
  else
    err = p9_rpc (P9_ATTACH_REQUEST,
                  "44ss", root_nn->walk_fid, -1,
                  p9_user_name ?: "", p9_aname ?: "",
                  "148", &root_nn->qid.type,
                  &root_nn->qid.version, &root_nn->qid.path);
  if (err)
    return err;

  return 0;
}

static size_t
adjust_buffer_size (struct netnode *nn, size_t reserve_size,
                    size_t buffer_size)
{
  if (reserve_size + buffer_size > nn->max_message_size)
    return nn->max_message_size - reserve_size;
  return buffer_size;
}

static inline uint64_t
div_round_up (uint64_t a, uint64_t b)
{
  uint64_t c = a / b;
  return (a % b) ? c + 1 : c;
}

static mode_t
p9_mode_to_mode (uint32_t p9_mode, int is_blockdev)
{
  mode_t mode = p9_mode & 0777;

  if (p9_version >= P9_VERSION_2000_U)
    {
      if (p9_mode & P9_MODE_SETGID)
        mode |= S_ISGID;
      if (p9_mode & P9_MODE_SETUID)
        mode |= S_ISUID;
      if (p9_mode & P9_MODE_SOCKET)
        mode |= S_IFSOCK;
      if (p9_mode & P9_MODE_FIFO)
        mode |= S_IFIFO;
      if (p9_mode & P9_MODE_DEV)
        mode |= is_blockdev ? S_IFBLK : S_IFCHR;
      if (p9_mode & P9_MODE_SYMLINK)
        mode |= S_IFLNK;
    }

  if (p9_mode & P9_MODE_DIR)
    mode |= S_IFDIR;

  if ((mode & S_IFMT) == 0)
    mode |= S_IFREG;

  return mode;
}

static uint32_t
mode_to_p9_mode (mode_t mode)
{
  uint32_t p9_mode = mode & 0777;

  if (p9_version >= P9_VERSION_2000_U)
    {
      if (mode & S_ISGID)
        p9_mode |= P9_MODE_SETGID;
      if (mode & S_ISUID)
        p9_mode |= P9_MODE_SETUID;
      if (mode & S_IFSOCK)
        p9_mode |= P9_MODE_SOCKET;
      if (mode & S_IFIFO)
        p9_mode |= P9_MODE_FIFO;
      if (mode & (S_IFCHR | S_IFBLK))
        p9_mode |= P9_MODE_DEV;
      if (mode & S_IFLNK)
        p9_mode |= P9_MODE_SYMLINK;
    }

  if (mode & S_IFDIR)
    p9_mode |= P9_MODE_DIR;

  return p9_mode;
}

static uint32_t
p9_mode_to_d_type (uint32_t p9_mode, int is_blockdev)
{
  if (p9_version >= P9_VERSION_2000_U)
    {
      if (p9_mode & P9_MODE_SOCKET)
        return DT_SOCK;
      if (p9_mode & P9_MODE_FIFO)
        return DT_FIFO;
      if (p9_mode & P9_MODE_DEV)
        return is_blockdev ? DT_BLK : DT_CHR;
      if (p9_mode & P9_MODE_SYMLINK)
        return DT_LNK;
    }

  if (p9_mode & P9_MODE_DIR)
    return DT_DIR;

  return DT_REG;
}

static int
parse_extension_device (const char *extension,
                        dev_t *dev, int *is_blockdev)
{
  int minor, major;
  char c;
  int scanned;

  scanned = sscanf (extension, "%c%d%d", &c, &major, &minor);
  if (scanned != 3)
    return 0;

  if (c == 'c')
    *is_blockdev = 0;
  else if (c == 'b')
    *is_blockdev = 1;
  else
    return 0;

  *dev = makedev (major, minor);
  return 1;
}

static uid_t
p9_uid_to_uid (const char *name, uint32_t n_uid)
{
  if (p9_user_name == NULL || name == NULL)
    return p9_nobody_uid;

  if (strcmp (p9_user_name, name) == 0)
    return getuid ();
  return p9_nobody_uid;
}

static gid_t
p9_gid_to_gid (const char *name, uint32_t n_gid)
{
  return p9_nobody_gid;
}

error_t
netfs_validate_stat (struct node *np, struct iouser *cred)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (np);

  /* Cache stats for up to 5 seconds.  */
  if (time (NULL) <= nn->last_stat + 5)
    return 0;

  np->nn_stat.st_fsid = getpid ();
  np->nn_stat.st_fstype = FSTYPE_MISC;

  if (p9_version >= P9_VERSION_2000_L)
    {
      /* In 9P2000.L, there's a dedicated getattr request
         which returns Unix stat info.  */
      uint64_t valid_mask;
      uint32_t mode, uid, gid;
      uint64_t nlink, rdev, size, blksize, blocks;
      uint64_t atime_sec, atime_nsec, mtime_sec, mtime_nsec;
      uint64_t ctime_sec, ctime_nsec, btime_sec, btime_nsec;
      uint64_t gen, data_version;

      err = p9_rpc (P9_GETATTR_REQUEST,
                    "48", nn->walk_fid, (uint64_t) P9_GETATTR_MASK_ALL_DEFINED,
                    "8148444888888888888888", &valid_mask,
                    &nn->qid.type, &nn->qid.version, &nn->qid.path,
                    &mode, &uid, &gid, &nlink, &rdev, &size, &blksize, &blocks,
                    &atime_sec, &atime_nsec, &mtime_sec, &mtime_nsec,
                    &ctime_sec, &ctime_nsec, &btime_sec, &btime_nsec,
                    &gen, &data_version);
      if (err)
        return err;

      if (valid_mask & P9_GETATTR_MASK_INO)
        np->nn_stat.st_ino = nn->qid.path;

      if (valid_mask & P9_GETATTR_MASK_MODE)
        np->nn_stat.st_mode = mode & (p9_mode_mask | ~0777);
      else
        np->nn_stat.st_mode = p9_mode_mask;

      if (valid_mask & P9_GETATTR_MASK_NLINK)
        np->nn_stat.st_nlink = nlink;
      else
        np->nn_stat.st_nlink = 1;

      if (valid_mask & P9_GETATTR_MASK_UID)
        np->nn_stat.st_uid = p9_uid_to_uid (NULL, uid);
      else
        np->nn_stat.st_uid = p9_nobody_uid;

      if (valid_mask & P9_GETATTR_MASK_GID)
        np->nn_stat.st_gid = p9_gid_to_gid (NULL, gid);
      else
        np->nn_stat.st_gid = p9_nobody_gid;

      if (valid_mask & P9_GETATTR_MASK_RDEV)
        np->nn_stat.st_rdev = rdev;
      else
        np->nn_stat.st_rdev = 0;

      if (valid_mask & P9_GETATTR_MASK_SIZE)
        np->nn_stat.st_size = size;
      else
        np->nn_stat.st_size = 0;

      if (valid_mask & P9_GETATTR_MASK_BLOCKS)
        np->nn_stat.st_blksize = blksize;
      else
        np->nn_stat.st_blksize = 512;

      if (valid_mask & P9_GETATTR_MASK_BLOCKS)
        np->nn_stat.st_blocks = blocks;
      else
        np->nn_stat.st_blocks = div_round_up (size, 512);

      if (valid_mask & P9_GETATTR_MASK_ATIME)
        {
          np->nn_stat.st_atim.tv_sec = atime_sec;
          np->nn_stat.st_atim.tv_nsec = atime_nsec;
        }
      else
        memset (&np->nn_stat.st_atim, 0,
                sizeof (struct timespec));

      if (valid_mask & P9_GETATTR_MASK_MTIME)
        {
          np->nn_stat.st_mtim.tv_sec = mtime_sec;
          np->nn_stat.st_mtim.tv_nsec = mtime_nsec;
        }
      else
        memset (&np->nn_stat.st_mtim, 0,
                sizeof (struct timespec));

      if (valid_mask & P9_GETATTR_MASK_CTIME)
        {
          np->nn_stat.st_ctim.tv_sec = ctime_sec;
          np->nn_stat.st_ctim.tv_nsec = ctime_nsec;
        }
      else
	memset (&np->nn_stat.st_ctim, 0,
                sizeof (struct timespec));
    }
  else
    {
      uint16_t stat_size;
      int long_stat, is_blockdev = 0;
      struct p9_stat s = { 0 };
      /* In plain 9P2000, we have to use Plan 9 stat, and
         map it to our Unix semantics as well as possible.  */
      err = p9_rpc (P9_STAT_REQUEST,
                    "4", nn->walk_fid,
                    "2" P9_STAT_BASIC "?s444", &stat_size, &s.size,
                    &s.type, &s.dev,
                    &s.qid.type, &s.qid.version, &s.qid.path,
                    &s.mode, &s.atime, &s.mtime, &s.length,
                    &s.name, &s.uid, &s.gid, &s.muid,
                    &long_stat,
                    &s.extension, &s.n_uid, &s.n_gid, &s.n_muid);
      if (err)
        {
          /* Make sure to free the partially-filled struct.  */
          p9_stat_free (&s);
          return err;
        }

      if (long_stat && p9_version < P9_VERSION_2000_U)
        {
          error (0, 0, "Unexpectedly long stat in core 9P");
          /* Forcefully ignore it; who knows what the server
             decided to put there.  */
          long_stat = 0;
        }

      nn->qid = s.qid;

      if (long_stat && s.extension)
        parse_extension_device (s.extension, &np->nn_stat.st_rdev,
                                &is_blockdev);
      else
        np->nn_stat.st_rdev = 0;

      np->nn_stat.st_ino = s.qid.path;
      np->nn_stat.st_mode
          = p9_mode_to_mode (s.mode, is_blockdev) & (p9_mode_mask | ~0777);
      np->nn_stat.st_nlink = 1;
      np->nn_stat.st_uid = p9_uid_to_uid (s.uid, long_stat ? s.n_uid : ~0);
      np->nn_stat.st_gid = p9_gid_to_gid (s.gid, long_stat ? s.n_gid : ~0);
      np->nn_stat.st_size = s.length;
      np->nn_stat.st_blksize = 512;
      np->nn_stat.st_blocks = div_round_up (s.length, 512);
      np->nn_stat.st_atime = s.atime;
      np->nn_stat.st_mtime = s.mtime;
      np->nn_stat.st_ctime = s.mtime;

      p9_stat_free (&s);
    }

  nn->last_stat = time (NULL);
  return 0;
}

static error_t
p9_wstat (uint32_t fid, const struct p9_stat *s)
{
  uint16_t stat_size;

  if (p9_version >= P9_VERSION_2000_U)
    {
      stat_size = p9_serialized_size (P9_STAT_BASIC "s444",
                                      s->size, s->type, s->dev,
                                      s->qid.type, s->qid.version, s->qid.path,
                                      s->mode, s->atime, s->mtime,
                                      s->length, s->name,
                                      s->uid, s->gid, s->muid,
                                      s->extension,
                                      s->n_uid, s->n_gid, s->n_muid);
      return p9_rpc (P9_WSTAT_REQUEST,
                     "42" P9_STAT_BASIC "s444", fid,
                     stat_size, stat_size - 2,
                     s->type, s->dev,
                     s->qid.type, s->qid.version, s->qid.path,
                     s->mode, s->atime, s->mtime,
                     s->length, s->name,
                     s->uid, s->gid, s->muid,
                     s->extension,
                     s->n_uid, s->n_gid, s->n_muid,
                     "");
    }
  else
    {
      stat_size = p9_serialized_size (P9_STAT_BASIC,
                                      s->size, s->type, s->dev,
                                      s->qid.type, s->qid.version, s->qid.path,
                                      s->mode, s->atime, s->mtime,
                                      s->length, s->name,
                                      s->uid, s->gid, s->muid);
      return p9_rpc (P9_WSTAT_REQUEST,
                     "42" P9_STAT_BASIC, fid,
                     stat_size, stat_size - 2,
                     s->type, s->dev,
                     s->qid.type, s->qid.version, s->qid.path,
                     s->mode, s->atime, s->mtime,
                     s->length, s->name,
                     s->uid, s->gid, s->muid,
                     "");
    }
}

static error_t
p9_setattr (uint32_t fid, enum p9_setattr_mask valid_mask,
            uint32_t mode,
            uint32_t uid, uint32_t gid,
            uint64_t size,
            uint64_t atime_sec, uint64_t atime_nsec,
            uint64_t mtime_sec, uint64_t mtime_nsec)
{
  return p9_rpc (P9_SETATTR_REQUEST,
                 "4444488888", fid, (uint32_t) valid_mask,
                 mode, uid, gid, size, atime_sec, atime_nsec,
                 mtime_sec, mtime_nsec,
                 "");
}

static error_t
ensure_open (struct netnode *nn, int flags)
{
  error_t err;
  unsigned char core_mode;
  uint32_t l_flags;

  uint32_t newfid;
  uint32_t max_message_size;
  uint16_t n_qids;
  struct p9_qid qid;

  /* Check if it's already open with (at least) those flags.  */
  if ((nn->open_flags & flags) == flags)
    return 0;

  switch (flags)
    {
    case O_READ:
      core_mode = 0;
      l_flags = 0;
      break;
    case O_WRITE:
      core_mode = 1;
      l_flags = 0;
      break;
    case O_RDWR:
      core_mode = 2;
      l_flags = 2;
      break;
    default:
      assert_backtrace (!"Bad flags");
    }

  /* TODO: Do the below requests concurrently.  */

  if (nn->io_fid != NO_FID)
    {
      /* Close any existing I/O fid.  */
      err = p9_rpc (P9_CLUNK_REQUEST,
                    "4", nn->io_fid, "");
      if (err)
        error (0, err, "Failed to clunk fid");
      nn->io_fid = NO_FID;
      nn->open_flags = 0;
    }

  /* Duplicate the walk fid to a fresh one that
     will become the new I/O fid. */
  newfid = allocate_fid ();
  err = p9_rpc (P9_WALK_REQUEST,
                "442", nn->walk_fid, newfid, 0,
                "2", &n_qids);
  if (err)
    return err;

  nn->io_fid = newfid;

  if (p9_version >= P9_VERSION_2000_L)
    err = p9_rpc (P9_LOPEN_REQUEST,
                  "44", newfid, l_flags,
                  "1484", &qid.type, &qid.version, &qid.path,
                  &max_message_size);
  else
    err = p9_rpc (P9_OPEN_REQUEST,
                  "41", newfid, core_mode,
                  "1484", &qid.type, &qid.version, &qid.path,
                  &max_message_size);

  if (!err)
    {
      nn->open_flags = flags;
      /* Some servers are known to send a 0 iounit; ignore it.  */
      if (max_message_size > 0)
        nn->max_message_size = max_message_size;
    }

  return err;
}

error_t
netfs_attempt_chown (struct iouser *cred, struct node *np,
                     uid_t uid, uid_t gid)
{
  return EROFS;
}

error_t
netfs_attempt_chauthor (struct iouser *cred, struct node *np,
                        uid_t author)
{
  return EROFS;
}

error_t
netfs_attempt_chmod (struct iouser *cred, struct node *np,
                     mode_t mode)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (np);
  struct p9_stat s;

  nn->last_stat = 0;

  mode |= np->nn_stat.st_mode & S_IFMT;

  if (p9_version >= P9_VERSION_2000_L)
    return p9_setattr (nn->walk_fid, P9_SETATTR_MASK_MODE,
                       mode, 0, 0, 0, 0, 0, 0, 0);

  p9_stat_dont_touch (&s);
  s.mode = mode_to_p9_mode (mode);
  err = p9_wstat (nn->walk_fid, &s);
  p9_stat_free (&s);

  return err;
}

error_t
netfs_attempt_mksymlink (struct iouser *cred, struct node *np,
                         const char *name)
{
  return EROFS;
}

error_t
netfs_attempt_mkdev (struct iouser *cred, struct node *np,
                     mode_t type, dev_t indexes)
{
  return EROFS;
}

error_t
netfs_attempt_chflags (struct iouser *cred, struct node *np,
                       int flags)
{
  return EROFS;
}

error_t
netfs_attempt_utimes (struct iouser *cred, struct node *np,
                      struct timespec *atime, struct timespec *mtime)
{
  return EROFS;
}

error_t
netfs_attempt_set_size (struct iouser *cred, struct node *np,
                        loff_t size)
{
  error_t err;
  struct p9_stat s;
  struct netnode *nn = netfs_node_netnode (np);

  if (p9_version >= P9_VERSION_2000_L)
    return p9_setattr (nn->walk_fid, P9_SETATTR_MASK_SIZE,
                       0, 0, 0, size, 0, 0, 0, 0);

  p9_stat_dont_touch (&s);
  s.length = size;
  err = p9_wstat (nn->walk_fid, &s);
  p9_stat_free (&s);

  return err;

}

error_t
netfs_attempt_statfs (struct iouser *cred, struct node *np,
                      fsys_statfsbuf_t *st)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_sync (struct iouser *cred, struct node *np, int wait)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (np);

  if (p9_version >= P9_VERSION_2000_L)
    {
      /* Use the dedicated fsync request.  */
      err = ensure_open (nn, O_WRITE);
      if (err)
        return err;
      err = p9_rpc (P9_FSYNC_REQUEST,
                    "4", nn->io_fid, "");
    }
  else
    {
      struct p9_stat s;

      /* Otherwise, use the special case of wstat with
         all the values set to "don't touch" values.  */
      p9_stat_dont_touch (&s);
      err = p9_wstat (nn->walk_fid, &s);
      p9_stat_free (&s);
    }

  return err;
}

error_t
netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  /* There's no operation in 9P for this; pretend it worked.  */
  return 0;
}

error_t
netfs_attempt_lookup (struct iouser *cred, struct node *dir,
                      const char *name, struct node **np)
{
  error_t err;
  uint32_t newfid;
  uint16_t n_qids;
  struct p9_qid qid;
  struct netnode *dirnn, *nn;
  size_t parent_path_len, path_len;

  /* TODO: Redefine netfs_S_dir_lookup () to allow multi-hop
     lookups on the remote side, since Twalk supports multiple
     path components.  */

  dirnn = netfs_node_netnode (dir);
  newfid = allocate_fid ();

  err = p9_rpc (P9_WALK_REQUEST,
                "442s", dirnn->walk_fid, newfid, 1, name,
                "2148", &n_qids, &qid.type, &qid.version, &qid.path);
  if (err)
    {
      *np = NULL;
      goto out;
    }

  *np = netfs_make_node_alloc (sizeof (struct netnode));
  if (*np == NULL)
    {
      err = errno;
      goto out;
    }
  nn = netfs_node_netnode (*np);
  init_netnode (nn);

  nn->walk_fid = newfid;
  nn->qid = qid;
  nn->max_message_size = dirnn->max_message_size;

  parent_path_len = dirnn->path ? strlen (dirnn->path) : 0;
  path_len = parent_path_len + 1 + strlen (name);

  nn->path = malloc (path_len + 1);
  if (nn->path == NULL)
    {
      err = errno;
      free (*np);
      *np = NULL;
      goto out;
    }
  if (parent_path_len > 0)
    memcpy (nn->path, dirnn->path, parent_path_len);
  nn->path[parent_path_len] = '/';
  memcpy (nn->path + parent_path_len + 1, name, strlen (name));
  nn->path[path_len] = 0;

  pthread_mutex_lock (&(*np)->lock);

 out:
  pthread_mutex_unlock (&dir->lock);
  return err;
}

error_t
netfs_attempt_unlink (struct iouser *cred, struct node *dir, const char *name)
{
  return EROFS;
}

error_t
netfs_attempt_rename (struct iouser *cred, struct node *fromdir,
                      const char *fromname, struct node *todir,
                      const char *toname, int excl)
{
  return EROFS;
}

error_t
netfs_attempt_mkdir (struct iouser *cred, struct node *dir,
                     const char *name, mode_t mode)
{
  return EROFS;
}

error_t
netfs_attempt_rmdir (struct iouser *cred, struct node *dir,
                     const char *name)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (dir);
  uint32_t newfid;
  uint16_t n_qids;
  struct p9_qid qid;

  /* TODO: Do the below operations concurrently.  */
  newfid = allocate_fid ();

  err = p9_rpc (P9_WALK_REQUEST,
                "442s", nn->walk_fid, newfid, 1, name,
                "2148", &n_qids, &qid.type, &qid.version, &qid.path);
  if (err)
    return err;

  err = p9_rpc (P9_REMOVE_REQUEST,
                "4", newfid, "");
  /* Tremove also clunks the fid, even if the remove itself fails.  */
  return err;
}

error_t
netfs_attempt_link (struct iouser *cred, struct node *dir,
                    struct node *file, const char *name, int excl)
{
  return EROFS;
}

error_t
netfs_attempt_mkfile (struct iouser *user, struct node *dir,
                      mode_t mode, struct node **np)
{
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

error_t
netfs_attempt_create_file (struct iouser *cred, struct node *dir,
                           const char *name, mode_t mode, struct node **np)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_readlink (struct iouser *cred, struct node *np,
                        char *buf)
{
  return EOPNOTSUPP;
}

error_t
netfs_check_open_permissions (struct iouser *cred, struct node *np,
                              int flags, int newnode)
{
  return 0;
}

error_t
netfs_attempt_read (struct iouser *cred, struct node *np,
                    loff_t offset, size_t *len, void *data)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (np);
  uint32_t size;

  nn->last_stat = 0;

  /* TODO: Do the below requests concurrently.  */
  err = ensure_open (nn, O_READ);
  if (err)
    return err;

  /* We only really need to reserve 4 + 1 + 2 + 4 = 11 bytes
     of space for the headers in the read_response message;
     but some servers are known to require at least 24.
     It is also important that we adjust the buffer size after
     potentially opening the file, since that may update the
     mac_buffer_size.  */
  size = adjust_buffer_size (nn, 24, *len);

  err = p9_rpc (P9_READ_REQUEST,
                "484", nn->io_fid, (uint64_t) offset, size,
                "d", &size, data);
  *len = err ? 0 : size;
  return err;
}

error_t
netfs_attempt_write (struct iouser *cred, struct node *np,
                     loff_t offset, size_t *len, const void *data)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (np);
  uint32_t size;

  nn->last_stat = 0;

  /* TODO: Do the below requests concurrently.  */
  err = ensure_open (nn, O_WRITE);
  if (err)
    return err;

  /* We only really need to reserve 4 + 1 + 2 + 4 + 8 + 4 = 23
     bytes of space for the headers in the read_response message;
     but some servers are known to require at least 24.
     It is also important that we adjust the buffer size after
     potentially opening the file, since that may update the
     mac_buffer_size.  */
  size = adjust_buffer_size (nn, 24, *len);

  err = p9_rpc (P9_WRITE_REQUEST,
                "48d", nn->io_fid, (uint64_t) offset,
                (size_t) size, data,
                "4", &size);
  *len = err ? 0 : size;
  return err;
}

error_t
netfs_report_access (struct iouser *cred, struct node *np,
                     int *types)
{
  return 0;
}

void
netfs_node_norefs (struct node *np)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (np);

  free (nn->path);
  if (nn->dir.buffer.base != NULL)
    {
      err = vm_deallocate (mach_task_self (),
                           (vm_address_t) nn->dir.buffer.base,
                           nn->dir.buffer.allocated_size);
      assert_perror_backtrace (err);
    }

  /* TODO: Do these concurrently.  */
  err = p9_rpc (P9_CLUNK_REQUEST,
                "4", nn->walk_fid, "");
  if (err)
    error (0, err, "Failed to clunk fid");

  if (nn->io_fid != NO_FID)
    {
      err = p9_rpc (P9_CLUNK_REQUEST,
                    "4", nn->io_fid, "");
      if (err)
        error (0, err, "Failed to clunk fid");
    }

  pthread_mutex_unlock (&np->lock);
  pthread_mutex_destroy (&np->lock);
  free (np);
}

static size_t
round_size_up (size_t size, size_t align)
{
  return (size + align - 1) / align * align;
}

error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
                   int entry, int nentries, char **data,
                   mach_msg_type_number_t *datacnt,
                   vm_size_t bufsize, int *amt)
{
  error_t err;
  struct netnode *nn = netfs_node_netnode (dir);
  struct dir_buffer *db = &nn->dir.buffer;
  struct dirent64 *dirent;
  int can_reuse_io_fid;
  enum p9_message_type request_type;
  uint32_t receive_size;

  vm_size_t data_buffer_size = *datacnt;
  int data_was_allocated = 0;

  int
  make_space (size_t space)
  {
    error_t err;
    vm_address_t addr;
    size_t required_size = *datacnt + space;
    size_t alloc_size = round_page (required_size);

    if (bufsize != 0 && required_size > bufsize)
      return 0;
    if (required_size <= data_buffer_size)
      return 1;

    err = vm_allocate (mach_task_self (), &addr, alloc_size, 1);
    if (err)
      return 0;
    memcpy ((void *) addr, *data, *datacnt);
    if (data_was_allocated)
      vm_deallocate (mach_task_self (), (vm_address_t) *data,
                     data_buffer_size);
    *data = (void *) addr;
    data_buffer_size = alloc_size;
    data_was_allocated = 1;

    return 1;
  }

  *amt = 0;
  *datacnt = 0;

 parse_existing:
  while (entry >= nn->dir.next_entry && db->remaining_size > 0
         && (*amt < nentries || nentries == -1))
    {
      /* Look through the buffered data.  */
      if (p9_version >= P9_VERSION_2000_L)
        /* TODO: .L */
        assert_backtrace (0);
      else
        {
          struct p9_stat s = { 0 };
          size_t consumed = 0;
          size_t namelen, reclen;
          dev_t dev;
          int is_blockdev;

          err = p9_stat_deserialize (&s, db->remaining_size,
                                     db->ptr, &consumed);
          if (err)
            return err;

          if (entry > nn->dir.next_entry)
            goto next_entry;

          /* Record takes up as much space as struct dirent64, plus the space
             for the name, rounded up to the alignment.  Note that d_name is
             declared as a single-character array, which takes up exactly as
             much space as we need for the null terminator.  */
          namelen = strlen (s.name);
          reclen = round_size_up (sizeof (struct dirent64) + namelen,
                                  __alignof__ (struct dirent64));

          if (!make_space (reclen))
            {
              p9_stat_free (&s);
              break;
            }

          if (s.extension == NULL
              || !parse_extension_device (s.extension, &dev,
                                          &is_blockdev))
            is_blockdev = 0;

          dirent = (struct dirent64 *) (*data + *datacnt);
          dirent->d_ino = s.qid.path;
          dirent->d_reclen = reclen;
          dirent->d_type = p9_mode_to_d_type (s.mode, is_blockdev);
          dirent->d_namlen = namelen;
          memcpy (&dirent->d_name[0], s.name, namelen);
          dirent->d_name[namelen] = 0;


          *datacnt += reclen;
          (*amt)++;
          entry++;

 next_entry:
          db->ptr += consumed;
          db->remaining_size -= consumed;
          nn->dir.next_entry++;
          nn->dir.next_offset += consumed;

          p9_stat_free (&s);
        }
    }

  /* This is the end of the buffered data, deallocate it.  */
  if (db->remaining_size == 0 && db->base != NULL)
    {
      err = vm_deallocate (mach_task_self (), (vm_address_t) db->base,
                           db->allocated_size);
      assert_perror_backtrace (err);
      db->base = NULL;
      db->ptr = NULL;
      db->allocated_size = 0;
    }

  /* If we managed to fill in some entries (or none were
     requested in the first place), this is it.  */
  if (*amt > 0 || nentries == 0)
    return 0;

  /* We're going to load more data from the server.
     See if we can reuse the open I/O fid.  */
  can_reuse_io_fid = nn->io_fid != NO_FID && entry >= nn->dir.next_entry;

  /* TODO: Do the below requests concurrently.  */

  if (!can_reuse_io_fid)
    {
      /* Reset open_flags to force the ensure_open ()
         call below to recreate the fid.  */
      nn->open_flags = 0;
      /* The new fid will start from the beginning.  */
      nn->dir.next_entry = 0;
      nn->dir.next_offset = 0;

      /* Open a new fid for I/O.  */
      err = ensure_open (nn, O_READ);
      if (err)
        return err;
    }

  /* Allocate memory to hold read data.  */
  receive_size = adjust_buffer_size (nn, 24, vm_page_size * 2);
  err = vm_allocate (mach_task_self (), (vm_address_t *) &db->base,
                     receive_size, 1);
  if (err)
    return err;
  db->allocated_size = receive_size;

  /* Actually request the data from the server.  */
  if (p9_version >= P9_VERSION_2000_L)
    request_type = P9_READDIR_REQUEST;
  else
    request_type = P9_READ_REQUEST;

  err = p9_rpc (request_type,
                "484", nn->io_fid, nn->dir.next_offset,
                db->allocated_size,
                "d", &receive_size, db->base);

  /* If we got nothing, make sure to clean up.  */
  if (err || receive_size == 0)
    {
      vm_deallocate (mach_task_self (), (vm_address_t) db->base,
                     db->allocated_size);
      db->base = NULL;
      db->allocated_size = 0;
      db->remaining_size = 0;
    }
  if (err)
    return err;

  /* If there are no more entries, just say that.  */
  if (receive_size == 0)
    {
      *data = NULL;
      *datacnt = 0;
      return 0;
    }

  db->ptr = db->base;
  db->remaining_size = receive_size;
  goto parse_existing;
}
