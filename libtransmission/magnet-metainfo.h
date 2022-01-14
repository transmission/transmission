/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "announce-list.h"

struct tr_error;
struct tr_variant;

class tr_magnet_metainfo
{
public:
    bool parseMagnet(std::string_view magnet_link, tr_error** error = nullptr);
    std::string magnet() const;
    virtual ~tr_magnet_metainfo() = default;

    auto const& infoHash() const
    {
        return info_hash_;
    }
    auto const& name() const
    {
        return name_;
    }

    auto webseedCount() const
    {
        return std::size(webseed_urls_);
    }

    auto const& webseed(size_t i) const
    {
        return webseed_urls_[i];
    }

    auto& announceList()
    {
        return announce_list_;
    }

    auto const& announceList() const
    {
        return announce_list_;
    }

    std::string const& infoHashString() const
    {
        return info_hash_str_;
    }

    virtual void clear();

    void setName(std::string_view name)
    {
        name_ = name;
    }

    std::string makeTorrentFilename(std::string_view torrent_dir) const
    {
        return makeFilename(torrent_dir, name(), infoHashString(), BasenameFormat::Hash, ".torrent");
    }

    std::string makeResumeFilename(std::string_view resume_dir) const
    {
        return makeFilename(resume_dir, name(), infoHashString(), BasenameFormat::Hash, ".resume");
    }

    bool migrateFile(
        std::string_view dirname,
        std::string_view name,
        std::string_view info_hash_string,
        std::string_view suffix);

protected:
    enum class BasenameFormat
    {
        Hash,
        NameAndPartialHash
    };

    static std::string makeFilename(
        std::string_view dirname,
        std::string_view name,
        std::string_view info_hash_string,
        BasenameFormat format,
        std::string_view suffix);

    std::string makeFilename(std::string_view dirname, BasenameFormat format, std::string_view suffix) const
    {
        return makeFilename(dirname, name(), infoHashString(), format, suffix);
    }

    tr_announce_list announce_list_;
    std::vector<std::string> webseed_urls_;
    tr_sha1_digest_t info_hash_;
    std::string info_hash_str_;
    std::string name_;
};
