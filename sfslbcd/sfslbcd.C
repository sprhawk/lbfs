/*
 *
 * Copyright (C) 1998 David Mazieres (dm@uun.org)
 * Copyright (C) 2002 Benjie Chen (benjie@lcs.mit.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#include "async.h"
#include "arpc.h"
#include "sfsmisc.h"
#include "crypt.h"
#include "sfsclient.h"
#include "sfsconnect.h"
#include "axprt_compress.h"
#include "sfslbcd.h"
#include "rxx.h"
#include <errno.h>

#define LBFSCACHE "/var/tmp/lbfscache"

void
sfslbcd_connect(sfsprog*prog, ref<nfsserv> ns, int tcpfd,
                sfscd_mountarg *ma, sfsserver::fhcb cb, ptr<sfscon> c, str s)
{
  if (c) {
    rpc_ptr<sfs_connectok> cres;
    cres.alloc();
    sfs_connectok *p = cres;
    p->servinfo = c->servinfo;
    ma->cres = cres;
    vNew refcounted<server>(sfsserverargs (ns, tcpfd, prog, ma, cb));
  }
  else {
    warn <<  s;
    (*cb)(NULL);
  }
}

void
sfslbcd_getfd(sfsprog*prog, ref<nfsserv> ns, int tcpfd,
              sfscd_mountarg *ma, sfsserver::fhcb cb, int fd)
{
  if (!isunixsocket (fd))
    tcp_nodelay (fd);
  ptr<axprt> x = New refcounted<axprt_zcrypt>(fd, axprt_zcrypt::ps());
  sfs_connect_withx
    (ma->carg, wrap(&sfslbcd_connect, prog, ns, tcpfd, ma, cb), x);
}

void
sfslbcd_alloc(sfsprog *prog, ref<nfsserv> ns, int tcpfd,
              sfscd_mountarg *ma, sfsserver::fhcb cb)
{
  if (!ma->cres ||
      (ma->carg.civers == 5 && !sfs_parsepath (ma->carg.ci5->sname))) {
    str path = ma->carg.ci5->sname;
    const char *p = path;
    unsigned i;
    for (i=0; i<path.len(); i++) {
      if (p[i] == ':')
	break;
    }
    if (i<path.len() && substr(path,0,i) == "lbfs") {
      ma->carg.ci5->sname = substr(path,i+1);
      u_int16_t port;
      str location;
      if (!sfs_parsepath (ma->carg.ci5->sname, &location, NULL, &port) ||
	  !strchr(location, '.'))
	warn << ma->carg.ci5->sname << ": cannot parse path\n";
      else {
	tcpconnect
	  (location, port, wrap (&sfslbcd_getfd, prog, ns, tcpfd, ma, cb));
        return;
      }
    }
  }
  (*cb)(NULL);
}

server::server (const sfsserverargs &a)
  : sfsserver_auth (a), fc(50000, wrap(mkref(this), &server::remove_cache))
{
  cdir = strbuf(LBFSCACHE) << "/" << a.ma->carg.ci5->sname;
  if (mkdir(cdir.cstr(), 0755) < 0 && errno != EEXIST)
    fatal ("cannot create cache directory %s\n", cdir.cstr());
  for (int i=0; i<254; i++) {
    str f = cdir << "/" << i;
    if (mkdir(f.cstr(), 0755) < 0 && errno != EEXIST)
      fatal ("cannot create cache directory %s\n", f.cstr());
  }
}

int
main (int argc, char **argv)
{
  setprogname (argv[0]);
  warn ("version %s, pid %d\n", VERSION, getpid ());

  if (argc != 1)
    fatal ("usage: %s\n", progname.cstr ());

  sfsconst_init ();
  random_init_file (sfsdir << "/random_seed");
  server::keygen ();
  
  if (mkdir(LBFSCACHE, 0755) < 0 && errno != EEXIST)
    fatal ("cannot create cache directory %s\n", LBFSCACHE);

  if (ptr<axprt_unix> x = axprt_unix_stdin ())
    // doesn't need simulated close, but mount with close option
    vNew sfsprog (x, &sfslbcd_alloc, false, true);
  else
    fatal ("could not get connection to sfscd.\n");

  amain ();
}
