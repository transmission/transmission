/*
 * This file Copyright (C) 2005-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#ifndef _WIN32
#include <sys/time.h> /* getrlimit */
#include <sys/resource.h> /* getrlimit */
#endif

#include "transmission.h"
#include "error.h"
#include "error-types.h"
#include "fdlimit.h"
#include "file.h"
#include "log.h"
#include "session.h"
#include "torrent.h" /* tr_isTorrent() */
#include "tr-assert.h"

#define dbgmsg(...) tr_logAddDeepNamed(NULL, __VA_ARGS__)

/***
****
****  Local Files
****
***/

static bool preallocate_file_sparse(tr_sys_file_t fd, uint64_t length, tr_error** error)
{
    tr_error* my_error = NULL;

    if (length == 0)
    {
        return true;
    }

    if (tr_sys_file_preallocate(fd, length, TR_SYS_FILE_PREALLOC_SPARSE, &my_error))
    {
        return true;
    }

    dbgmsg("Preallocating (sparse, normal) failed (%d): %s", my_error->code, my_error->message);

    if (!TR_ERROR_IS_ENOSPC(my_error->code))
    {
        char const zero = '\0';

        tr_error_clear(&my_error);

        /* fallback: the old-style seek-and-write */
        if (tr_sys_file_write_at(fd, &zero, 1, length - 1, NULL, &my_error) && tr_sys_file_truncate(fd, length, &my_error))
        {
            return true;
        }

        dbgmsg("Preallocating (sparse, fallback) failed (%d): %s", my_error->code, my_error->message);
    }

    tr_error_propagate(error, &my_error);
    return false;
}

static bool preallocate_file_full(tr_sys_file_t fd, uint64_t length, tr_error** error)
{
    tr_error* my_error = NULL;

    if (length == 0)
    {
        return true;
    }

    if (tr_sys_file_preallocate(fd, length, 0, &my_error))
    {
        return true;
    }

    dbgmsg("Preallocating (full, normal) failed (%d): %s", my_error->code, my_error->message);

    if (!TR_ERROR_IS_ENOSPC(my_error->code))
    {
        uint8_t buf[4096];
        bool success = true;

        memset(buf, 0, sizeof(buf));
        tr_error_clear(&my_error);

        /* fallback: the old-fashioned way */
        while (success && length > 0)
        {
            uint64_t const thisPass = MIN(length, sizeof(buf));
            uint64_t bytes_written;
            success = tr_sys_file_write(fd, buf, thisPass, &bytes_written, &my_error);
            length -= bytes_written;
        }

        if (success)
        {
            return true;
        }

        dbgmsg("Preallocating (full, fallback) failed (%d): %s", my_error->code, my_error->message);
    }

    tr_error_propagate(error, &my_error);
    return false;
}

/*****
******
******
******
*****/

struct tr_cached_file
{
    bool is_writable;
    tr_sys_file_t fd;
    int torrent_id;
    tr_file_index_t file_index;
    time_t used_at;
};

static inline bool cached_file_is_open(struct tr_cached_file const* o)
{
    TR_ASSERT(o != NULL);

    return o->fd != TR_BAD_SYS_FILE;
}

static void cached_file_close(struct tr_cached_file* o)
{
    TR_ASSERT(cached_file_is_open(o));

    tr_sys_file_close(o->fd, NULL);
    o->fd = TR_BAD_SYS_FILE;
}

/**
 * returns 0 on success, or an errno value on failure.
 * errno values include ENOENT if the parent folder doesn't exist,
 * plus the errno values set by tr_sys_dir_create () and tr_sys_file_open ().
 */
