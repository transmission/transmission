// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> // strlen()
// #include <unistd.h> // sync()

#include "transmission.h"

#include "blocklist.h"
#include "file.h"
#include "net.h"
#include "peer-socket.h"
#include "session.h" // tr_session.tr_session.addressIsBlocked()
#include "tr-strbuf.h"

#include "test-fixtures.h"

namespace libtransmission
{

namespace test
{

class BlocklistTest : public SessionTest
{
protected:
    static char const constexpr* const Contents1 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n";

    static char const constexpr* const Contents2 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
        "Evilcorp:216.88.88.0-216.88.88.255\n";

#if 0
    void createFileWithContents(char const* path, char const* contents)
    {
        auto const dir = tr_sys_path_dirname(path);
        tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0700);

        auto const fd = tr_sys_file_open(path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600);
        blockingFileWrite(fd, contents, strlen(contents));
        tr_sys_file_close(fd);

        sync();
    }

#endif

    bool addressIsBlocked(char const* address_str)
    {
        auto const addr = tr_address::fromString(address_str);
        return !addr || session_->addressIsBlocked(*addr);
    }
};

TEST_F(BlocklistTest, parsing)
{
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_));

    // init the blocklist
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto const path = tr_pathbuf{ session_->configDir(), "/blocklists/level1"sv };
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    createFileWithContents(path, Contents1);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    tr_sessionReloadBlocklists(session_);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(tr_blocklistExists(session_));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_EQ(size_t{ 5 }, tr_blocklistGetRuleCount(session_));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;

    // enable the blocklist
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_FALSE(tr_blocklistIsEnabled(session_));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    tr_blocklistSetEnabled(session_, true);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(tr_blocklistIsEnabled(session_));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;

    // test blocked addresses
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_FALSE(addressIsBlocked("0.0.0.1"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("10.1.2.3"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_FALSE(addressIsBlocked("216.16.1.143"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.144"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.145"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.146"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.147"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.148"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.149"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.150"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_TRUE(addressIsBlocked("216.16.1.151"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_FALSE(addressIsBlocked("216.16.1.152"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_FALSE(addressIsBlocked("216.16.1.153"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_FALSE(addressIsBlocked("217.0.0.1"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_FALSE(addressIsBlocked("255.0.0.1"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
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
    EXPECT_EQ(5U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents2);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(6U, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5U, tr_blocklistGetRuleCount(session_));

    // ensure that new files, if bad, get skipped
    createFileWithContents(path, "# nothing useful\n");
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5U, tr_blocklistGetRuleCount(session_));

    // cleanup
}

} // namespace test

} // namespace libtransmission
