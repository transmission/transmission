/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <stdlib.h> /* qsort */
#include <string.h> /* strcmp, strlen */

#include <event2/util.h> /* evutil_ascii_strcasecmp() */

#include "transmission.h"
#include "crypto-utils.h" /* tr_sha1 */
#include "error.h"
#include "file.h"
#include "log.h"
#include "session.h"
#include "makemeta.h"
#include "platform.h" /* threads, locks */
#include "tr-assert.h"
#include "utils.h" /* buildpath */
#include "variant.h"
#include "version.h"

/****
*****
****/

struct FileList
{
    uint64_t size;
    char* filename;
    struct FileList* next;
};

static struct FileList* getFiles(char const* dir, char const* base, struct FileList* list)
{
    if (dir == NULL || base == NULL)
    {
        return NULL;
    }

    char* buf;
    tr_sys_path_info info;
    tr_error* error = NULL;

    buf = tr_buildPath(dir, base, NULL);

    if (!tr_sys_path_get_info(buf, 0, &info, &error))
    {
        tr_logAddError(_("Torrent Creator is skipping file \"%s\": %s"), buf, error->message);
        tr_free(buf);
        tr_error_free(error);
        return list;
    }

    tr_sys_dir_t odir = info.type == TR_SYS_PATH_IS_DIRECTORY ? tr_sys_dir_open(buf, NULL) : TR_BAD_SYS_DIR;

    if (odir != TR_BAD_SYS_DIR)
    {
        char const* name;

        while ((name = tr_sys_dir_read_name(odir, NULL)) != NULL)
        {
            if (name[0] != '.') /* skip dotfiles */
            {
                list = getFiles(buf, name, list);
            }
        }

        tr_sys_dir_close(odir, NULL);
    }
    else if (info.type == TR_SYS_PATH_IS_FILE && info.size > 0)
    {
        struct FileList* node = tr_new(struct FileList, 1);
        node->size = info.size;
        node->filename = tr_strdup(buf);
        node->next = list;
        list = node;
    }

    tr_free(buf);
    return list;
}

static uint32_t bestPieceSize(uint64_t totalSize)
{
    uint32_t const KiB = 1024;
    uint32_t const MiB = 1048576;
    uint32_t const GiB = 1073741824;

    if (totalSize >= 2 * GiB)
    {
        return 2 * MiB;
    }

    if (totalSize >= 1 * GiB)
    {
        return 1 * MiB;
    }

    if (totalSize >= 512 * MiB)
    {
        return 512 * KiB;
    }

    if (totalSize >= 350 * MiB)
    {
        return 256 * KiB;
    }

    if (totalSize >= 150 * MiB)
    {
        return 128 * KiB;
    }

    if (totalSize >= 50 * MiB)
    {
        return 64 * KiB;
    }

    return 32 * KiB; /* less than 50 meg */
}

static int builderFileCompare(void const* va, void const* vb)
{
    tr_metainfo_builder_file const* a = va;
    tr_metainfo_builder_file const* b = vb;

    return evutil_ascii_strcasecmp(a->filename, b->filename);
}

tr_metainfo_builder* tr_metaInfoBuilderCreate(char const* topFileArg)
{
    char* const real_top = tr_sys_path_resolve(topFileArg, NULL);

    if (real_top == NULL)
    {
        /* TODO: Better error reporting */
        return NULL;
    }

    struct FileList* files;
    tr_metainfo_builder* ret = tr_new0(tr_metainfo_builder, 1);

    ret->top = real_top;

    {
        tr_sys_path_info info;
        ret->isFolder = tr_sys_path_get_info(ret->top, 0, &info, NULL) && info.type == TR_SYS_PATH_IS_DIRECTORY;
    }

    /* build a list of files containing top file and,
       if it's a directory, all of its children */
    {
        char* dir = tr_sys_path_dirname(ret->top, NULL);
        char* base = tr_sys_path_basename(ret->top, NULL);
        files = getFiles(dir, base, NULL);
        tr_free(base);
        tr_free(dir);
    }

    for (struct FileList* walk = files; walk != NULL; walk = walk->next)
    {
        ++ret->fileCount;
    }

    ret->files = tr_new0(tr_metainfo_builder_file, ret->fileCount);

    for (int i = 0; files != NULL; ++i)
    {
        struct FileList* tmp = files;
        files = files->next;

        tr_metainfo_builder_file* file = &ret->files[i];
        file->filename = tmp->filename;
        file->size = tmp->size;

        ret->totalSize += tmp->size;

        tr_free(tmp);
    }

    qsort(ret->files, ret->fileCount, sizeof(tr_metainfo_builder_file), builderFileCompare);

    tr_metaInfoBuilderSetPieceSize(ret, bestPieceSize(ret->totalSize));

    return ret;
}

