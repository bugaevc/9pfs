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
#include "fsys_S.h"
#include "fs_S.h"
#include <argp.h>
#include <mach.h>
#include <error.h>
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <hurd/ports.h>

char *p9_source;
FILE *p9_conn_in, *p9_conn_out;
char *p9_user_name;
uint32_t p9_uid = ~0;
uid_t p9_nobody_uid;
gid_t p9_nobody_gid;
char *p9_aname;
/* If not specified, allow everything.  */
mode_t p9_mode_mask = ~0;
int p9_dump_exchanges;
boolean_t p9_readonly;
char *p9_settrans_path;

static char *server_host;
static char *server_port = "564";
static int allow_ip = 46;


const char *argp_program_version = "9pfs 0.1.0";

static const char *const args_doc = "[USER@]HOST[:PORT][/ANAME]";
static const char *const doc = "9P remote filesystem translator for the Hurd."
"\vHOST may be an address or a hostname, in which case a TCP connection"
" is made to that hostname on an optionally provided PORT (which defaults"
" to 564).  Alternatively, HOST may name an absolute file path, in which"
" case the file will be opened, either as a socket or special file."
"  USER defaults to the running user.";

/* Options that can be set both at startup and at runtime.  */
static const struct argp_option common_options[] =
{
  {"aname", 'a', "NAME", 0, "Pick remote file tree"},
  {"mask", 'm', "MASK", OPTION_ARG_OPTIONAL, "Apply MASK (default: 0755)"
                                             " to file permissions"},
  {"readonly", 'r', 0, 0, "Disallow any modifications"},
  {"rdonly", 0, 0, OPTION_ALIAS | OPTION_HIDDEN},
  {"ro", 0, 0, OPTION_ALIAS | OPTION_HIDDEN},
  {"writable", 'w', 0, 0, "Allow modifications (on by default)"},
  {"rdwr", 0, 0, OPTION_ALIAS | OPTION_HIDDEN},
  {"rw", 0, 0, OPTION_ALIAS | OPTION_HIDDEN},
  {"dump-9p", 'D', 0, 0, "Dump 9P exchanges to stderr for debugging"},
  {0, 0},
};

static void
parse_user (char *start, char *end)
{
  char *parsed_end;

  /* Try to parse as numeric UID.  */
  p9_uid = strtoul (start, &parsed_end, 0);

  /* Have we sucessfully parsed the whole string?  */
  if (errno != 0 || parsed_end != end)
    /* That did not work, take it as a name.  */
    p9_user_name = strndup (start, end - start);
}

static error_t
parse_common_opt (int key, char *arg, struct argp_state *state)
{
  char *end;

  switch (key)
    {
    case 'a':
      p9_aname = arg;
      return 0;

    case 'm':
      if (arg)
        p9_mode_mask = strtoul (arg, &end, 8);
      else
        /* Disallow writing to anyone but the owner.  */
        p9_mode_mask = 0755;
      return 0;

    case 'r':
      p9_readonly = TRUE;
      return 0;

    case 'w':
      p9_readonly = FALSE;
      return 0;

    case 'D':
      p9_dump_exchanges = 1;
      return 0;

    default:
      return ARGP_ERR_UNKNOWN;
    }
}

static const struct argp common_argp = { common_options, parse_common_opt };

/* Startup options.  */

static const struct argp_option startup_options[] =
{
  {0, '4', 0, 0, "Connect using IPv4"},
  {0, '6', 0, 0, "Connect using IPv6"},
  {"settrans", 'S', "PATH", 0, "Set as active translator on PATH"
                               " (useful for debugging)"},
  {0, 0},
};

