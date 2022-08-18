// This file Copyright (C) 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"
#include "file.h"
#include "subprocess.h"
#include "utils.h" // tr_env_get_string()

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
            tr_sys_file_write_line(fd, argv[i]);
        }
    }
    else if (test_action == "--dump-env")
    {
        for (int i = 3; i < argc; ++i)
        {
            auto const value = tr_env_get_string(argv[i], "<null>");
            tr_sys_file_write_line(fd, value);
        }
    }
    else if (test_action == "--dump-cwd")
    {
        auto const value = tr_sys_dir_get_current(nullptr);
        tr_sys_file_write_line(fd, !std::empty(value) ? value : "<null>");
    }
    else
    {
        tr_sys_file_close(fd);
        tr_sys_path_remove(tmp_result_path.data());
        return 1;
    }

    tr_sys_file_close(fd);
    tr_sys_path_rename(tmp_result_path.data(), result_path.data());
    return 0;
}