static bool isValidPieceSize(uint32_t n)
{
    bool const isPowerOfTwo = n != 0 && (n & (n - 1)) == 0;

    return isPowerOfTwo;
}

bool tr_metaInfoBuilderSetPieceSize(tr_metainfo_builder* b, uint32_t bytes)
{
    if (!isValidPieceSize(bytes))
    {
        char wanted[32];
        char gotten[32];
        tr_formatter_mem_B(wanted, bytes, sizeof(wanted));
        tr_formatter_mem_B(gotten, b->pieceSize, sizeof(gotten));
        tr_logAddError(_("Failed to set piece size to %s, leaving it at %s"), wanted, gotten);
        return false;
    }

    b->pieceSize = bytes;
    b->pieceCount = (int)(b->totalSize / b->pieceSize);

    if (b->totalSize % b->pieceSize != 0)
    {
        ++b->pieceCount;
    }

    return true;
}

void tr_metaInfoBuilderFree(tr_metainfo_builder* builder)
{
    if (builder != NULL)
    {
        for (uint32_t i = 0; i < builder->fileCount; ++i)
        {
            tr_free(builder->files[i].filename);
        }

        tr_free(builder->files);
        tr_free(builder->top);
        tr_free(builder->comment);

        for (int i = 0; i < builder->trackerCount; ++i)
        {
            tr_free(builder->trackers[i].announce);
        }

        tr_free(builder->trackers);
        tr_free(builder->outputFile);
        tr_free(builder);
    }
}

/****
*****
****/

static uint8_t* getHashInfo(tr_metainfo_builder* b)
{
    uint32_t fileIndex = 0;
    uint8_t* ret = tr_new0(uint8_t, SHA_DIGEST_LENGTH * b->pieceCount);
    uint8_t* walk = ret;
    uint8_t* buf;
    uint64_t totalRemain;
    uint64_t off = 0;
    tr_sys_file_t fd;
    tr_error* error = NULL;

    if (b->totalSize == 0)
    {
        return ret;
    }

    buf = tr_valloc(b->pieceSize);
    b->pieceIndex = 0;
    totalRemain = b->totalSize;
    fd = tr_sys_file_open(b->files[fileIndex].filename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &error);

    if (fd == TR_BAD_SYS_FILE)
    {
        b->my_errno = error->code;
        tr_strlcpy(b->errfile, b->files[fileIndex].filename, sizeof(b->errfile));
        b->result = TR_MAKEMETA_IO_READ;
        tr_free(buf);
        tr_free(ret);
        tr_error_free(error);
        return NULL;
    }

    while (totalRemain != 0)
    {
        TR_ASSERT(b->pieceIndex < b->pieceCount);

        uint8_t* bufptr = buf;
        uint32_t const thisPieceSize = (uint32_t)MIN(b->pieceSize, totalRemain);
        uint64_t leftInPiece = thisPieceSize;

        while (leftInPiece != 0)
        {
            uint64_t const n_this_pass = MIN(b->files[fileIndex].size - off, leftInPiece);
            uint64_t n_read = 0;
            tr_sys_file_read(fd, bufptr, n_this_pass, &n_read, NULL);
            bufptr += n_read;
            off += n_read;
            leftInPiece -= n_read;

            if (off == b->files[fileIndex].size)
            {
                off = 0;
                tr_sys_file_close(fd, NULL);
                fd = TR_BAD_SYS_FILE;

                if (++fileIndex < b->fileCount)
                {
                    fd = tr_sys_file_open(b->files[fileIndex].filename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &error);

                    if (fd == TR_BAD_SYS_FILE)
                    {
                        b->my_errno = error->code;
                        tr_strlcpy(b->errfile, b->files[fileIndex].filename, sizeof(b->errfile));
                        b->result = TR_MAKEMETA_IO_READ;
                        tr_free(buf);
                        tr_free(ret);
                        tr_error_free(error);
                        return NULL;
                    }
                }
            }
        }

        TR_ASSERT(bufptr - buf == (int)thisPieceSize);
        TR_ASSERT(leftInPiece == 0);
        tr_sha1(walk, buf, (int)thisPieceSize, NULL);
        walk += SHA_DIGEST_LENGTH;

        if (b->abortFlag)
        {
            b->result = TR_MAKEMETA_CANCELLED;
            break;
        }

        totalRemain -= thisPieceSize;
        ++b->pieceIndex;
    }

    TR_ASSERT(b->abortFlag || walk - ret == (int)(SHA_DIGEST_LENGTH * b->pieceCount));
    TR_ASSERT(b->abortFlag || !totalRemain);

    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd, NULL);
    }

    tr_free(buf);
    return ret;
}