static int cached_file_open(struct tr_cached_file* o, char const* filename, bool writable, tr_preallocation_mode allocation,
    uint64_t file_size)
{
    int flags;
    tr_sys_path_info info;
    bool already_existed;
    bool resize_needed;
    tr_sys_file_t fd = TR_BAD_SYS_FILE;
    tr_error* error = NULL;

    /* create subfolders, if any */
    if (writable)
    {
        char* dir = tr_sys_path_dirname(filename, &error);

        if (dir == NULL)
        {
            tr_logAddError(_("Couldn't get directory for \"%1$s\": %2$s"), filename, error->message);
            goto fail;
        }

        if (!tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0777, &error))
        {
            tr_logAddError(_("Couldn't create \"%1$s\": %2$s"), dir, error->message);
            tr_free(dir);
            goto fail;
        }

        tr_free(dir);
    }

    already_existed = tr_sys_path_get_info(filename, 0, &info, NULL) && info.type == TR_SYS_PATH_IS_FILE;

    /* we can't resize the file w/o write permissions */
    resize_needed = already_existed && (file_size < info.size);
    writable |= resize_needed;

    /* open the file */
    flags = writable ? (TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE) : 0;
    flags |= TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL;
    fd = tr_sys_file_open(filename, flags, 0666, &error);

    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddError(_("Couldn't open \"%1$s\": %2$s"), filename, error->message);
        goto fail;
    }

    if (writable && !already_existed && allocation != TR_PREALLOCATE_NONE)
    {
        bool success = false;
        char const* type = NULL;

        if (allocation == TR_PREALLOCATE_FULL)
        {
            success = preallocate_file_full(fd, file_size, &error);
            type = _("full");
        }
        else if (allocation == TR_PREALLOCATE_SPARSE)
        {
            success = preallocate_file_sparse(fd, file_size, &error);
            type = _("sparse");
        }

        TR_ASSERT(type != NULL);

        if (!success)
        {
            tr_logAddError(_("Couldn't preallocate file \"%1$s\" (%2$s, size: %3$" PRIu64 "): %4$s"),
                filename, type, file_size, error->message);
            goto fail;
        }

        tr_logAddDebug(_("Preallocated file \"%1$s\" (%2$s, size: %3$" PRIu64 ")"), filename, type, file_size);
    }

    /* If the file already exists and it's too large, truncate it.
     * This is a fringe case that happens if a torrent's been updated
     * and one of the updated torrent's files is smaller.
     * https://trac.transmissionbt.com/ticket/2228
     * https://bugs.launchpad.net/ubuntu/+source/transmission/+bug/318249
     */
    if (resize_needed && !tr_sys_file_truncate(fd, file_size, &error))
    {
        tr_logAddError(_("Couldn't truncate \"%1$s\": %2$s"), filename, error->message);
        goto fail;
    }

    o->fd = fd;
    return 0;

fail:
    {
        int const err = error->code;
        tr_error_free(error);

        if (fd != TR_BAD_SYS_FILE)
        {
            tr_sys_file_close(fd, NULL);
        }

        return err;
    }
}

/***
****
***/

struct tr_fileset
{
    struct tr_cached_file* begin;
    struct tr_cached_file const* end;
};

static void fileset_construct(struct tr_fileset* set, int n)
{
    struct tr_cached_file const TR_CACHED_FILE_INIT =
    {
        .is_writable = false,
        .fd = TR_BAD_SYS_FILE,
        .torrent_id = 0,
        .file_index = 0,
        .used_at = 0
    };

    set->begin = tr_new(struct tr_cached_file, n);
    set->end = set->begin + n;

    for (struct tr_cached_file* o = set->begin; o != set->end; ++o)
    {
        *o = TR_CACHED_FILE_INIT;
    }
}

static void fileset_close_all(struct tr_fileset* set)
{
    if (set != NULL)
    {
        for (struct tr_cached_file* o = set->begin; o != set->end; ++o)
        {
            if (cached_file_is_open(o))
            {
                cached_file_close(o);
            }
        }
    }
}

static void fileset_destruct(struct tr_fileset* set)
{
    fileset_close_all(set);
    tr_free(set->begin);
    set->end = set->begin = NULL;
}

static void fileset_close_torrent(struct tr_fileset* set, int torrent_id)
{
    if (set != NULL)
    {
        for (struct tr_cached_file* o = set->begin; o != set->end; ++o)
        {
            if (o->torrent_id == torrent_id && cached_file_is_open(o))
            {
                cached_file_close(o);
            }
        }
    }
}

