/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "file.h"
#include "subprocess.h"
#include "utils.h"

#include <memory>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        return 1;
    }

    auto const result_path = std::string{ argv[1] };
    auto const test_action = std::string{ argv[2] };
    auto const tmp_result_path = result_path + ".tmp";

    auto fd = tr_sys_file_open(
        tmp_result_path.data(), // NOLINT
        TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
        0644,
        nullptr);

    if (fd == TR_BAD_SYS_FILE)
    {
        return 1;
    }

    if (test_action == "--dump-args")
    {
        for (int i = 3; i < argc; ++i)
        {
            tr_sys_file_write_line(fd, argv[i], nullptr);
        }
    }
    else if (test_action == "--dump-env")
    {
        for (int i = 3; i < argc; ++i)
        {
            char* const value = tr_env_get_string(argv[i], "<null>");
            tr_sys_file_write_line(fd, value, nullptr);
            tr_free(value);
        }
    }
    else if (test_action == "--dump-cwd")
    {
        char* const value = tr_sys_dir_get_current(nullptr);
        tr_sys_file_write_line(fd, value != nullptr ? value : "<null>", nullptr);
        tr_free(value);
    }
    else
    {
        tr_sys_file_close(fd, nullptr);
        tr_sys_path_remove(tmp_result_path.data(), nullptr);
        return 1;
    }

    tr_sys_file_close(fd, nullptr);
    tr_sys_path_rename(tmp_result_path.data(), result_path.data(), nullptr);
    return 0;
}