static error_t
parse_startup_opt (int key, char *arg, struct argp_state *state)
{
  char *ptr;

  switch (key)
    {
    case '4':
      allow_ip = 4;
      return 0;

    case '6':
      allow_ip = 6;
      return 0;

    case 'S':
        p9_settrans_path = arg;
        return 0;

    case ARGP_KEY_ARG:
      /* Only allow one positional argument.  */
      if (state->arg_num > 0)
        argp_usage (state);

      p9_source = arg;

      /* Parse the USER@ part.  */
      ptr = strchr (arg, '@');
      if (ptr)
        {
          parse_user (arg, ptr);
          arg = ptr + 1;
        }

      if (arg[0] == '/')
        {
          /* The host is actually a file path.
             In this case there can be no aname.  */
          server_host = arg;
          return 0;
        }

      /* Look for the :PORT part.  */
      ptr = strchr (arg, ':');
      if (ptr)
        {
          /* Everything up to the colon is the host.  */
          server_host = strndup (arg, ptr - arg);
          arg = ptr + 1;
          /* Everything up to a slash (if any) is the port.
             The rest is aname.  */
          ptr = strchrnul (arg, '/');
          server_port = strndup (arg, ptr - arg);
          if (*ptr)
            p9_aname = ptr;
          return 0;
        }

      /* Everything up to a slash (if any) is the host.  */
      ptr = strchrnul (arg, '/');
      server_host = strndup (arg, ptr - arg);
      /* The rest is aname.  */
      if (*ptr)
        p9_aname = ptr;
      return 0;

    case ARGP_KEY_NO_ARGS:
      argp_usage (state);
      return EINVAL;

    default:
      return ARGP_ERR_UNKNOWN;
    }
}

static const struct argp_child runtime_argp_children[] =
{
  {&common_argp},
  {0},
  {0},
};

static struct argp runtime_argp = { 0, 0, 0, 0, runtime_argp_children };

static const struct argp startup_argp =
{
  startup_options,
  parse_startup_opt,
  args_doc,
  doc,
  runtime_argp_children,
};

/* Adopted form libports/notify_S.h */
extern mig_routine_t ports_notify_server_routines[];
static inline mig_routine_t
ports_notify_server_routine (const mach_msg_header_t *inp)
{
  int msgh_id;

  msgh_id = inp->msgh_id - 64;

  if ((msgh_id > 8) || (msgh_id < 0))
    return 0;

  return ports_notify_server_routines[msgh_id];
}

/* Adopted from libports/interrupt_S.h */
extern mig_routine_t ports_interrupt_server_routines[];
static inline mig_routine_t
ports_interrupt_server_routine (const mach_msg_header_t *inp)
{
  int msgh_id;

  msgh_id = inp->msgh_id - 33000;

  if ((msgh_id > 0) || (msgh_id < 0))
    return 0;

  return ports_interrupt_server_routines[msgh_id];
}

