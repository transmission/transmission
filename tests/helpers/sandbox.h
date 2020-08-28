/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "libtransmission/error.h"
#include "libtransmission/file.h" // tr_sys_file_*()

#include "tests/helpers/utils.h" // makeString()

#include <cstdlib> // getenv()
#include <cstring> // strlen()
#include <iostream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace transmission
{

namespace tests
{

namespace helpers
{

class Sandbox
{
public:
    Sandbox() :
        parent_dir_{get_default_parent_dir()},
        sandbox_dir_{create_sandbox(parent_dir_, "transmission-test-XXXXXX")}
    {
    }

    ~Sandbox()
    {
        rimraf(sandbox_dir_);
    }

    std::string const& path() const
    {
        return sandbox_dir_;
    }

protected:
    static std::string get_default_parent_dir()
    {
        auto* path = getenv("TMPDIR");
        if (path != NULL)
        {
            return path;
        }

        tr_error* error = nullptr;
        path = tr_sys_dir_get_current(&error);
        if (path != nullptr)
        {
            std::string const ret = path;
            tr_free(path);
            return ret;
        }

        std::cerr << "tr_sys_dir_get_current error: '" << error->message << "'" << std::endl;
        tr_error_free(error);
        return {};
    }

    static std::string create_sandbox(std::string const& parent_dir, std::string const& tmpl)
    {
        std::string path = makeString(tr_buildPath(parent_dir.data(), tmpl.data(), nullptr));
        tr_sys_dir_create_temp(&path.front(), nullptr);
        tr_sys_path_native_separators(&path.front());
        return path;
    }

    static auto get_folder_files(std::string const& path)
    {
        std::vector<std::string> ret;

        tr_sys_path_info info;
        if (tr_sys_path_get_info(path.data(), 0, &info, nullptr) &&
            (info.type == TR_SYS_PATH_IS_DIRECTORY))
        {
            auto const odir = tr_sys_dir_open(path.data(), nullptr);
            if (odir != TR_BAD_SYS_DIR)
            {
                char const* name;
                while ((name = tr_sys_dir_read_name(odir, nullptr)) != nullptr)
                {
                    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                    {
                        ret.push_back(makeString(tr_buildPath(path.data(), name, nullptr)));
                    }
                }

                tr_sys_dir_close(odir, nullptr);
            }
        }

        return ret;
    }

    static void rimraf(std::string const& path, bool verbose = false)
    {
        for (auto const& child : get_folder_files(path))
        {
            rimraf(child, verbose);
        }

        if (verbose)
        {
            std::cerr << "cleanup: removing '" << path << "'" << std::endl;
        }

        tr_sys_path_remove(path.data(), nullptr);
    }

private:
    std::string const parent_dir_;
    std::string const sandbox_dir_;
};

class SandboxedTest : public ::testing::Test
{
protected:
    std::string sandboxDir() const { return sandbox_.path(); }

    auto currentTestName() const
    {
        auto const* i = ::testing::UnitTest::GetInstance()->current_test_info();
        auto child = std::string(i->test_suite_name());
        child += '_';
        child += i->name();
        return child;
    }

    void buildParentDir(std::string const& path) const
    {
        auto const tmperr = errno;

        auto const dir = makeString(tr_sys_path_dirname(path.c_str(), nullptr));
        tr_error* error = nullptr;
        tr_sys_dir_create(dir.data(), TR_SYS_DIR_CREATE_PARENTS, 0700, &error);
        EXPECT_EQ(nullptr, error) << "path[" << path << "] dir[" << dir << "] " << error->code << ", " << error->message;

        errno = tmperr;
    }

    static void blockingFileWrite(tr_sys_file_t fd, void const* data, size_t data_len)
    {
        uint64_t n_left = data_len;
        auto const* left = static_cast<uint8_t const*>(data);

        while (n_left > 0)
        {
            uint64_t n = {};
            tr_error* error = nullptr;
            if (!tr_sys_file_write(fd, left, n_left, &n, &error))
            {
                fprintf(stderr, "Error writing file: '%s'\n", error->message);
                tr_error_free(error);
                break;
            }

            left += n;
            n_left -= n;
        }
    }

    void createTmpfileWithContents(std::string& tmpl, void const* payload, size_t n) const
    {
        auto const tmperr = errno;

        buildParentDir(tmpl);

        // NOLINTNEXTLINE(clang-analyzer-cplusplus.InnerPointer)
        auto const fd = tr_sys_file_open_temp(&tmpl.front(), nullptr);
        blockingFileWrite(fd, payload, n);
        tr_sys_file_close(fd, nullptr);
        sync();

        errno = tmperr;
    }

    void createFileWithContents(std::string const& path, void const* payload, size_t n) const
    {
        auto const tmperr = errno;

        buildParentDir(path);

        auto const fd = tr_sys_file_open(path.c_str(),
            TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
            0600, nullptr);
        blockingFileWrite(fd, payload, n);
        tr_sys_file_close(fd, nullptr);
        sync();

        errno = tmperr;
    }

    void createFileWithContents(std::string const& path, void const* payload) const
    {
        createFileWithContents(path, payload, strlen(static_cast<char const*>(payload)));
    }

    bool verbose = false;

    void sync() const
    {
#ifndef _WIN32
        ::sync();
#endif
    }

private:
    Sandbox sandbox_;
};

} // namespace helpers

} // namespace tests

} // namespace transmission
