// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring> /* strcmp, strlen */
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include <event2/util.h> /* evutil_ascii_strcasecmp() */

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "makemeta.h"
#include "session.h"
#include "tr-assert.h"
#include "utils.h" /* buildpath */
#include "variant.h"
#include "version.h"
#include "web-utils.h"

using namespace std::literals;

/****
*****
****/

struct FileList
{
    uint64_t size;
    char* filename;
    struct FileList* next;
};

static struct FileList* getFiles(std::string_view dir, std::string_view base, struct FileList* list)
{
    if (std::empty(dir) || std::empty(base))
    {
        return nullptr;
    }

    auto buf = tr_pathbuf{ dir, '/', base };
    tr_sys_path_native_separators(std::data(buf));

    tr_sys_path_info info;
    if (tr_error* error = nullptr; !tr_sys_path_get_info(buf, 0, &info, &error))
    {
        tr_logAddWarn(fmt::format(
            _("Skipping '{path}': {error} ({error_code})"),
            fmt::arg("path", buf),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
        return list;
    }

    if (tr_sys_dir_t odir = info.type == TR_SYS_PATH_IS_DIRECTORY ? tr_sys_dir_open(buf.c_str()) : TR_BAD_SYS_DIR;
        odir != TR_BAD_SYS_DIR)
    {
        char const* name = nullptr;
        while ((name = tr_sys_dir_read_name(odir)) != nullptr)
        {
            if (name[0] != '.') /* skip dotfiles */
            {
                list = getFiles(buf.c_str(), name, list);
            }
        }

        tr_sys_dir_close(odir);
    }
    else if (info.type == TR_SYS_PATH_IS_FILE)
    {
        auto* const node = tr_new0(FileList, 1);
        node->size = info.size;
        node->filename = tr_strvDup(buf);
        node->next = list;
        list = node;
    }

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

tr_metainfo_builder* tr_metaInfoBuilderCreate(char const* topFileArg)
{
    char* const real_top = tr_sys_path_resolve(topFileArg);

    if (real_top == nullptr)
    {
        /* TODO: Better error reporting */
        return nullptr;
    }

    auto* const ret = tr_new0(tr_metainfo_builder, 1);

    ret->top = real_top;

    {
        tr_sys_path_info info;
        ret->isFolder = tr_sys_path_get_info(ret->top, 0, &info) && info.type == TR_SYS_PATH_IS_DIRECTORY;
    }

    /* build a list of files containing top file and,
       if it's a directory, all of its children */
    auto* files = getFiles(tr_sys_path_dirname(ret->top), tr_sys_path_basename(ret->top), nullptr);

    for (auto* walk = files; walk != nullptr; walk = walk->next)
    {
        ++ret->fileCount;
    }

    ret->files = tr_new0(tr_metainfo_builder_file, ret->fileCount);

    int i = 0;
    auto const offset = strlen(ret->top);
    while (files != nullptr)
    {
        auto* const tmp = files;
        files = files->next;

        auto* const file = &ret->files[i++];
        file->filename = tmp->filename;
        file->size = tmp->size;
        file->is_portable = tr_torrent_files::isSubpathPortable(file->filename + offset);

        ret->totalSize += tmp->size;

        tr_free(tmp);
    }

    std::sort(
        ret->files,
        ret->files + ret->fileCount,
        [](auto const& a, auto const& b) { return evutil_ascii_strcasecmp(a.filename, b.filename) < 0; });

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
        tr_logAddWarn(fmt::format(
            _("Couldn't use invalid piece size {expected_size}; using {actual_size} instead"),
            fmt::arg("expected_size", tr_formatter_mem_B(bytes)),
            fmt::arg("actual_size", tr_formatter_mem_B(b->pieceSize))));
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
    if (builder != nullptr)
    {
        for (uint32_t i = 0; i < builder->fileCount; ++i)
        {
            tr_free(builder->files[i].filename);
        }

        tr_free(builder->files);
        tr_free(builder->top);
        tr_free(builder->comment);
        tr_free(builder->source);

        for (int i = 0; i < builder->trackerCount; ++i)
        {
            tr_free(builder->trackers[i].announce);
        }

        for (int i = 0; i < builder->webseedCount; ++i)
        {
            tr_free(builder->webseeds[i]);
        }

        tr_free(builder->trackers);
        tr_free(builder->webseeds);
        tr_free(builder->outputFile);
        tr_free(builder);
    }
}

/****
*****
****/

static std::vector<std::byte> getHashInfo(tr_metainfo_builder* b)
{
    auto ret = std::vector<std::byte>(std::size(tr_sha1_digest_t{}) * b->pieceCount);

    if (b->totalSize == 0)
    {
        return {};
    }

    b->pieceIndex = 0;
    uint64_t totalRemain = b->totalSize;
    uint32_t fileIndex = 0;
    auto* walk = std::data(ret);
    auto buf = std::vector<char>(b->pieceSize);
    uint64_t off = 0;
    tr_error* error = nullptr;

    tr_sys_file_t fd = tr_sys_file_open(b->files[fileIndex].filename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &error);
    if (fd == TR_BAD_SYS_FILE)
    {
        b->my_errno = error->code;
        tr_strlcpy(b->errfile, b->files[fileIndex].filename, sizeof(b->errfile));
        b->result = TrMakemetaResult::ERR_IO_READ;
        tr_error_free(error);
        return {};
    }

    while (totalRemain != 0)
    {
        TR_ASSERT(b->pieceIndex < b->pieceCount);

        uint32_t const this_piece_size = std::min(uint64_t{ b->pieceSize }, totalRemain);
        buf.resize(this_piece_size);
        auto* bufptr = std::data(buf);
        uint64_t leftInPiece = this_piece_size;

        while (leftInPiece != 0)
        {
            uint64_t const n_this_pass = std::min(b->files[fileIndex].size - off, leftInPiece);
            uint64_t n_read = 0;
            (void)tr_sys_file_read(fd, bufptr, n_this_pass, &n_read);
            bufptr += n_read;
            off += n_read;
            leftInPiece -= n_read;

            if (off == b->files[fileIndex].size)
            {
                off = 0;
                tr_sys_file_close(fd);
                fd = TR_BAD_SYS_FILE;

                if (++fileIndex < b->fileCount)
                {
                    fd = tr_sys_file_open(b->files[fileIndex].filename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &error);

                    if (fd == TR_BAD_SYS_FILE)
                    {
                        b->my_errno = error->code;
                        tr_strlcpy(b->errfile, b->files[fileIndex].filename, sizeof(b->errfile));
                        b->result = TrMakemetaResult::ERR_IO_READ;
                        tr_error_free(error);
                        return {};
                    }
                }
            }
        }

        TR_ASSERT(bufptr - std::data(buf) == (int)this_piece_size);
        TR_ASSERT(leftInPiece == 0);
        auto const digest = tr_sha1::digest(buf);
        walk = std::copy(std::begin(digest), std::end(digest), walk);

        if (b->abortFlag)
        {
            b->result = TrMakemetaResult::CANCELLED;
            break;
        }

        totalRemain -= this_piece_size;
        ++b->pieceIndex;
    }

    TR_ASSERT(b->abortFlag || size_t(walk - std::data(ret)) == std::size(ret));
    TR_ASSERT(b->abortFlag || !totalRemain);

    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd);
    }

    return ret;
}

static void getFileInfo(
    char const* topFile,
    tr_metainfo_builder_file const* file,
    tr_variant* uninitialized_length,
    tr_variant* uninitialized_path)
{
    /* get the file size */
    tr_variantInitInt(uninitialized_length, file->size);

    /* how much of file->filename to walk past */
    size_t offset = strlen(topFile);
    if (offset > 0 && topFile[offset - 1] != TR_PATH_DELIMITER)
    {
        ++offset; /* +1 for the path delimiter */
    }

    /* build the path list */
    tr_variantInitList(uninitialized_path, 0);

    auto filename = std::string_view{ file->filename };
    if (std::size(filename) > offset)
    {
        filename.remove_prefix(offset);
        auto token = std::string_view{};
        while (tr_strvSep(&filename, &token, TR_PATH_DELIMITER))
        {
            tr_variantListAddStr(uninitialized_path, token);
        }
    }
}

static void makeInfoDict(tr_variant* dict, tr_metainfo_builder* builder)
{
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

    if (auto const base = tr_sys_path_basename(builder->top); !std::empty(base))
    {
        tr_variantDictAddStr(dict, TR_KEY_name, base);
    }

    tr_variantDictAddInt(dict, TR_KEY_piece_length, builder->pieceSize);

    if (auto const piece_hashes = getHashInfo(builder); !std::empty(piece_hashes))
    {
        tr_variantDictAddRaw(dict, TR_KEY_pieces, std::data(piece_hashes), std::size(piece_hashes));
    }

    if (builder->isPrivate)
    {
        tr_variantDictAddInt(dict, TR_KEY_private, 1);
    }

    if (builder->source != nullptr)
    {
        tr_variantDictAddStr(dict, TR_KEY_source, builder->source);
    }
}

static void tr_realMakeMetaInfo(tr_metainfo_builder* builder)
{
    tr_variant top;

    for (int i = 0; i < builder->trackerCount && builder->result == TrMakemetaResult::OK; ++i)
    {
        if (!tr_urlIsValidTracker(builder->trackers[i].announce))
        {
            tr_strlcpy(builder->errfile, builder->trackers[i].announce, sizeof(builder->errfile));
            builder->result = TrMakemetaResult::ERR_URL;
        }
    }

    for (int i = 0; i < builder->webseedCount && builder->result == TrMakemetaResult::OK; ++i)
    {
        if (!tr_urlIsValid(builder->webseeds[i]))
        {
            tr_strlcpy(builder->errfile, builder->webseeds[i], sizeof(builder->errfile));
            builder->result = TrMakemetaResult::ERR_URL;
        }
    }

    tr_variantInitDict(&top, 6);

    if (builder->fileCount == 0 || builder->totalSize == 0 || builder->pieceSize == 0 || builder->pieceCount == 0)
    {
        builder->errfile[0] = '\0';
        builder->my_errno = ENOENT;
        builder->result = TrMakemetaResult::ERR_IO_READ;
        builder->isDone = true;
    }

    if (builder->result == TrMakemetaResult::OK && builder->trackerCount != 0)
    {
        int prevTier = -1;
        tr_variant* tier = nullptr;

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

    if (builder->result == TrMakemetaResult::OK && builder->webseedCount > 0)
    {
        tr_variant* url_list = tr_variantDictAddList(&top, TR_KEY_url_list, builder->webseedCount);

        for (int i = 0; i < builder->webseedCount; ++i)
        {
            tr_variantListAddStr(url_list, builder->webseeds[i]);
        }
    }

    if (builder->result == TrMakemetaResult::OK && !builder->abortFlag)
    {
        if (!tr_str_is_empty(builder->comment))
        {
            tr_variantDictAddStr(&top, TR_KEY_comment, builder->comment);
        }

        if (!builder->anonymize)
        {
            tr_variantDictAddStrView(&top, TR_KEY_created_by, TR_NAME "/" LONG_VERSION_STRING);
            tr_variantDictAddInt(&top, TR_KEY_creation_date, time(nullptr));
        }
        tr_variantDictAddStrView(&top, TR_KEY_encoding, "UTF-8");
        makeInfoDict(tr_variantDictAddDict(&top, TR_KEY_info, 666), builder);
    }

    /* save the file */
    if ((builder->result == TrMakemetaResult::OK) && (!builder->abortFlag) &&
        (tr_variantToFile(&top, TR_VARIANT_FMT_BENC, builder->outputFile) != 0))
    {
        builder->my_errno = errno;
        tr_strlcpy(builder->errfile, builder->outputFile, sizeof(builder->errfile));
        builder->result = TrMakemetaResult::ERR_IO_WRITE;
    }

    /* cleanup */
    tr_variantFree(&top);

    if (builder->abortFlag)
    {
        builder->result = TrMakemetaResult::CANCELLED;
    }

    builder->isDone = true;
}

/***
****
****  A threaded builder queue
****
***/

static tr_metainfo_builder* queue = nullptr;

static std::optional<std::thread::id> worker_thread_id;

static std::recursive_mutex queue_mutex_;

static void makeMetaWorkerFunc()
{
    for (;;)
    {
        tr_metainfo_builder* builder = nullptr;

        /* find the next builder to process */
        queue_mutex_.lock();

        if (queue != nullptr)
        {
            builder = queue;
            queue = queue->nextBuilder;
        }

        queue_mutex_.unlock();

        /* if no builders, this worker thread is done */
        if (builder == nullptr)
        {
            break;
        }

        tr_realMakeMetaInfo(builder);
    }

    worker_thread_id.reset();
}

void tr_makeMetaInfo(
    tr_metainfo_builder* builder,
    char const* outputFile,
    tr_tracker_info const* trackers,
    int trackerCount,
    char const** webseeds,
    int webseedCount,
    char const* comment,
    bool isPrivate,
    bool anonymize,
    char const* source)
{
    /* free any variables from a previous run */
    for (int i = 0; i < builder->trackerCount; ++i)
    {
        tr_free(builder->trackers[i].announce);
    }

    tr_free(builder->trackers);
    tr_free(builder->comment);
    tr_free(builder->source);
    tr_free(builder->outputFile);

    /* initialize the builder variables */
    builder->abortFlag = false;
    builder->result = TrMakemetaResult::OK;
    builder->isDone = false;
    builder->pieceIndex = 0;
    builder->trackerCount = trackerCount;
    builder->trackers = tr_new0(tr_tracker_info, builder->trackerCount);

    for (int i = 0; i < builder->trackerCount; ++i)
    {
        builder->trackers[i].tier = trackers[i].tier;
        builder->trackers[i].announce = tr_strdup(trackers[i].announce);
    }

    builder->webseedCount = webseedCount;
    builder->webseeds = tr_new0(char*, webseedCount);
    for (int i = 0; i < webseedCount; ++i)
    {
        builder->webseeds[i] = tr_strdup(webseeds[i]);
    }

    builder->comment = tr_strdup(comment);
    builder->isPrivate = isPrivate;
    builder->anonymize = anonymize;
    builder->source = tr_strdup(source);

    builder->outputFile = !tr_str_is_empty(outputFile) ? tr_strdup(outputFile) :
                                                         tr_strvDup(fmt::format(FMT_STRING("{:s}.torrent"), builder->top));

    /* enqueue the builder */
    auto const lock = std::lock_guard(queue_mutex_);

    builder->nextBuilder = queue;
    queue = builder;

    if (!worker_thread_id)
    {
        auto thread = std::thread(makeMetaWorkerFunc);
        worker_thread_id = thread.get_id();
        thread.detach();
    }
}

///

namespace tr_torrent_maker
{

static void walkTree(std::string_view top, std::string_view subpath, tr_torrent_files& files)
{
    if (std::empty(top) && std::empty(subpath))
    {
        return;
    }

    auto path = tr_pathbuf{ top };
    if (!std::empty(subpath))
    {
        path.append('/', subpath);
    }
    tr_sys_path_native_separators(std::data(path));

    auto info = tr_sys_path_info{};
    if (tr_error* error = nullptr; !tr_sys_path_get_info(path, 0, &info, &error))
    {
        tr_logAddWarn(fmt::format(
            _("Skipping '{path}': {error} ({error_code})"),
            fmt::arg("path", path),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
    }

    switch (info.type)
    {
    case TR_SYS_PATH_IS_DIRECTORY:
        if (tr_sys_dir_t odir = tr_sys_dir_open(path.c_str()); odir != TR_BAD_SYS_DIR)
        {
            char const* name = nullptr;
            while ((name = tr_sys_dir_read_name(odir)) != nullptr)
            {
                if (name[0] == '.') // skip dotfiles
                {
                    continue;
                }

                if (!std::empty(subpath))
                {
                    walkTree(top, tr_pathbuf{ subpath, '/', name }, files);
                }
                else
                {
                    walkTree(top, name, files);
                }
            }

            tr_sys_dir_close(odir);
        }
        break;

    case TR_SYS_PATH_IS_FILE:
        files.add(subpath, info.size);
        break;

    default:
        break;
    }
}

Builder::Builder(std::string_view top)
    : top_{ top }
{
    walkTree(top, {}, files_);
    block_info_ = tr_block_info{ files().totalSize(), defaultPieceSize(files_.totalSize()) };
}

uint32_t Builder::defaultPieceSize(uint64_t totalSize)
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

bool Builder::isLegalPieceSize(uint32_t x)
{
    // It must be a power of two and at least 16KiB
    static auto constexpr MinSize = uint32_t{ 1024U * 16U };
    auto const is_power_of_two = (x & (x - 1)) == 0;
    return x >= MinSize && is_power_of_two;
}

bool Builder::setPieceSize(uint32_t piece_size)
{
    if (!isLegalPieceSize(piece_size))
    {
        return false;
    }

    block_info_ = tr_block_info{ files().totalSize(), piece_size };
    return true;
}

bool Builder::makeChecksums(tr_error** error)
{
    checksum_percent_done_ = 0;
    cancel_ = false;

    auto const& files = this->files();
    auto const& block_info = this->blockInfo();

    if (files.totalSize() == 0U)
    {
        tr_error_set(error, ENOENT, tr_strerror(ENOENT));
        return false;
    }

    auto hashes = std::vector<std::byte>(std::size(tr_sha1_digest_t{}) * block_info.pieceCount());
    auto* walk = std::data(hashes);

    auto file_index = tr_file_index_t{ 0U };
    auto piece_index = tr_piece_index_t{ 0U };
    auto total_remain = files.totalSize();
    auto off = uint64_t{ 0U };

    auto buf = std::vector<char>(block_info.pieceSize());

    auto fd = tr_sys_file_open(
        tr_pathbuf{ top_, '/', files.path(file_index) },
        TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL,
        0,
        error);
    if (fd == TR_BAD_SYS_FILE)
    {
        return false;
    }

    while (!cancel_ && (total_remain > 0U))
    {
        checksum_percent_done_ = static_cast<double>(piece_index) / block_info.pieceCount();

        TR_ASSERT(piece_index < block_info.pieceCount());

        uint32_t const piece_size = block_info.pieceSize(piece_index);
        buf.resize(piece_size);
        auto* bufptr = std::data(buf);

        auto left_in_piece = piece_size;
        while (left_in_piece > 0U)
        {
            auto const n_this_pass = std::min(files.fileSize(file_index) - off, uint64_t{ left_in_piece });
            auto n_read = uint64_t{};

            (void)tr_sys_file_read(fd, bufptr, n_this_pass, &n_read, error);
            bufptr += n_read;
            off += n_read;
            left_in_piece -= n_read;

            if (off == files.fileSize(file_index))
            {
                off = 0;
                tr_sys_file_close(fd);
                fd = TR_BAD_SYS_FILE;

                if (++file_index < files.fileCount())
                {
                    fd = tr_sys_file_open(
                        tr_pathbuf{ top_, '/', files.path(file_index) },
                        TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL,
                        0,
                        error);
                    if (fd == TR_BAD_SYS_FILE)
                    {
                        return false;
                    }
                }
            }
        }

        TR_ASSERT(bufptr - std::data(buf) == (int)piece_size);
        TR_ASSERT(left_in_piece == 0);
        auto const digest = *tr_sha1(buf);
        walk = std::copy(std::begin(digest), std::end(digest), walk);

        total_remain -= piece_size;
        ++piece_index;
    }

    TR_ASSERT(cancel_ || size_t(walk - std::data(hashes)) == std::size(hashes));
    TR_ASSERT(cancel_ || total_remain == 0U);

    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd);
    }

    if (cancel_)
    {
        tr_error_set(error, ECANCELED, tr_strerror(ECANCELED));
        return false;
    }

    piece_hashes_ = std::move(hashes);
    return true;
}

std::future<tr_error*> Builder::makeChecksumsAsync()
{
    auto promise = std::promise<tr_error*>{};
    auto future = promise.get_future();
    std::thread work_thread(
        [this, promise = std::move(promise)]() mutable
        {
            tr_error* error = nullptr;
            makeChecksums(&error);
            promise.set_value(error);
        });
    return future;
}

std::string Builder::benc(tr_error** error) const
{
    auto const anonymize = this->anonymize();
    auto const& block_info = this->blockInfo();
    auto const& comment = this->comment();
    auto const& files = this->files();
    auto const& source = this->source();
    auto const& webseeds = this->webseeds();

    if (block_info.totalSize() == 0)
    {
        tr_error_set(error, ENOENT, tr_strerror(ENOENT));
        return {};
    }

    auto top = tr_variant{};
    tr_variantInitDict(&top, 8);

    // add the announce-list trackers
    if (!std::empty(announceList()))
    {
        auto* const announce_list = tr_variantDictAddList(&top, TR_KEY_announce_list, 0);
        tr_variant* tier_list = nullptr;
        auto prev_tier = std::optional<tr_tracker_tier_t>{};
        for (auto const& tracker : announceList())
        {
            if (!prev_tier || *prev_tier != tracker.tier)
            {
                tier_list = nullptr;
            }

            if (tier_list == nullptr)
            {
                prev_tier = tracker.tier;
                tr_variantListAddList(announce_list, 0);
            }

            tr_variantListAddStr(tier_list, tracker.announce);
        }
    }

    // add the webseeds
    if (!std::empty(webseeds))
    {
        auto* const url_list = tr_variantDictAddList(&top, TR_KEY_url_list, std::size(webseeds));

        for (auto const& webseed : webseeds)
        {
            tr_variantListAddStr(url_list, webseed);
        }
    }

    // add the comment
    if (!std::empty(comment))
    {
        tr_variantDictAddStr(&top, TR_KEY_comment, comment);
    }

    // maybe add some optional metainfo
    if (!anonymize)
    {
        tr_variantDictAddStrView(&top, TR_KEY_created_by, TR_NAME "/" LONG_VERSION_STRING);
        tr_variantDictAddInt(&top, TR_KEY_creation_date, time(nullptr));
    }

    tr_variantDictAddStrView(&top, TR_KEY_encoding, "UTF-8");

    if (is_private_)
    {
        tr_variantDictAddInt(&top, TR_KEY_private, 1);
    }

    if (!std::empty(source))
    {
        tr_variantDictAddStr(&top, TR_KEY_source, source_);
    }

    auto* const info_dict = tr_variantDictAddDict(&top, TR_KEY_info, 5);

    // "There is also a key `length` or a key `files`, but not both or neither.
    // If length is present then the download represents a single file,
    // otherwise it represents a set of files which go in a directory structure."
    if (files.fileCount() == 1U)
    {
        tr_variantDictAddInt(info_dict, TR_KEY_length, files.fileSize(0));
    }
    else
    {
        auto const n_files = files.fileCount();
        auto* const file_list = tr_variantDictAddList(info_dict, TR_KEY_files, n_files);

        for (tr_file_index_t i = 0; i < n_files; ++i)
        {
            auto* file_dict = tr_variantListAddDict(file_list, 2);
            tr_variantDictAddInt(file_dict, TR_KEY_length, files.fileSize(i));
            auto subpath = std::string_view{ files.path(i) };
            auto* path_list = tr_variantDictAddList(file_dict, TR_KEY_path, 0);
            auto token = std::string_view{};
            while (tr_strvSep(&subpath, &token, '/'))
            {
                tr_variantListAddStr(path_list, token);
            }
        }
    }

    if (auto const base = tr_sys_path_basename(top_); !std::empty(base))
    {
        tr_variantDictAddStr(info_dict, TR_KEY_name, base);
    }

    tr_variantDictAddInt(info_dict, TR_KEY_piece_length, block_info.pieceSize());
    tr_variantDictAddRaw(info_dict, TR_KEY_pieces, std::data(piece_hashes_), std::size(piece_hashes_));
    auto ret = tr_variantToStr(&top, TR_VARIANT_FMT_BENC);
    tr_variantFree(&top);
    return ret;
}

bool Builder::save(std::string_view filename, tr_error** error) const
{
    return tr_saveFile(filename, benc(), error);
}

} // namespace tr_torrent_maker
