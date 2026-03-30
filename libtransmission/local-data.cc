// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "libtransmission/local-data.h"

#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/inout.h"
#include "libtransmission/open-files.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrents.h"
#include "libtransmission/transmission.h"

namespace tr
{
namespace
{
[[nodiscard]] tr_error make_error(int err)
{
    auto error = tr_error{};
    if (err != 0)
    {
        error.set_from_errno(err);
    }

    return error;
}

[[nodiscard]] std::optional<tr_sha1_digest_t> recalculate_hash(
    LocalData::Backend& backend,
    tr_torrent_id_t const id,
    tr_block_info const block_info,
    tr_piece_index_t const piece)
{
    TR_ASSERT(piece < block_info.piece_count());

    auto sha = tr_sha1{};
    auto buffer = LocalData::BlockData{};

    auto const [begin_byte, end_byte] = block_info.byte_span_for_piece(piece);
    auto const [begin_block, end_block] = block_info.block_span_for_piece(piece);
    [[maybe_unused]] auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block)
    {
        auto const byte_span = block_info.byte_span_for_block(block);

        buffer.clear();
        if (auto const err = backend.read(id, byte_span, buffer); err != 0)
        {
            return {};
        }

        auto* begin = std::data(buffer);
        auto* end = begin + byte_span.size();

        if (block == begin_block)
        {
            begin += (begin_byte - byte_span.begin);
        }
        if (block + 1U == end_block)
        {
            end -= (byte_span.end - end_byte);
        }

        sha.add(begin, end - begin);
        n_bytes_checked += (end - begin);
    }

    TR_ASSERT(block_info.piece_size(piece) == n_bytes_checked);
    return sha.finish();
}

class DefaultBackend final : public LocalData::Backend
{
public:
    DefaultBackend(tr_torrents const& torrents, tr_open_files& open_files)
        : open_files_{ open_files }
        , torrents_{ torrents }
    {
    }

    [[nodiscard]] int read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, LocalData::BlockData& setme) override
    {
        if (!byte_span.is_valid())
        {
            return EINVAL;
        }

        auto const len = byte_span.size();
        if (len > tr_block_info::BlockSize)
        {
            return EINVAL;
        }
        auto const span_size = static_cast<size_t>(len);

        auto const* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        auto const loc = tor->block_info().byte_loc(byte_span.begin);
        setme.resize(span_size);
        return tr_ioRead(*tor, open_files_, loc, std::span{ std::data(setme), span_size });
    }

    [[nodiscard]] int test_piece(tr_torrent_id_t const id, tr_piece_index_t const piece, tr_sha1_digest_t& setme_hash) override
    {
        auto const* const tor = torrents_.get(id);
        if (tor == nullptr || piece >= tor->piece_count())
        {
            return EINVAL;
        }

        auto const hash = recalculate_hash(*this, id, tor->block_info(), piece);
        if (!hash)
        {
            return EIO;
        }

        setme_hash = *hash;
        return 0;
    }

    [[nodiscard]] int write(tr_torrent_id_t const id, tr_byte_span_t const byte_span, LocalData::BlockData const& data) override
    {
        if (!byte_span.is_valid())
        {
            return EINVAL;
        }

        auto const len = byte_span.size();
        if (len > std::size(data))
        {
            return EINVAL;
        }
        auto const span_size = static_cast<size_t>(len);

        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        auto const loc = tor->block_info().byte_loc(byte_span.begin);
        return tr_ioWrite(*tor, open_files_, loc, std::span{ std::data(data), span_size });
    }

    [[nodiscard]] int move(
        tr_torrent_id_t const id,
        std::string_view const old_parent,
        std::string_view const parent,
        std::string_view const parent_name) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        auto error = tr_error{};
        if (tor->files().move(old_parent, parent, parent_name, &error))
        {
            return 0;
        }

        return error ? error.code() : EIO;
    }

    [[nodiscard]] int remove(tr_torrent_id_t const id, tr_torrent_remove_func remove_func) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        if (!remove_func)
        {
            remove_func = tr_sys_path_remove;
        }

        auto error = tr_error{};
        tor->files().remove(tor->current_dir(), tor->name(), remove_func, &error);
        return error ? error.code() : 0;
    }

    void rename(
        tr_torrent_id_t const id,
        std::string_view const oldpath,
        std::string_view const newname,
        tr_torrent_rename_done_func callback) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            if (callback != nullptr)
            {
                callback(id, oldpath, newname, make_error(EINVAL));
            }
            return;
        }

        tr_torrentRenamePath(tor, oldpath, newname, std::move(callback));
    }

    void close_all() override
    {
        open_files_.close_all();
    }

    void close_torrent(tr_torrent_id_t const tor_id) override
    {
        open_files_.close_torrent(tor_id);
    }

    void close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num) override
    {
        open_files_.close_file(tor_id, file_num);
    }

private:
    tr_open_files& open_files_;
    tr_torrents const& torrents_;
};

} // namespace

LocalData::LocalData(tr_torrents const& torrents, tr_open_files& open_files, [[maybe_unused]] size_t worker_count)
    : backend_{ std::make_unique<DefaultBackend>(torrents, open_files) }
{
}

LocalData::LocalData(std::unique_ptr<Backend> backend, [[maybe_unused]] size_t worker_count)
    : backend_{ std::move(backend) }
{
}

LocalData::~LocalData() = default;

void LocalData::read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, OnRead on_read)
{
    auto data = std::make_unique<BlockData>();
    auto const err = backend_->read(id, byte_span, *data);
    if (err != 0)
    {
        data.reset();
    }

    if (on_read)
    {
        on_read(id, byte_span, make_error(err), std::move(data));
    }
}

void LocalData::test_piece(tr_torrent_id_t const id, tr_piece_index_t const piece, OnTest on_test)
{
    auto hash = tr_sha1_digest_t{};
    auto const err = backend_->test_piece(id, piece, hash);

    if (on_test)
    {
        on_test(id, piece, make_error(err), err == 0 ? std::optional<tr_sha1_digest_t>{ hash } : std::nullopt);
    }
}

void LocalData::write(
    tr_torrent_id_t const id,
    tr_byte_span_t const byte_span,
    std::unique_ptr<BlockData> data,
    OnWrite on_write)
{
    auto err = int{ EINVAL };
    if (data != nullptr)
    {
        err = backend_->write(id, byte_span, *data);
    }

    if (on_write)
    {
        on_write(id, byte_span, make_error(err));
    }
}

void LocalData::close_torrent(tr_torrent_id_t const tor_id)
{
    backend_->close_torrent(tor_id);
}

void LocalData::close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num)
{
    backend_->close_file(tor_id, file_num);
}

void LocalData::close_all()
{
    backend_->close_all();
}

void LocalData::move(
    tr_torrent_id_t const id,
    std::string_view const old_parent,
    std::string_view const parent,
    std::string_view const parent_name,
    OnMove on_move)
{
    auto const err = backend_->move(id, old_parent, parent, parent_name);

    if (on_move)
    {
        on_move(id, make_error(err));
    }
}

void LocalData::remove(tr_torrent_id_t const id, tr_torrent_remove_func remove_func)
{
    static_cast<void>(backend_->remove(id, std::move(remove_func)));
}

void LocalData::rename(
    tr_torrent_id_t const id,
    std::string_view const oldpath,
    std::string_view const newname,
    tr_torrent_rename_done_func callback)
{
    backend_->rename(id, oldpath, newname, std::move(callback));
}

void LocalData::shutdown()
{
}

uint64_t LocalData::enqueued_write_bytes() const
{
    return 0U;
}

} // namespace tr