// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "file.h"
#include "stats.h"
#include "tr-strbuf.h"
#include "utils.h" // for tr_getRatio(), tr_time()
#include "variant.h"

using namespace std::literals;

tr_session_stats tr_stats::loadOldStats(std::string_view config_dir)
{
    auto ret = tr_session_stats{};

    auto top = tr_variant{};
    auto filename = tr_pathbuf{ config_dir, "/stats.json"sv };
    bool loaded = tr_sys_path_exists(filename) && tr_variantFromFile(&top, TR_VARIANT_PARSE_JSON, filename.sv(), nullptr);

    if (!loaded)
    {
        // maybe the user just upgraded from an old version of Transmission
        // that was still using stats.benc
        filename.assign(config_dir, "/stats.benc");
        loaded = tr_sys_path_exists(filename) && tr_variantFromFile(&top, TR_VARIANT_PARSE_BENC, filename.sv(), nullptr);
    }

    if (loaded)
    {
        auto i = int64_t{};

        if (tr_variantDictFindInt(&top, TR_KEY_downloaded_bytes, &i))
        {
            ret.downloadedBytes = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_files_added, &i))
        {
            ret.filesAdded = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_seconds_active, &i))
        {
            ret.secondsActive = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_session_count, &i))
        {
            ret.sessionCount = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_uploaded_bytes, &i))
        {
            ret.uploadedBytes = (uint64_t)i;
        }

        tr_variantClear(&top);
    }

    return ret;
}

void tr_stats::save() const
{
    auto const saveme = cumulative();
    auto const filename = tr_pathbuf{ config_dir_, "/stats.json"sv };
    auto top = tr_variant{};
    tr_variantInitDict(&top, 5);
    tr_variantDictAddInt(&top, TR_KEY_downloaded_bytes, saveme.downloadedBytes);
    tr_variantDictAddInt(&top, TR_KEY_files_added, saveme.filesAdded);
    tr_variantDictAddInt(&top, TR_KEY_seconds_active, saveme.secondsActive);
    tr_variantDictAddInt(&top, TR_KEY_session_count, saveme.sessionCount);
    tr_variantDictAddInt(&top, TR_KEY_uploaded_bytes, saveme.uploadedBytes);
    tr_variantToFile(&top, TR_VARIANT_FMT_JSON, filename);
    tr_variantClear(&top);
}

void tr_stats::clear()
{
    single_ = old_ = Zero;
    is_dirty_ = true;
    start_time_ = tr_time();
}

[[nodiscard]] tr_session_stats tr_stats::current() const
{
    auto ret = single_;
    ret.secondsActive = time(nullptr) - start_time_;
    ret.ratio = tr_getRatio(ret.uploadedBytes, ret.downloadedBytes);
    return ret;
}

tr_session_stats tr_stats::add(tr_session_stats const& a, tr_session_stats const& b)
{
    auto ret = tr_session_stats{};
    ret.uploadedBytes = a.uploadedBytes + b.uploadedBytes;
    ret.downloadedBytes = a.downloadedBytes + b.downloadedBytes;
    ret.filesAdded = a.filesAdded + b.filesAdded;
    ret.sessionCount = a.sessionCount + b.sessionCount;
    ret.secondsActive = a.secondsActive + b.secondsActive;
    ret.ratio = tr_getRatio(ret.uploadedBytes, ret.downloadedBytes);
    return ret;
}
