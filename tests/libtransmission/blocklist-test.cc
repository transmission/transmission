// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> // strlen()
// #include <unistd.h> // sync()

#include <libtransmission/transmission.h>

#include <libtransmission/blocklist.h>
#include <libtransmission/file.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-socket.h>
#include <libtransmission/session.h> // tr_session.tr_session.addressIsBlocked()
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

namespace libtransmission::test
{

class BlocklistTest : public SessionTest
{
protected:
    static char const constexpr* const Contents1 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
        "IPv6 example:2001:db8::-2001:db8:ffff:ffff:ffff:ffff:ffff:ffff\n";

    static char const constexpr* const Contents2 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
        "IPv6 example:2001:db8::-2001:db8:ffff:ffff:ffff:ffff:ffff:ffff\n"
        "Evilcorp:216.88.88.0-216.88.88.255\n";

    bool addressIsBlocked(char const* address_str)
    {
        auto const addr = tr_address::from_string(address_str);
        return !addr || session_->addressIsBlocked(*addr);
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

/***
****
***/

TEST_F(BlocklistTest, updating)
{
    // init the session
    auto const path = tr_pathbuf{ session_->configDir(), "/blocklists/level1"sv };

    // no blocklist to start with...
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(6U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents2);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(7U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(6U, tr_blocklistGetRuleCount(session_));

    // ensure that new files, if bad, get skipped
    createFileWithContents(path, "# nothing useful\n");
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(6U, tr_blocklistGetRuleCount(session_));

    // cleanup
}

} // namespace libtransmission::test