static int
demuxer (mach_msg_header_t *inp,
         mach_msg_header_t *outp)
{
  mig_routine_t routine;

  if ((routine = io_server_routine (inp)) ||
      (routine = fsys_server_routine (inp)) ||
      (routine = fs_server_routine (inp)) ||
      (routine = ports_notify_server_routine (inp)) ||
      (routine = ports_interrupt_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }

  return FALSE;
}

static void
establish_connection ()
{
  error_t err;
  int fd = -1, fd_dup = -1;

  assert_backtrace (server_host);

  if (server_host[0] == '/')
    {
      struct sockaddr_un sockaddr;

      /* This is actually a file path.
         Perhaps it's a socket? Try connecting to it.  */
      fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
      if (fd < 0)
        error (1, errno, "socket");
      memset (&sockaddr, 0, sizeof (sockaddr));
      sockaddr.sun_family = PF_LOCAL;
      strncpy (sockaddr.sun_path, server_host,
               sizeof (sockaddr.sun_path) - 1);
      err = connect (fd, (const struct sockaddr *) &sockaddr,
                     sizeof (sockaddr));

      if (!err)
        {
          /* Cool, that worked.  */
        }
      else if (errno != ENOTSOCK)
        error (1, errno, "connect");
      else
        {
          /* Hmm, it's not a socket.
             Try just opening it directly.  */
          close (fd);
          fd = open (server_host, O_RDWR | O_CLOEXEC);
        }
    }
  else
    {
      struct addrinfo hints, *ais, *ai;

      /* Resolve the symbolic address to socket addresses.
         We may get multiple families and multiple addresses
         within each family.  */
      memset (&hints, 0, sizeof (hints));
      hints.ai_socktype = SOCK_STREAM;

      switch (allow_ip)
        {
        case 46:
          hints.ai_family = PF_UNSPEC;
          break;
        case 4:
          hints.ai_family = PF_INET;
          break;
        case 6:
          hints.ai_family = PF_INET6;
          break;
        default:
          assert_backtrace (0);
        }

      err = getaddrinfo (server_host, server_port,
                         &hints, &ais);
      if (err)
        error (1, 0, "getaddrinfo: %s", gai_strerror (err));

      for (ai = ais; ai; ai = ai->ai_next)
        {
          fd = socket (ai->ai_family,
                       ai->ai_socktype | SOCK_CLOEXEC,
                       ai->ai_protocol);
          if (fd < 0)
            error (1, errno, "socket");
          /* Try to connect to this address.  */
          err = connect (fd, ai->ai_addr, ai->ai_addrlen);
          if (!err)
            break;
          err = errno;
          close (fd);
        }
      if (err)
        error (1, err, "connect");
      /* Great, we have a connected socket.  */
      freeaddrinfo (ais);
    }

  if (fd < 0)
    error (1, errno, "Cannot open connection");

  fd_dup = fcntl (fd, F_DUPFD_CLOEXEC);
  if (fd_dup < 0)
    error (1, errno, "fcntl(F_DUPFD_CLOEXEC)");

  p9_conn_in = fdopen (fd, "r");
  p9_conn_out = fdopen (fd_dup, "w");
}

static void
lookup_nobody ()
{
  int rc;
  unsigned char *buf;
  size_t bufsize;
  struct passwd pwd, *ppwd;

  bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
  if (bufsize == -1)
    bufsize = 16384;

 again:
  buf = malloc (bufsize);
  if (buf == NULL)
    {
      error (0, errno, "malloc");
      goto not_found;
    }

  errno = 0;
  rc = getpwnam_r ("nobody", &pwd, buf, bufsize, &ppwd);
  free (buf);
  if (rc != 0 && errno == ERANGE)
    {
      bufsize *= 2;
      goto again;
    }

  if (rc != 0 || ppwd == NULL)
    goto not_found;

  p9_nobody_uid = pwd.pw_uid;
  p9_nobody_gid = pwd.pw_gid;
  return;

 not_found:
  p9_nobody_uid = 65534;
  p9_nobody_gid = 65534;
}

static error_t
lookup_current_user ()
{
  int rc;
  unsigned char *buf;
  size_t bufsize;
  struct passwd pwd, *ppwd;

  p9_uid = getuid ();

  bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
  if (bufsize == -1)
    bufsize = 16384;

 again:
  buf = malloc (bufsize);
  if (buf == NULL)
    return errno;

  errno = 0;
  rc = getpwuid_r (p9_uid, &pwd, buf, bufsize, &ppwd);
  if (rc != 0)
    {
      free (buf);
      if (errno == ERANGE)
        {
          bufsize *= 2;
          goto again;
        }
      return errno;
    }
  if (ppwd == NULL)
    {
      free (buf);
      return EINVAL;
    }

  p9_user_name = pwd.pw_name;

  free (buf);
  return 0;
}

int
main (int argc, char **argv)
{
  error_t err;

  argp_parse (&startup_argp, argc, argv, 0, 0, 0);

  if (p9_user_name == NULL && p9_uid == ~0)
    {
      err = lookup_current_user ();
      if (err)
        error (1, err, "failed to look up user %lu",
               (unsigned long) getuid ());
    }

  lookup_nobody ();

  /* Connect to the server.  */
  establish_connection ();

  p9_startup ();

  ports_manage_port_operations_multithread (p9_bucket,
                                            demuxer,
                                            0, 0, 0);
}
