/*
 * This file Copyright (C) 2005-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifdef HAVE_POSIX_FADVISE
 #ifdef _XOPEN_SOURCE
  #undef _XOPEN_SOURCE
 #endif
 #define _XOPEN_SOURCE 600
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#ifdef __APPLE__
 #include <fcntl.h>
#endif

#ifdef HAVE_FALLOCATE64
  /* FIXME can't find the right #include voodoo to pick up the declaration.. */
  extern int fallocate64 (int fd, int mode, uint64_t offset, uint64_t len);
#endif

#ifdef HAVE_XFS_XFS_H
 #include <xfs/xfs.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h> /* getrlimit */
#include <sys/resource.h> /* getrlimit */
#include <fcntl.h> /* O_LARGEFILE posix_fadvise */
#include <unistd.h> /* lseek (), write (), ftruncate (), pread (), pwrite (), etc */

#include "transmission.h"
#include "fdlimit.h"
#include "file.h"
#include "log.h"
#include "session.h"
#include "torrent.h" /* tr_isTorrent () */

#define dbgmsg(...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        tr_logAddDeep (__FILE__, __LINE__, NULL, __VA_ARGS__); \
    } \
  while (0)

/***
****
****  Local Files
****
***/

#ifndef O_LARGEFILE
 #define O_LARGEFILE 0
#endif

#ifndef O_BINARY
 #define O_BINARY 0
#endif

#ifndef O_SEQUENTIAL
 #define O_SEQUENTIAL 0
#endif


static bool
preallocate_file_sparse (int fd, uint64_t length)
{
  const char zero = '\0';
  bool success = 0;

  if (!length)
    success = true;

#ifdef HAVE_FALLOCATE64
  if (!success) /* fallocate64 is always preferred, so try it first */
    success = !fallocate64 (fd, 0, 0, length);
#endif

  if (!success) /* fallback: the old-style seek-and-write */
    success = (lseek (fd, length-1, SEEK_SET) != -1)
           && (write (fd, &zero, 1) != -1)
           && (ftruncate (fd, length) != -1);

  return success;
}

static bool
preallocate_file_full (const char * filename, uint64_t length)
{
  bool success = 0;

#ifdef _WIN32

  HANDLE hFile = CreateFile (filename, GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_FLAG_RANDOM_ACCESS, 0);
  if (hFile != INVALID_HANDLE_VALUE)
    {
      LARGE_INTEGER li;
      li.QuadPart = length;
      success = SetFilePointerEx (hFile, li, NULL, FILE_BEGIN) && SetEndOfFile (hFile);
      CloseHandle (hFile);
    }

#else

  int flags = O_RDWR | O_CREAT | O_LARGEFILE;
  int fd = open (filename, flags, 0666);
  if (fd >= 0)
    {
# ifdef HAVE_FALLOCATE64
      if (!success)
        success = !fallocate64 (fd, 0, 0, length);
# endif
# ifdef HAVE_XFS_XFS_H
      if (!success && platform_test_xfs_fd (fd))
        {
          xfs_flock64_t fl;
          fl.l_whence = 0;
          fl.l_start = 0;
          fl.l_len = length;
          success = !xfsctl (NULL, fd, XFS_IOC_RESVSP64, &fl);
        }
# endif
# ifdef __APPLE__
      if (!success)
        {
          fstore_t fst;
          fst.fst_flags = F_ALLOCATECONTIG;
          fst.fst_posmode = F_PEOFPOSMODE;
          fst.fst_offset = 0;
          fst.fst_length = length;
          fst.fst_bytesalloc = 0;
          success = !fcntl (fd, F_PREALLOCATE, &fst);
        }
# endif
# ifdef HAVE_POSIX_FALLOCATE
      if (!success)
        success = !posix_fallocate (fd, 0, length);
# endif

      if (!success) /* if nothing else works, do it the old-fashioned way */
        {
          uint8_t buf[ 4096 ];
          memset (buf, 0, sizeof (buf));
          success = true;
          while (success && (length > 0))
            {
              const int thisPass = MIN (length, sizeof (buf));
              success = write (fd, buf, thisPass) == thisPass;
              length -= thisPass;
            }
        }

      close (fd);
    }

#endif

  return success;
}


