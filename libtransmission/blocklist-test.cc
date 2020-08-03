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

namespace libtransmission::test
{

class BlocklistTest: public SessionTest
{
protected:
    char const* contents1 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n";

    char const* contents2 =
        "10.5.6.7/8\n"
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255\n"
        "Corel Corporation:216.21.157.192-216.21.157.223\n"
        "Fox Speed Channel:216.79.131.192-216.79.131.223\n"
        "Evilcorp:216.88.88.0-216.88.88.255\n";

    void create_text_file(char const* path, char const* contents)
    {
        tr_sys_file_t fd;
        char* dir;

        dir = tr_sys_path_dirname(path, NULL);
        tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0700, NULL);
        tr_free(dir);

        fd = tr_sys_file_open(path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, NULL);
        blocking_file_write(fd, contents, strlen(contents));
        tr_sys_file_close(fd, NULL);

        sync();
    }

    bool address_is_blocked(tr_session* session, char const* address_str)
    {
        struct tr_address addr;
        tr_address_from_string(&addr, address_str);
        return tr_sessionIsAddressBlocked(session, &addr);
    }
};

TEST_F(BlocklistTest, parsing)
{
    // char* path;
    // tr_session* session;

    /* init the session */
    // session = libttest_session_init(NULL);
    // EXPECT_TRUE(!tr_blocklistExists(session));
    EXPECT_EQ(0, tr_blocklistGetRuleCount(session_));

    /* init the blocklist */
    char* path = tr_buildPath(tr_sessionGetConfigDir(session_), "blocklists", "level1", NULL);
    create_text_file(path, contents1);
    tr_free(path);
    tr_sessionReloadBlocklists(session_);
    EXPECT_TRUE(tr_blocklistExists(session_));
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

    /* enable the blocklist */
    EXPECT_TRUE(!tr_blocklistIsEnabled(session_));
    tr_blocklistSetEnabled(session_, true);
    EXPECT_TRUE(tr_blocklistIsEnabled(session_));

    /* test blocked addresses */
    EXPECT_TRUE(!address_is_blocked(session_, "0.0.0.1"));
    EXPECT_TRUE(address_is_blocked(session_, "10.1.2.3"));
    EXPECT_TRUE(!address_is_blocked(session_, "216.16.1.143"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.144"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.145"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.146"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.147"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.148"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.149"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.150"));
    EXPECT_TRUE(address_is_blocked(session_, "216.16.1.151"));
    EXPECT_TRUE(!address_is_blocked(session_, "216.16.1.152"));
    EXPECT_TRUE(!address_is_blocked(session_, "216.16.1.153"));
    EXPECT_TRUE(!address_is_blocked(session_, "217.0.0.1"));
    EXPECT_TRUE(!address_is_blocked(session_, "255.0.0.1"));

    /* cleanup */
    // libttest_session_close(session);
}

/***
****
***/

TEST_F(BlocklistTest, updating)
{
    //tr_session* session;

    /* init the session */
    // session = libttest_session_init(NULL);
    char* path = tr_buildPath(tr_sessionGetConfigDir(session_), "blocklists", "level1", NULL);

    /* no blocklist to start with... */
    EXPECT_EQ(0, tr_blocklistGetRuleCount(session_));

    /* test that updated source files will get loaded */
    create_text_file(path, contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

    /* test that updated source files will get loaded */
    create_text_file(path, contents2);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(6, tr_blocklistGetRuleCount(session_));

    /* test that updated source files will get loaded */
    create_text_file(path, contents1);
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

    /* ensure that new files, if bad, get skipped */
    create_text_file(path, "# nothing useful\n");
    tr_sessionReloadBlocklists(session_);
    EXPECT_EQ(5, tr_blocklistGetRuleCount(session_));

    /* cleanup */
    //libttest_session_close(session);
    tr_free(path);
}

}  // namespace libtransmission::test
