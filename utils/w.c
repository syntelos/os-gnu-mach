/* Hurdish w

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <ctype.h>
#include <utmp.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>

#include <sys/fcntl.h>

#include <argp.h>
#include <argz.h>
#include <envz.h>
#include <idvec.h>
#include <ps.h>
#include <timefmt.h>
#include <error.h>

#define DEFAULT_FMT_STRING "~USER ~TTY ~FROM ~LOGIN ~IDLE ~WHAT"

extern char *canon_host (char *host);
extern char *shared_domain (char *host1, char *host2);
extern char *localhost ();

/* Long options without corresponding short ones.  -1 is EOF.  */
#define OPT_SORT	-4
#define OPT_FMT		-5

#define OA OPTION_ARG_OPTIONAL

static struct argp_option options[] =
{
  {"fmt",        OPT_FMT, "FMT",  0,  "Use the custom output-format FMT"},
  {"no-header",  'H',     0,      0,  "Don't print a descriptive header line"},
  {0,		 'h',     0,   OPTION_ALIAS | OPTION_HIDDEN}, /* BSD compat */
  {"reverse",    'r',     0,      0,  "Reverse the order of any sort"},
  {"sort",       OPT_SORT,"FIELD",0,  "Sort the output with respect to FIELD,"
                                     " backwards if FIELD is prefixed by `-'"},
  {0,            'i',     0,	  0,  "Sort output by idle time"},
  {"tty",        't',     "TTY",  OA, "Only show entries for terminal TTY"},
  {"width",      'w',     "WIDTH",OA, "If WIDTH is given, try to format the"
                                      " output for WIDTH columns, otherwise,"
				      " remove the default limit"},
  {"uptime",     'u',     0,      0,  "Only show the uptime and load info"},
  {"no-uptime",  'U',     0,      0,  "Don't show the uptime and load info"},
  {"raw-hosts",  'n',     0,      0,  "Show network addresses as numbers"},
  {0, 0}
};
static char *args_doc = "[USER...]";
static char *doc = 0;

/* The current time of day.  */
static struct timeval now;

/* True if we shouldn't attempt to show names for host addresses.  */
static int raw_hosts = 0;

struct w_hook
{
  struct utmp utmp;
  struct timeval idle;
  char *host;			/* A malloced host name.  */
};

#define W_PSTAT_USER	(PSTAT_USER_BASE << 0)
#define W_PSTAT_HOST	(PSTAT_USER_BASE << 1)
#define W_PSTAT_IDLE	(PSTAT_USER_BASE << 2)
#define W_PSTAT_LOGIN	(PSTAT_USER_BASE << 3)

static ps_flags_t
w_fetch (struct proc_stat *ps, ps_flags_t need, ps_flags_t have)
{
  struct w_hook *hook = ps->hook;

  if (! hook)
    return 0;			/* Can't do anything without w specific info.*/

  if (need & W_PSTAT_HOST)
    {
      struct utmp *utmp = &hook->utmp;
      if (utmp->ut_host[0])
	{
	  /* UTMP->ut_host might not be '\0' terminated; this copy is.  */
	  char ut_host[sizeof utmp->ut_host + 1];

	  strncpy (ut_host, utmp->ut_host, sizeof utmp->ut_host);
	  ut_host[sizeof utmp->ut_host] = '\0';

	  if (raw_hosts)
	    hook->host = strdup (ut_host);
	  else
	    {
	      char *sd;
	      hook->host = strdup (canon_host (ut_host) ?: ut_host);
	      sd = shared_domain (hook->host, localhost ());
	      if (sd)
		*sd = '\0';
	    }

	  have |= W_PSTAT_HOST;
	}
    }

  if (need & W_PSTAT_IDLE)
    {
      struct stat stat;
      ps_tty_t tty = ps->tty;

      hook->idle.tv_usec = 0;
      if (io_stat (tty->port, &stat) == 0)
	{
	  hook->idle.tv_sec = now.tv_sec - stat.st_mtime;
	  if (hook->idle.tv_sec > 0)
	    have |= W_PSTAT_IDLE;
	}
    }

  /* We can always return these.  */
  have |= (need & W_PSTAT_LOGIN);

  return have;
}

static void
w_cleanup (struct proc_stat *ps)
{
  if (ps->hook)
    {
      if (ps->flags & W_PSTAT_HOST)
	free (((struct w_hook *)ps->hook)->host);
      free (ps->hook);
    }
}