/* portability wrapper for fsync (). */
int
tr_fsync (int fd)
{
#ifdef _WIN32
  return _commit (fd);
#else
  return fsync (fd);
#endif
}


/* Like pread and pwrite, except that the position is undefined afterwards.
   And of course they are not thread-safe. */

/* don't use pread/pwrite on old versions of uClibc because they're buggy.
 * https://trac.transmissionbt.com/ticket/3826 */
#ifdef __UCLIBC__
#define TR_UCLIBC_CHECK_VERSION(major,minor,micro) \
  (__UCLIBC_MAJOR__ > (major) || \
   (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ > (minor)) || \
   (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ == (minor) && \
      __UCLIBC_SUBLEVEL__ >= (micro)))
#if !TR_UCLIBC_CHECK_VERSION (0,9,28)
 #undef HAVE_PREAD
 #undef HAVE_PWRITE
#endif
#endif

#ifdef __APPLE__
 #define HAVE_PREAD
 #define HAVE_PWRITE
#endif

ssize_t
tr_pread (int fd, void *buf, size_t count, off_t offset)
{
#ifdef HAVE_PREAD
  return pread (fd, buf, count, offset);
#else
  const off_t lrc = lseek (fd, offset, SEEK_SET);
  if (lrc < 0)
    return -1;
  return read (fd, buf, count);
#endif
}

ssize_t
tr_pwrite (int fd, const void *buf, size_t count, off_t offset)
{
#ifdef HAVE_PWRITE
  return pwrite (fd, buf, count, offset);
#else
  const off_t lrc = lseek (fd, offset, SEEK_SET);
  if (lrc < 0)
    return -1;
  return write (fd, buf, count);
#endif
}

int
tr_prefetch (int fd UNUSED, off_t offset UNUSED, size_t count UNUSED)
{
#ifdef HAVE_POSIX_FADVISE
  return posix_fadvise (fd, offset, count, POSIX_FADV_WILLNEED);
#elif defined (__APPLE__)
  struct radvisory radv;
  radv.ra_offset = offset;
  radv.ra_count = count;
  return fcntl (fd, F_RDADVISE, &radv);
#else
  return 0;
#endif
}