static void getFileInfo(char const* topFile, tr_metainfo_builder_file const* file, tr_variant* uninitialized_length,
    tr_variant* uninitialized_path)
{
    size_t offset;

    /* get the file size */
    tr_variantInitInt(uninitialized_length, file->size);

    /* how much of file->filename to walk past */
    offset = strlen(topFile);

    if (offset > 0 && topFile[offset - 1] != TR_PATH_DELIMITER)
    {
        ++offset; /* +1 for the path delimiter */
    }

    /* build the path list */
    tr_variantInitList(uninitialized_path, 0);

    if (strlen(file->filename) > offset)
    {
        char* filename = tr_strdup(file->filename + offset);
        char* walk = filename;
        char const* token;

        while ((token = tr_strsep(&walk, TR_PATH_DELIMITER_STR)) != NULL)
        {
            if (!tr_str_is_empty(token))
            {
                tr_variantListAddStr(uninitialized_path, token);
            }
        }

        tr_free(filename);
    }
}

static void makeInfoDict(tr_variant* dict, tr_metainfo_builder* builder)
{
    char* base;
    uint8_t* pch;

    tr_variantDictReserve(dict, 5);

    if (builder->isFolder) /* root node is a directory */
    {
        tr_variant* list = tr_variantDictAddList(dict, TR_KEY_files, builder->fileCount);

        for (uint32_t i = 0; i < builder->fileCount; ++i)
        {
            tr_variant* d = tr_variantListAddDict(list, 2);
            tr_variant* length = tr_variantDictAdd(d, TR_KEY_length);
            tr_variant* pathVal = tr_variantDictAdd(d, TR_KEY_path);
            getFileInfo(builder->top, &builder->files[i], length, pathVal);
        }
    }
    else
    {
        tr_variantDictAddInt(dict, TR_KEY_length, builder->files[0].size);
    }

    base = tr_sys_path_basename(builder->top, NULL);

    if (base != NULL)
    {
        tr_variantDictAddStr(dict, TR_KEY_name, base);
        tr_free(base);
    }

    tr_variantDictAddInt(dict, TR_KEY_piece_length, builder->pieceSize);

    if ((pch = getHashInfo(builder)) != NULL)
    {
        tr_variantDictAddRaw(dict, TR_KEY_pieces, pch, SHA_DIGEST_LENGTH * builder->pieceCount);
        tr_free(pch);
    }

    tr_variantDictAddInt(dict, TR_KEY_private, builder->isPrivate ? 1 : 0);
}