static struct tr_cached_file* fileset_lookup(struct tr_fileset* set, int torrent_id, tr_file_index_t i)
{
    if (set != NULL)
    {
        for (struct tr_cached_file* o = set->begin; o != set->end; ++o)
        {
            if (torrent_id == o->torrent_id && i == o->file_index && cached_file_is_open(o))
            {
                return o;
            }
        }
    }

    return NULL;
}

static struct tr_cached_file* fileset_get_empty_slot(struct tr_fileset* set)
{
    struct tr_cached_file* cull = NULL;

    if (set->begin != NULL)
    {
        /* try to find an unused slot */
        for (struct tr_cached_file* o = set->begin; o != set->end; ++o)
        {
            if (!cached_file_is_open(o))
            {
                return o;
            }
        }

        /* all slots are full... recycle the least recently used */
        for (struct tr_cached_file* o = set->begin; o != set->end; ++o)
        {
            if (cull == NULL || o->used_at < cull->used_at)
            {
                cull = o;
            }
        }

        cached_file_close(cull);
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

static void ensureSessionFdInfoExists(tr_session* session)
{
    TR_ASSERT(tr_isSession(session));

    if (session->fdInfo == NULL)
    {
        struct tr_fdInfo* i;
        int const FILE_CACHE_SIZE = 32;

        /* Create the local file cache */
        i = tr_new0(struct tr_fdInfo, 1);
        fileset_construct(&i->fileset, FILE_CACHE_SIZE);
        session->fdInfo = i;

#ifndef _WIN32

        /* set the open-file limit to the largest safe size wrt FD_SETSIZE */
        struct rlimit limit;

        if (getrlimit(RLIMIT_NOFILE, &limit) == 0)
        {
            int const old_limit = (int)limit.rlim_cur;
            int const new_limit = MIN(limit.rlim_max, FD_SETSIZE);

            if (new_limit != old_limit)
            {
                limit.rlim_cur = new_limit;
                setrlimit(RLIMIT_NOFILE, &limit);
                getrlimit(RLIMIT_NOFILE, &limit);
                tr_logAddInfo("Changed open file limit from %d to %d", old_limit, (int)limit.rlim_cur);
            }
        }

#endif
    }
}

void tr_fdClose(tr_session* session)
{
    if (session != NULL && session->fdInfo != NULL)
    {
        struct tr_fdInfo* i = session->fdInfo;
        fileset_destruct(&i->fileset);
        tr_free(i);
        session->fdInfo = NULL;
    }
}

/***
****
***/

static struct tr_fileset* get_fileset(tr_session* session)
{
    if (session == NULL)
    {
        return NULL;
    }

    ensureSessionFdInfoExists(session);
    return &session->fdInfo->fileset;
}

void tr_fdFileClose(tr_session* s, tr_torrent const* tor, tr_file_index_t i)
{
    struct tr_cached_file* o;

    if ((o = fileset_lookup(get_fileset(s), tr_torrentId(tor), i)) != NULL)
    {
        /* flush writable files so that their mtimes will be
         * up-to-date when this function returns to the caller... */
        if (o->is_writable)
        {
            tr_sys_file_flush(o->fd, NULL);
        }

        cached_file_close(o);
    }
}

tr_sys_file_t tr_fdFileGetCached(tr_session* s, int torrent_id, tr_file_index_t i, bool writable)
{
    struct tr_cached_file* o = fileset_lookup(get_fileset(s), torrent_id, i);

    if (o == NULL || (writable && !o->is_writable))
    {
        return TR_BAD_SYS_FILE;
    }

    o->used_at = tr_time();
    return o->fd;
}

bool tr_fdFileGetCachedMTime(tr_session* s, int torrent_id, tr_file_index_t i, time_t* mtime)
{
    bool success;
    tr_sys_path_info info;
    struct tr_cached_file* o = fileset_lookup(get_fileset(s), torrent_id, i);

    if ((success = o != NULL && tr_sys_file_get_info(o->fd, &info, NULL)))
    {
        *mtime = info.last_modified_at;
    }

    return success;
}

void tr_fdTorrentClose(tr_session* session, int torrent_id)
{
    TR_ASSERT(tr_sessionIsLocked(session));

    fileset_close_torrent(get_fileset(session), torrent_id);
}

/* returns an fd on success, or a TR_BAD_SYS_FILE on failure and sets errno */
tr_sys_file_t tr_fdFileCheckout(tr_session* session, int torrent_id, tr_file_index_t i, char const* filename, bool writable,
    tr_preallocation_mode allocation, uint64_t file_size)
{
    struct tr_fileset* set = get_fileset(session);
    struct tr_cached_file* o = fileset_lookup(set, torrent_id, i);

    if (o != NULL && writable && !o->is_writable)
    {
        cached_file_close(o); /* close it so we can reopen in rw mode */
    }
    else if (o == NULL)
    {
        o = fileset_get_empty_slot(set);
    }

    if (!cached_file_is_open(o))
    {
        int const err = cached_file_open(o, filename, writable, allocation, file_size);

        if (err != 0)
        {
            errno = err;
            return TR_BAD_SYS_FILE;
        }

        dbgmsg("opened '%s' writable %c", filename, writable ? 'y' : 'n');
        o->is_writable = writable;
    }

    dbgmsg("checking out '%s'", filename);
    o->torrent_id = torrent_id;
    o->file_index = i;
    o->used_at = tr_time();
    return o->fd;
}

/***
****
****  Sockets
****
***/

tr_socket_t tr_fdSocketCreate(tr_session* session, int domain, int type)
{
    TR_ASSERT(tr_isSession(session));

    tr_socket_t s = TR_BAD_SOCKET;
    struct tr_fdInfo* gFd;

    ensureSessionFdInfoExists(session);
    gFd = session->fdInfo;

    if (gFd->peerCount < session->peerLimit)
    {
        if ((s = socket(domain, type, 0)) == TR_BAD_SOCKET)
        {
            if (sockerrno != EAFNOSUPPORT)
            {
                char err_buf[512];
                tr_logAddError(_("Couldn't create socket: %s"), tr_net_strerror(err_buf, sizeof(err_buf), sockerrno));
            }
        }
    }

    if (s != TR_BAD_SOCKET)
    {
        ++gFd->peerCount;
    }

    TR_ASSERT(gFd->peerCount >= 0);

    if (s != TR_BAD_SOCKET)
    {
        static bool buf_logged = false;

        if (!buf_logged)
        {
            int i = 0;
            socklen_t size = sizeof(i);

            if (getsockopt(s, SOL_SOCKET, SO_SNDBUF, (void*)&i, &size) != -1)
            {
                tr_logAddDebug("SO_SNDBUF size is %d", i);
            }

            i = 0;
            size = sizeof(i);

            if (getsockopt(s, SOL_SOCKET, SO_RCVBUF, (void*)&i, &size) != -1)
            {
                tr_logAddDebug("SO_RCVBUF size is %d", i);
            }

            buf_logged = true;
        }
    }

    return s;
}

tr_socket_t tr_fdSocketAccept(tr_session* s, tr_socket_t sockfd, tr_address* addr, tr_port* port)
{
    TR_ASSERT(tr_isSession(s));
    TR_ASSERT(addr != NULL);
    TR_ASSERT(port != NULL);

    tr_socket_t fd;
    socklen_t len;
    struct tr_fdInfo* gFd;
    struct sockaddr_storage sock;

    ensureSessionFdInfoExists(s);
    gFd = s->fdInfo;

    len = sizeof(struct sockaddr_storage);
    fd = accept(sockfd, (struct sockaddr*)&sock, &len);

    if (fd != TR_BAD_SOCKET)
    {
        if (gFd->peerCount < s->peerLimit && tr_address_from_sockaddr_storage(addr, port, &sock))
        {
            ++gFd->peerCount;
        }
        else
        {
            tr_netCloseSocket(fd);
            fd = TR_BAD_SOCKET;
        }
    }

    return fd;
}

void tr_fdSocketClose(tr_session* session, tr_socket_t fd)
{
    TR_ASSERT(tr_isSession(session));

    if (session->fdInfo != NULL)
    {
        struct tr_fdInfo* gFd = session->fdInfo;

        if (fd != TR_BAD_SOCKET)
        {
            tr_netCloseSocket(fd);
            --gFd->peerCount;
        }

        TR_ASSERT(gFd->peerCount >= 0);
    }
}