static void
w_get_idle (proc_stat_t ps, struct timeval *tv)
{
  struct w_hook *hook = ps->hook;
  *tv = hook->idle;
}
struct ps_getter w_idle_getter =
{"idle_time", W_PSTAT_IDLE, w_get_idle};

static void
w_get_login (proc_stat_t ps, struct timeval *tv)
{
  struct w_hook *hook = ps->hook;
  tv->tv_usec = 0;
  tv->tv_sec = hook ? hook->utmp.ut_time : 0;
}
struct ps_getter w_login_getter =
{"login_time", 0, w_get_login};

static void
w_get_user (proc_stat_t ps, char **user, unsigned *user_len)
{
  struct w_hook *hook = ps->hook;
  *user = hook->utmp.ut_name;
  *user_len = ((char *)memchr (*user, 0, UT_NAMESIZE) ?: *user) - *user;
}
struct ps_getter w_user_getter =
{"user", 0, w_get_user};

static void
w_get_host (proc_stat_t ps, char **host, unsigned *host_len)
{
  struct w_hook *hook = ps->hook;
  *host = hook->host;
  *host_len = strlen (*host);
}
struct ps_getter w_host_getter =
{"host", W_PSTAT_HOST, w_get_host};

extern error_t ps_emit_time (), ps_emit_string (), ps_emit_minutes ();
extern int ps_cmp_times (), ps_cmp_strings ();

struct ps_fmt_spec _w_specs[] =
{
  {"USER",  0, UT_NAMESIZE, &w_user_getter, ps_emit_string, ps_cmp_strings},
  {"LOGIN", "LOGIN@", -7,   &w_login_getter, /*ps_emit_time*/ps_emit_minutes,    ps_cmp_times},
  {"FROM",  0, UT_HOSTSIZE, &w_host_getter,  ps_emit_string,  ps_cmp_strings},
  {"IDLE",  0, -5,          &w_idle_getter,  ps_emit_minutes, ps_cmp_times},
  {"WHAT=args"},
  {0}
};
struct ps_fmt_specs w_specs = {_w_specs, &ps_std_fmt_specs};

/* Add to PROCS any processes in the foreground process group corresponding
   to U, attaching a struct w_hook to which ever process is deemed the most
   noteworthy.  */
static void
add_utmp_procs (struct proc_stat_list *procs, struct utmp *u)
{
  /* The tty name, with space for '\0' termination and an
     appropiate prefix.  */
  char tty[sizeof _PATH_DEV + sizeof u->ut_line];
  io_t tty_node;
  error_t err;
  pid_t pid;
  int pos;
  struct proc_stat *ps;

  strncpy (tty, u->ut_line, sizeof u->ut_line);
  tty[sizeof u->ut_line] = '\0'; /* Ensure it's '\0' terminated. */

  if (*tty != '/')
    /* Not an absolute path -- it must be in /dev, which insert. */
    {
      bcopy (tty, tty + sizeof _PATH_DEV - 1, strlen (tty) + 1);
      bcopy (_PATH_DEV, tty, sizeof _PATH_DEV - 1);
    }

  /* Now find which process group is the in foreground for TTY.  */
  tty_node = file_name_lookup (tty, 0, 0);
  if (tty_node == MACH_PORT_NULL)
    {
      error (0, errno, "%s", tty);
      return;
    }

  err = io_get_owner (tty_node, &pid);
  if (err)
    {
      error (0, err, "%s", tty);
      return;
    }

  /* The new process will get added at the end, so look for it there. */
  pos = proc_stat_list_num_procs (procs);
  if (pid >= 0)
    err = proc_stat_list_add_pid (procs, pid, &ps);
  else
    {
      struct proc_stat **pgrp_procs;
      unsigned num_procs;

      err = proc_stat_list_add_pgrp (procs, -pid, &pgrp_procs, &num_procs);
      if (! err)
	{
	  if (num_procs > 0)
	    ps = pgrp_procs[0]; /* Use the first one.  */
	  else
	    ps = 0;
	  free (pgrp_procs);
	}
    }
  if (err)
    {
      error (0, err, "%s (owner %s %d)",
	     tty, pid < 0 ? "pgrp" : "pid", pid < 0 ? -pid : pid);
      return;
    }

  if (ps)
    {
      ps->hook = malloc (sizeof (struct w_hook));
      if (ps->hook)
	bcopy (u, &((struct w_hook *)ps->hook)->utmp, sizeof *u);
    }
  else
    error (0, 0, "%s (owner %s %d): No processes",
	   tty, pid < 0 ? "pgrp" : "pid", pid < 0 ? -pid : pid);
}