static void tr_realMakeMetaInfo(tr_metainfo_builder* builder)
{
    tr_variant top;

    /* allow an empty set, but if URLs *are* listed, verify them. #814, #971 */
    for (int i = 0; i < builder->trackerCount && builder->result == TR_MAKEMETA_OK; ++i)
    {
        if (!tr_urlIsValidTracker(builder->trackers[i].announce))
        {
            tr_strlcpy(builder->errfile, builder->trackers[i].announce, sizeof(builder->errfile));
            builder->result = TR_MAKEMETA_URL;
        }
    }

    tr_variantInitDict(&top, 6);

    if (builder->fileCount == 0 || builder->totalSize == 0 || builder->pieceSize == 0 || builder->pieceCount == 0)
    {
        builder->errfile[0] = '\0';
        builder->my_errno = ENOENT;
        builder->result = TR_MAKEMETA_IO_READ;
        builder->isDone = true;
    }

    if (builder->result == TR_MAKEMETA_OK && builder->trackerCount != 0)
    {
        int prevTier = -1;
        tr_variant* tier = NULL;

        if (builder->trackerCount > 1)
        {
            tr_variant* annList = tr_variantDictAddList(&top, TR_KEY_announce_list, 0);

            for (int i = 0; i < builder->trackerCount; ++i)
            {
                if (prevTier != builder->trackers[i].tier)
                {
                    prevTier = builder->trackers[i].tier;
                    tier = tr_variantListAddList(annList, 0);
                }

                tr_variantListAddStr(tier, builder->trackers[i].announce);
            }
        }

        tr_variantDictAddStr(&top, TR_KEY_announce, builder->trackers[0].announce);
    }

    if (builder->result == TR_MAKEMETA_OK && !builder->abortFlag)
    {
        if (!tr_str_is_empty(builder->comment))
        {
            tr_variantDictAddStr(&top, TR_KEY_comment, builder->comment);
        }

        tr_variantDictAddStr(&top, TR_KEY_created_by, TR_NAME "/" LONG_VERSION_STRING);
        tr_variantDictAddInt(&top, TR_KEY_creation_date, time(NULL));
        tr_variantDictAddStr(&top, TR_KEY_encoding, "UTF-8");
        makeInfoDict(tr_variantDictAddDict(&top, TR_KEY_info, 666), builder);
    }

    /* save the file */
    if (builder->result == TR_MAKEMETA_OK && !builder->abortFlag)
    {
        if (tr_variantToFile(&top, TR_VARIANT_FMT_BENC, builder->outputFile) != 0)
        {
            builder->my_errno = errno;
            tr_strlcpy(builder->errfile, builder->outputFile, sizeof(builder->errfile));
            builder->result = TR_MAKEMETA_IO_WRITE;
        }
    }

    /* cleanup */
    tr_variantFree(&top);

    if (builder->abortFlag)
    {
        builder->result = TR_MAKEMETA_CANCELLED;
    }

    builder->isDone = true;
}

/***
****
****  A threaded builder queue
****
***/

static tr_metainfo_builder* queue = NULL;

static tr_thread* workerThread = NULL;

static tr_lock* getQueueLock(void)
{
    static tr_lock* lock = NULL;

    if (lock == NULL)
    {
        lock = tr_lockNew();
    }

    return lock;
}

static void makeMetaWorkerFunc(void* unused UNUSED)
{
    for (;;)
    {
        tr_metainfo_builder* builder = NULL;

        /* find the next builder to process */
        tr_lock* lock = getQueueLock();
        tr_lockLock(lock);

        if (queue != NULL)
        {
            builder = queue;
            queue = queue->nextBuilder;
        }

        tr_lockUnlock(lock);

        /* if no builders, this worker thread is done */
        if (builder == NULL)
        {
            break;
        }

        tr_realMakeMetaInfo(builder);
    }

    workerThread = NULL;
}

void tr_makeMetaInfo(tr_metainfo_builder* builder, char const* outputFile, tr_tracker_info const* trackers, int trackerCount,
    char const* comment, bool isPrivate)
{
    tr_lock* lock;

    /* free any variables from a previous run */
    for (int i = 0; i < builder->trackerCount; ++i)
    {
        tr_free(builder->trackers[i].announce);
    }

    tr_free(builder->trackers);
    tr_free(builder->comment);
    tr_free(builder->outputFile);

    /* initialize the builder variables */
    builder->abortFlag = false;
    builder->result = 0;
    builder->isDone = false;
    builder->pieceIndex = 0;
    builder->trackerCount = trackerCount;
    builder->trackers = tr_new0(tr_tracker_info, builder->trackerCount);

    for (int i = 0; i < builder->trackerCount; ++i)
    {
        builder->trackers[i].tier = trackers[i].tier;
        builder->trackers[i].announce = tr_strdup(trackers[i].announce);
    }

    builder->comment = tr_strdup(comment);
    builder->isPrivate = isPrivate;

    if (!tr_str_is_empty(outputFile))
    {
        builder->outputFile = tr_strdup(outputFile);
    }
    else
    {
        builder->outputFile = tr_strdup_printf("%s.torrent", builder->top);
    }

    /* enqueue the builder */
    lock = getQueueLock();
    tr_lockLock(lock);
    builder->nextBuilder = queue;
    queue = builder;

    if (workerThread == NULL)
    {
        workerThread = tr_threadNew(makeMetaWorkerFunc, NULL);
    }

    tr_lockUnlock(lock);
}
