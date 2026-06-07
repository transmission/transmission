// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/net.h>
#include <libtransmission/session.h> // tr_session.addressIsBlocked()
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

namespace tr::test
{

class BlocklistTest : public SessionTest
{
protected:
    static auto constexpr Contents1 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
        "IPv6 example:2001:db8::-2001:db8:ffff:ffff:ffff:ffff:ffff:ffff\n"sv;

    static auto constexpr Contents2 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
        "IPv6 example:2001:db8::-2001:db8:ffff:ffff:ffff:ffff:ffff:ffff\n"
        "Evilcorp:216.88.88.0-216.88.88.255\n"sv;

    static auto constexpr ContentsEmpty = ""sv;

    static auto constexpr ContentsWhitespaceOnly = " \t\r\n\f\n"sv;

    static auto constexpr ContentsWithComments =
        "# this is a comment\n"
        "// this is a comment\n"
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
        "IPv6 example:2001:db8::-2001:db8:ffff:ffff:ffff:ffff:ffff:ffff\n"
        "Evilcorp:216.88.88.0-216.88.88.255\n"sv;

    static auto constexpr ContentsCommentsOnly =
        "# this is a comment\n"
        "// this is a comment\n"sv;

    bool addressIsBlocked(char const* address_str)
    {
        auto const addr = tr_address::from_string(address_str);
        return !addr || session_->blocklist().contains(*addr);
    }
};

TEST_F(BlocklistTest, parsing)
{
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // init the blocklist
    auto const path = tr_pathbuf{ session_->configDir(), "/blocklists/level1"sv };
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_TRUE(tr_blocklistExists(session_));
    EXPECT_EQ(size_t{ 6 }, tr_blocklistGetRuleCount(session_));

    // enable the blocklist
    EXPECT_FALSE(tr_blocklistIsEnabled(session_));
    tr_blocklistSetEnabled(session_, true);
    EXPECT_TRUE(tr_blocklistIsEnabled(session_));

    // test blocked addresses
    EXPECT_FALSE(addressIsBlocked("0.0.0.1"));
    EXPECT_TRUE(addressIsBlocked("10.1.2.3"));
    EXPECT_FALSE(addressIsBlocked("216.16.1.143"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.144"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.145"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.146"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.147"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.148"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.149"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.150"));
    EXPECT_TRUE(addressIsBlocked("216.16.1.151"));
    EXPECT_FALSE(addressIsBlocked("216.16.1.152"));
    EXPECT_FALSE(addressIsBlocked("216.16.1.153"));
    EXPECT_FALSE(addressIsBlocked("217.0.0.1"));
    EXPECT_FALSE(addressIsBlocked("255.0.0.1"));
    // IPv6
    EXPECT_TRUE(addressIsBlocked("2001:db8:dead:beef:dead:beef:dead:beef"));
    EXPECT_TRUE(addressIsBlocked("2001:db8:ffff:ffff:ffff:ffff:ffff:fffe"));
    EXPECT_FALSE(addressIsBlocked("fe80:1:1:1:1:1:1:1337"));
    EXPECT_FALSE(addressIsBlocked("2a05:d012:8b5:6501:b6d2:c4fb:b11:5181"));
    EXPECT_FALSE(addressIsBlocked("1::1"));
    EXPECT_FALSE(addressIsBlocked("ffff::ffff"));
}

TEST_F(BlocklistTest, reloading)
{
    auto const path = tr_pathbuf{ session_->configDir(), "/blocklists/level1"sv };

    // no blocklist to start with...
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(6U, tr_blocklistGetRuleCount(session_));

    // test that empty files are loaded as 0 rules
    createFileWithContents(path, ContentsEmpty);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents2);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(7U, tr_blocklistGetRuleCount(session_));

    // test that whitespace-only files are loaded as 0 rules
    createFileWithContents(path, ContentsWhitespaceOnly);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // test that comments get ignored and not treated as bad
    createFileWithContents(path, ContentsWithComments);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(7U, tr_blocklistGetRuleCount(session_));

    // ensure that new files, if bad, get skipped
    createFileWithContents(path, "sdfsdjkfasfildbg\n");
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(7U, tr_blocklistGetRuleCount(session_));

    createFileWithContents(path, ContentsCommentsOnly);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));
}

TEST_F(BlocklistTest, updating)
{
    auto const path = tr_pathbuf{ sandboxDir(), "/blocklist.tmp" };

    // no blocklist to start with...
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents1);
    EXPECT_EQ(6U, tr_blocklistSetContent(session_, path));
    EXPECT_EQ(6U, tr_blocklistGetRuleCount(session_));

    // test that empty files are loaded as 0 rules
    createFileWithContents(path, ContentsEmpty);
    EXPECT_EQ(0U, tr_blocklistSetContent(session_, path));
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents2);
    EXPECT_EQ(7U, tr_blocklistSetContent(session_, path));
    EXPECT_EQ(7U, tr_blocklistGetRuleCount(session_));

    // test that whitespace-only files are loaded as 0 rules
    createFileWithContents(path, ContentsWhitespaceOnly);
    EXPECT_EQ(0U, tr_blocklistSetContent(session_, path));
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // test that comments get ignored and not treated as bad
    createFileWithContents(path, ContentsWithComments);
    EXPECT_EQ(7U, tr_blocklistSetContent(session_, path));
    EXPECT_EQ(7U, tr_blocklistGetRuleCount(session_));

    // ensure that new files, if bad, get skipped
    createFileWithContents(path, "sdfsdjkfasfildbg\n");
    EXPECT_FALSE(tr_blocklistSetContent(session_, path));
    EXPECT_EQ(7U, tr_blocklistGetRuleCount(session_));

    // test that comments get ignored and not treated as bad
    createFileWithContents(path, ContentsCommentsOnly);
    EXPECT_EQ(0U, tr_blocklistSetContent(session_, path));
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));
}

} // namespace tr::test