/* Read into PROCS from the utmp file called NAME.  */
static void
read_utmp_procs (proc_stat_list_t procs, char *name)
{
  int rd, fd;
  struct utmp buf[64];

  fd = open (name, O_RDONLY);
  if (fd < 0)
    error (9, errno, "%s", name);

  while ((rd = read (fd, buf, sizeof (buf))) > 0)
    {
      struct utmp *u = buf;
      while (rd >= sizeof (*u))
	{
	  if (*u->ut_name && *u->ut_line)
	    /* An active entry.  */
	    add_utmp_procs (procs, u);
	  rd -= sizeof (*u);
	  u++;
	}
    }

  close (fd);
}

static void
uptime (proc_stat_list_t procs)
{
  struct timeval uptime;
  char uptime_rep[20], tod_rep[20];
  struct host_load_info *load;
  unsigned nusers = 0;
  int maybe_add_user (proc_stat_t ps) { if (ps->hook) nusers++; return 0; }

  proc_stat_list_for_each (procs, maybe_add_user);

  /* What do we do??? XXX */
  bzero (&uptime, sizeof (uptime));

  strftime (tod_rep, sizeof (tod_rep), "%r", localtime (&now.tv_sec));
  if (tod_rep[0] == '0')
    tod_rep[0] = ' ';		/* Get rid of bletcherous leading 0.  */

  errno = ps_host_load_info (&load);
  if (errno)
    error (0, errno, "ps_host_load_info");

  fmt_named_interval (&uptime, 0, uptime_rep, sizeof (uptime_rep));

  printf ("%s  up %s,  %u user%s,  load averages: %.2f, %.2f, %.2f\n",
	  tod_rep, uptime_rep, nusers, nusers == 1 ? "" : "s",
	  (double)load->avenrun[0] / (double)LOAD_SCALE,
	  (double)load->avenrun[1] / (double)LOAD_SCALE,
	  (double)load->avenrun[2] / (double)LOAD_SCALE);
}

extern void psout ();

void
main(int argc, char *argv[])
{
  error_t err;
  ps_context_t context;
  int output_width = -1;
  char *fmt_string = DEFAULT_FMT_STRING, *sort_key_name = NULL;
  bool sort_reverse = 0, print_heading = 1, show_entries = 1, show_uptime = 1;
  bool squash_bogus_fields = 1, squash_nominal_fields = 1;
  proc_stat_list_t procs;
#if 0
  char *tty_names = 0;
  unsigned num_tty_names = 0;
  struct idvec *only_uids = make_idvec (), *not_uids = make_idvec ();
#endif
  struct ps_user_hooks ps_hooks = { 0, w_fetch, w_cleanup };

  int has_hook (proc_stat_t ps) { return ps->hook != 0; }

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'H': case 'h': print_heading = 0; break;
	case 'i': sort_key_name = "idle"; break;
	case OPT_SORT: sort_key_name = arg; break;
	case OPT_FMT:  fmt_string = arg; break;
	case 'r': sort_reverse = 1; break;
	case 'u': show_entries = 0; break;
	case 'U': show_uptime = 0; break;
	case 'n': raw_hosts = 1; break;

	case ARGP_KEY_ARG:
	  break;

	default: return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  if (gettimeofday (&now, 0) < 0)
    error (0, errno, "gettimeofday");

  err = ps_context_create (getproc (), &context);
  if (err)
    error (2, err, "ps_context_create");

  err = proc_stat_list_create (context, &procs);
  if (err)
    error (3, err, "proc_stat_list_create");
  context->user_hooks = &ps_hooks;

  /* Parse our options.  */
  argp_parse (&argp, argc, argv, 0, 0);

  read_utmp_procs (procs, _PATH_UTMP);

  /* Keep only processes that have our hooks attached.  */
  proc_stat_list_filter1 (procs, has_hook, 0, 0);

  if (show_uptime)
    uptime (procs);

  if (show_entries)
    psout (procs, fmt_string, &w_specs, sort_key_name, sort_reverse,
	   output_width, print_heading,
	   squash_bogus_fields, squash_nominal_fields);

  exit(0);
}