void
tr_set_file_for_single_pass (int fd)
{
  if (fd >= 0)
    {
      /* Set hints about the lookahead buffer and caching. It's okay
         for these to fail silently, so don't let them affect errno */
      const int err = errno;
#ifdef HAVE_POSIX_FADVISE
      posix_fadvise (fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
#ifdef __APPLE__
      fcntl (fd, F_RDAHEAD, 1);
      fcntl (fd, F_NOCACHE, 1);
#endif
      errno = err;
    }
}

static int
open_local_file (const char * filename, int flags)
{
  const int fd = open (filename, flags, 0666);
  tr_set_file_for_single_pass (fd);
  return fd;
}
int
tr_open_file_for_writing (const char * filename)
{
  return open_local_file (filename, O_LARGEFILE|O_BINARY|O_CREAT|O_WRONLY);
}
int
tr_open_file_for_scanning (const char * filename)
{
  return open_local_file (filename, O_LARGEFILE|O_BINARY|O_SEQUENTIAL|O_RDONLY);
}

void
tr_close_file (int fd)
{
  close (fd);
}

/*****
******
******
******
*****/

struct tr_cached_file
{
  bool is_writable;
  int fd;
  int torrent_id;
  tr_file_index_t file_index;
  time_t used_at;
};

static inline bool
cached_file_is_open (const struct tr_cached_file * o)
{
  assert (o != NULL);

  return o->fd >= 0;
}

static void
cached_file_close (struct tr_cached_file * o)
{
  assert (cached_file_is_open (o));

  tr_close_file (o->fd);
  o->fd = -1;
}

/**
 * returns 0 on success, or an errno value on failure.
 * errno values include ENOENT if the parent folder doesn't exist,
 * plus the errno values set by tr_mkdirp () and open ().
 */
static int
cached_file_open (struct tr_cached_file  * o,
                  const char             * filename,
                  bool                     writable,
                  tr_preallocation_mode    allocation,
                  uint64_t                 file_size)
{
  int flags;
  tr_sys_path_info info;
  bool already_existed;
  bool resize_needed;

  /* create subfolders, if any */
  if (writable)
    {
      char * dir = tr_sys_path_dirname (filename, NULL);
      const int err = tr_mkdirp (dir, 0777) ? errno : 0;
      if (err)
        {
          tr_logAddError (_("Couldn't create \"%1$s\": %2$s"), dir, tr_strerror (err));
          tr_free (dir);
          return err;
        }
      tr_free (dir);
    }

  already_existed = tr_sys_path_get_info (filename, 0, &info, NULL) && info.type == TR_SYS_PATH_IS_FILE;

  if (writable && !already_existed && (allocation == TR_PREALLOCATE_FULL))
    if (preallocate_file_full (filename, file_size))
      tr_logAddDebug ("Preallocated file \"%s\"", filename);

  /* we can't resize the file w/o write permissions */
  resize_needed = already_existed && (file_size < info.size);
  writable |= resize_needed;

  /* open the file */
  flags = writable ? (O_RDWR | O_CREAT) : O_RDONLY;
  flags |= O_LARGEFILE | O_BINARY | O_SEQUENTIAL;
  o->fd = open (filename, flags, 0666);

  if (o->fd == -1)
    {
      const int err = errno;
      tr_logAddError (_("Couldn't open \"%1$s\": %2$s"), filename, tr_strerror (err));
      return err;
    }

  /* If the file already exists and it's too large, truncate it.
   * This is a fringe case that happens if a torrent's been updated
   * and one of the updated torrent's files is smaller.
   * http://trac.transmissionbt.com/ticket/2228
   * https://bugs.launchpad.net/ubuntu/+source/transmission/+bug/318249
   */
  if (resize_needed && (ftruncate (o->fd, file_size) == -1))
    {
      const int err = errno;
      tr_logAddError (_("Couldn't truncate \"%1$s\": %2$s"), filename, tr_strerror (err));
      return err;
    }

  if (writable && !already_existed && (allocation == TR_PREALLOCATE_SPARSE))
    preallocate_file_sparse (o->fd, file_size);

  /* Many (most?) clients request blocks in ascending order,
   * so increase the readahead buffer.
   * Also, disable OS-level caching because "inactive memory" angers users. */
  tr_set_file_for_single_pass (o->fd);

  return 0;
}

/***
****
***/

struct tr_fileset
{
  struct tr_cached_file * begin;
  const struct tr_cached_file * end;
};

static void
fileset_construct (struct tr_fileset * set, int n)
{
  struct tr_cached_file * o;
  const struct tr_cached_file TR_CACHED_FILE_INIT = { 0, -1, 0, 0, 0 };

  set->begin = tr_new (struct tr_cached_file, n);
  set->end = set->begin + n;

  for (o=set->begin; o!=set->end; ++o)
    *o = TR_CACHED_FILE_INIT;
}

static void
fileset_close_all (struct tr_fileset * set)
{
  struct tr_cached_file * o;

  if (set != NULL)
    for (o=set->begin; o!=set->end; ++o)
      if (cached_file_is_open (o))
        cached_file_close (o);
}

static void
fileset_destruct (struct tr_fileset * set)
{
  fileset_close_all (set);
  tr_free (set->begin);
  set->end = set->begin = NULL;
}

static void
fileset_close_torrent (struct tr_fileset * set, int torrent_id)
{
  struct tr_cached_file * o;

  if (set != NULL)
    for (o=set->begin; o!=set->end; ++o)
      if ((o->torrent_id == torrent_id) && cached_file_is_open (o))
        cached_file_close (o);
}

static struct tr_cached_file *
fileset_lookup (struct tr_fileset * set, int torrent_id, tr_file_index_t i)
{
  struct tr_cached_file * o;

  if (set != NULL)
    for (o=set->begin; o!=set->end; ++o)
      if ((torrent_id == o->torrent_id) && (i == o->file_index) && cached_file_is_open (o))
        return o;

  return NULL;
}

static struct tr_cached_file *
fileset_get_empty_slot (struct tr_fileset * set)
{
  struct tr_cached_file * cull = NULL;

  if (set->begin != NULL)
    {
      struct tr_cached_file * o;

      /* try to find an unused slot */
      for (o=set->begin; o!=set->end; ++o)
        if (!cached_file_is_open (o))
          return o;

      /* all slots are full... recycle the least recently used */
      for (cull=NULL, o=set->begin; o!=set->end; ++o)
        if (!cull || o->used_at < cull->used_at)
          cull = o;

      cached_file_close (cull);
    }

  return cull;
}

/***
****
****  Startup / Shutdown
****
***/

struct tr_fdInfo
{
  int peerCount;
  struct tr_fileset fileset;
};

static void
ensureSessionFdInfoExists (tr_session * session)
{
  assert (tr_isSession (session));

  if (session->fdInfo == NULL)
    {
      struct rlimit limit;
      struct tr_fdInfo * i;
      const int FILE_CACHE_SIZE = 32;

      /* Create the local file cache */
      i = tr_new0 (struct tr_fdInfo, 1);
      fileset_construct (&i->fileset, FILE_CACHE_SIZE);
      session->fdInfo = i;

      /* set the open-file limit to the largest safe size wrt FD_SETSIZE */
      if (!getrlimit (RLIMIT_NOFILE, &limit))
        {
          const int old_limit = (int) limit.rlim_cur;
          const int new_limit = MIN (limit.rlim_max, FD_SETSIZE);
          if (new_limit != old_limit)
            {
              limit.rlim_cur = new_limit;
              setrlimit (RLIMIT_NOFILE, &limit);
              getrlimit (RLIMIT_NOFILE, &limit);
              tr_logAddInfo ("Changed open file limit from %d to %d", old_limit, (int)limit.rlim_cur);
            }
        }
    }
}

void
tr_fdClose (tr_session * session)
{
  if (session && session->fdInfo)
    {
      struct tr_fdInfo * i = session->fdInfo;
      fileset_destruct (&i->fileset);
      tr_free (i);
      session->fdInfo = NULL;
    }
}

/***
****
***/

static struct tr_fileset*
get_fileset (tr_session * session)
{
  if (!session)
    return NULL;

  ensureSessionFdInfoExists (session);
  return &session->fdInfo->fileset;
}

void
tr_fdFileClose (tr_session * s, const tr_torrent * tor, tr_file_index_t i)
{
  struct tr_cached_file * o;

  if ((o = fileset_lookup (get_fileset (s), tr_torrentId (tor), i)))
    {
      /* flush writable files so that their mtimes will be
       * up-to-date when this function returns to the caller... */
      if (o->is_writable)
        tr_fsync (o->fd);

      cached_file_close (o);
    }
}

int
tr_fdFileGetCached (tr_session * s, int torrent_id, tr_file_index_t i, bool writable)
{
  struct tr_cached_file * o = fileset_lookup (get_fileset (s), torrent_id, i);

  if (!o || (writable && !o->is_writable))
    return -1;

  o->used_at = tr_time ();
  return o->fd;
}

#ifdef __APPLE__
 #define TR_STAT_MTIME(sb)((sb).st_mtimespec.tv_sec)
#else
 #define TR_STAT_MTIME(sb)((sb).st_mtime)
#endif

bool
tr_fdFileGetCachedMTime (tr_session * s, int torrent_id, tr_file_index_t i, time_t * mtime)
{
  bool success;
  struct stat sb;
  struct tr_cached_file * o = fileset_lookup (get_fileset (s), torrent_id, i);

  if ((success = (o != NULL) && !fstat (o->fd, &sb)))
    *mtime = TR_STAT_MTIME (sb);

  return success;
}

void
tr_fdTorrentClose (tr_session * session, int torrent_id)
{
  assert (tr_sessionIsLocked (session));

  fileset_close_torrent (get_fileset (session), torrent_id);
}

/* returns an fd on success, or a -1 on failure and sets errno */
int
tr_fdFileCheckout (tr_session             * session,
                   int                      torrent_id,
                   tr_file_index_t          i,
                   const char             * filename,
                   bool                     writable,
                   tr_preallocation_mode    allocation,
                   uint64_t                 file_size)
{
  struct tr_fileset * set = get_fileset (session);
  struct tr_cached_file * o = fileset_lookup (set, torrent_id, i);

  if (o && writable && !o->is_writable)
    cached_file_close (o); /* close it so we can reopen in rw mode */
  else if (!o)
    o = fileset_get_empty_slot (set);

  if (!cached_file_is_open (o))
    {
      const int err = cached_file_open (o, filename, writable, allocation, file_size);
      if (err)
        {
          errno = err;
          return -1;
        }

      dbgmsg ("opened '%s' writable %c", filename, writable?'y':'n');
      o->is_writable = writable;
    }

  dbgmsg ("checking out '%s'", filename);
  o->torrent_id = torrent_id;
  o->file_index = i;
  o->used_at = tr_time ();
  return o->fd;
}

/***
****
****  Sockets
****
***/

int
tr_fdSocketCreate (tr_session * session, int domain, int type)
{
  int s = -1;
  struct tr_fdInfo * gFd;
  assert (tr_isSession (session));

  ensureSessionFdInfoExists (session);
  gFd = session->fdInfo;

  if (gFd->peerCount < session->peerLimit)
    if ((s = socket (domain, type, 0)) < 0)
      if (sockerrno != EAFNOSUPPORT)
        tr_logAddError (_("Couldn't create socket: %s"), tr_strerror (sockerrno));

  if (s > -1)
    ++gFd->peerCount;

  assert (gFd->peerCount >= 0);

  if (s >= 0)
    {
      static bool buf_logged = false;
      if (!buf_logged)
        {
          int i;
          socklen_t size = sizeof (int);
          buf_logged = true;
          getsockopt (s, SOL_SOCKET, SO_SNDBUF, &i, &size);
          tr_logAddDebug ("SO_SNDBUF size is %d", i);
          getsockopt (s, SOL_SOCKET, SO_RCVBUF, &i, &size);
          tr_logAddDebug ("SO_RCVBUF size is %d", i);
        }
    }

  return s;
}

int
tr_fdSocketAccept (tr_session * s, int sockfd, tr_address * addr, tr_port * port)
{
  int fd;
  unsigned int len;
  struct tr_fdInfo * gFd;
  struct sockaddr_storage sock;

  assert (tr_isSession (s));
  assert (addr);
  assert (port);

  ensureSessionFdInfoExists (s);
  gFd = s->fdInfo;

  len = sizeof (struct sockaddr_storage);
  fd = accept (sockfd, (struct sockaddr *) &sock, &len);

  if (fd >= 0)
    {
      if ((gFd->peerCount < s->peerLimit)
          && tr_address_from_sockaddr_storage (addr, port, &sock))
        {
          ++gFd->peerCount;
        }
        else
        {
          tr_netCloseSocket (fd);
          fd = -1;
        }
    }

  return fd;
}

void
tr_fdSocketClose (tr_session * session, int fd)
{
  assert (tr_isSession (session));

  if (session->fdInfo != NULL)
    {
      struct tr_fdInfo * gFd = session->fdInfo;

      if (fd >= 0)
        {
          tr_netCloseSocket (fd);
          --gFd->peerCount;
        }

      assert (gFd->peerCount >= 0);
    }
}
