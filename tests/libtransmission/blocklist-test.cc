/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstdio>
#include <cstring> // strlen()
// #include <unistd.h> // sync()

#include "transmission.h"
#include "blocklist.h"
#include "file.h"
#include "peer-socket.h"
#include "net.h"
#include "session.h" // tr_sessionIsAddressBlocked()
#include "utils.h"

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
        tr_sys_file_t fd;
        char* dir;

        dir = tr_sys_path_dirname(path, nullptr);
        tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0700, nullptr);
        tr_free(dir);

        fd = tr_sys_file_open(path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, nullptr);
        blockingFileWrite(fd, contents, strlen(contents));
        tr_sys_file_close(fd, nullptr);

        sync();
    }

#endif

    bool addressIsBlocked(char const* address_str)
    {
        struct tr_address addr = {};
        tr_address_from_string(&addr, address_str);
        return tr_sessionIsAddressBlocked(session_, &addr);
    }
};

TEST_F(BlocklistTest, parsing)
{
    EXPECT_EQ(0, tr_blocklistGetRuleCount(session_));

    // init the blocklist
    auto const path = makeString(tr_buildPath(tr_sessionGetConfigDir(session_), "blocklists", "level1", nullptr));
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_TRUE(tr_blocklistExists(session_));
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

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
}

/***
****
***/

TEST_F(BlocklistTest, updating)
{
    // init the session
    char* path = tr_buildPath(tr_sessionGetConfigDir(session_), "blocklists", "level1", nullptr);

    // no blocklist to start with...
    EXPECT_EQ(0, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents2);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(6, tr_blocklistGetRuleCount(session_));

    // test that updated source files will get loaded
    createFileWithContents(path, Contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

    // ensure that new files, if bad, get skipped
    createFileWithContents(path, "# nothing useful\n");
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

    // cleanup
    tr_free(path);
}

} // namespace test

} // namespace libtransmission
