// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string>
#include <vector>

#include "file.h" // for tr_sys_file_t
#include "tr-assert.h"
#include "tr-macros.h"

struct tr_address;

struct tr_ipv4_range
{
    uint32_t begin;
    uint32_t end;
};

struct tr_blocklistFile
{
public:
    tr_blocklistFile(char const* filename, bool isEnabled)
        : is_enabled_(isEnabled)
        , fd_{ TR_BAD_SYS_FILE }
        , filename_(filename)
    {
    }

    ~tr_blocklistFile();

    void close();

    [[nodiscard]] bool exists() const;

    [[nodiscard]] char const* getFilename() const;

    // TODO: This function should be const, but cannot be const due to it calling ensureLoaded()
    size_t getRuleCount();

    [[nodiscard]] bool isEnabled() const;

    void setEnabled(bool isEnabled);

    bool hasAddress(tr_address const& addr);

    /// @brief Read the file of ranges, sort and merge, write to our own file, and reload from it
    size_t setContent(char const* filename);

private:
    void ensureLoaded();
    void load();
    static bool parseLine(char const* line, struct tr_ipv4_range* range);
    static bool compareAddressRangesByFirstAddress(tr_ipv4_range const& a, tr_ipv4_range const& b);

#ifdef TR_ENABLE_ASSERTS
    /// @brief Sanity checks: make sure the rules are sorted in ascending order and don't overlap
    void assertValidRules(std::vector<tr_ipv4_range>& ranges);
#endif

    bool is_enabled_;
    tr_sys_file_t fd_;
    size_t rule_count_ = 0;
    uint64_t byte_count_ = 0;
    std::string filename_;
    tr_ipv4_range* rules_ = nullptr;
};
