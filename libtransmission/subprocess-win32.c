/*
 * This file Copyright (C) 2011-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>

#include "transmission.h"
#include "error.h"
#include "subprocess.h"
#include "tr-assert.h"
#include "utils.h"

enum tr_app_type
{
    TR_APP_TYPE_EXE,
    TR_APP_TYPE_BATCH
};

static void set_system_error(tr_error** error, DWORD code, char const* what)
{
    if (error == NULL)
    {
        return;
    }

    char* message = tr_win32_format_message(code);

    if (message == NULL)
    {
        message = tr_strdup_printf("Unknown error: 0x%08lx", code);
    }

    if (what == NULL)
    {
        tr_error_set_literal(error, code, message);
    }
    else
    {
        tr_error_set(error, code, "%s failed: %s", what, message);
    }

    tr_free(message);
}

static void append_to_env_block(wchar_t** env_block, size_t* env_block_len, wchar_t const* part, size_t part_len)
{
    *env_block = tr_renew(wchar_t, *env_block, *env_block_len + part_len + 1);
    wmemcpy(*env_block + *env_block_len, part, part_len);
    *env_block_len += part_len;
}

static bool parse_env_block_part(wchar_t const* part, size_t* full_len, size_t* name_len)
{
    TR_ASSERT(part != NULL);

    wchar_t* const equals_pos = wcschr(part, L'=');

    if (equals_pos == NULL)
    {
        /* Invalid part */
        return false;
    }

    ptrdiff_t const my_name_len = equals_pos - part;

    if (my_name_len > SIZE_MAX)
    {
        /* Invalid part */
        return false;
    }

    if (full_len != NULL)
    {
        /* Includes terminating '\0' */
        *full_len = wcslen(part) + 1;
    }

    if (name_len != NULL)
    {
        *name_len = (size_t)my_name_len;
    }

    return true;
}

static int compare_wide_strings_ci(wchar_t const* lhs, size_t lhs_len, wchar_t const* rhs, size_t rhs_len)
{
    int diff = wcsnicmp(lhs, rhs, MIN(lhs_len, rhs_len));

    if (diff == 0)
    {
        diff = lhs_len < rhs_len ? -1 : (lhs_len > rhs_len ? 1 : 0);
    }

    return diff;
}

static int compare_env_part_names(wchar_t const** lhs, wchar_t const** rhs)
{
    int ret = 0;

    size_t lhs_part_len;
    size_t lhs_name_len;

    if (parse_env_block_part(*lhs, &lhs_part_len, &lhs_name_len))
    {
        size_t rhs_part_len;
        size_t rhs_name_len;

        if (parse_env_block_part(*rhs, &rhs_part_len, &rhs_name_len))
        {
            ret = compare_wide_strings_ci(*lhs, lhs_name_len, *rhs, rhs_name_len);
        }
    }

    return ret;
}

static wchar_t** to_wide_env(char const* const* env)
{
    if (env == NULL || env[0] == NULL)
    {
        return NULL;
    }

    size_t part_count = 0;

    while (env[part_count] != NULL)
    {
        ++part_count;
    }

    wchar_t** wide_env = tr_new(wchar_t*, part_count + 1);

    for (size_t i = 0; i < part_count; ++i)
    {
        wide_env[i] = tr_win32_utf8_to_native(env[i], -1);
    }

    wide_env[part_count] = NULL;

    /* "The sort is case-insensitive, Unicode order, without regard to locale" (c) MSDN */
    qsort(wide_env, part_count, sizeof(wchar_t*), &compare_env_part_names);

    return wide_env;
}

static bool create_env_block(char const* const* env, wchar_t** env_block, tr_error** error)
{
    wchar_t** wide_env = to_wide_env(env);

    if (wide_env == NULL)
    {
        *env_block = NULL;
        return true;
    }

    wchar_t* const old_env_block = GetEnvironmentStringsW();

    if (old_env_block == NULL)
    {
        set_system_error(error, GetLastError(), "Call to GetEnvironmentStrings()");
        return false;
    }

    *env_block = NULL;

    wchar_t const* old_part = old_env_block;
    size_t env_block_len = 0;

    for (size_t i = 0; wide_env[i] != NULL; ++i)
    {
        wchar_t const* const part = wide_env[i];

        size_t part_len;
        size_t name_len;

        if (!parse_env_block_part(part, &part_len, &name_len))
        {
            continue;
        }

        while (*old_part != L'\0')
        {
            size_t old_part_len;
            size_t old_name_len;

            if (!parse_env_block_part(old_part, &old_part_len, &old_name_len))
            {
                continue;
            }

            int const name_diff = compare_wide_strings_ci(old_part, old_name_len, part, name_len);

            if (name_diff < 0)
            {
                append_to_env_block(env_block, &env_block_len, old_part, old_part_len);
            }

            if (name_diff <= 0)
            {
                old_part += old_part_len;
            }

            if (name_diff >= 0)
            {
                break;
            }
        }

        append_to_env_block(env_block, &env_block_len, part, part_len);
    }

    while (*old_part != L'\0')
    {
        size_t old_part_len;

        if (!parse_env_block_part(old_part, &old_part_len, NULL))
        {
            continue;
        }

        append_to_env_block(env_block, &env_block_len, old_part, old_part_len);
        old_part += old_part_len;
    }

    (*env_block)[env_block_len] = '\0';

    FreeEnvironmentStringsW(old_env_block);

    tr_free_ptrv((void* const*)wide_env);
    tr_free(wide_env);

    return true;
}

static void append_argument(char** arguments, char const* argument)
{
    size_t arguments_len = *arguments != NULL ? strlen(*arguments) : 0u;
    size_t const argument_len = strlen(argument);

    if (arguments_len > 0)
    {
        (*arguments)[arguments_len++] = ' ';
    }

    if (!tr_str_is_empty(argument) && strpbrk(argument, " \t\n\v\"") == NULL)
    {
        *arguments = tr_renew(char, *arguments, arguments_len + argument_len + 2);
        strcpy(*arguments + arguments_len, argument);
        return;
    }

    *arguments = tr_renew(char, *arguments, arguments_len + argument_len * 2 + 4);

    char* dst = *arguments + arguments_len;
    *(dst++) = '"';

    for (char const* src = argument; *src != '\0';)
    {
        size_t backslash_count = 0;

        while (*src == '\\')
        {
            ++backslash_count;
            ++src;
        }

        switch (*src)
        {
        case '\0':
            backslash_count = backslash_count * 2;
            break;

        case '"':
            backslash_count = backslash_count * 2 + 1;
            break;
        }

        if (backslash_count != 0)
        {
            memset(dst, '\\', backslash_count);
            dst += backslash_count;
        }

        if (*src != '\0')
        {
            *(dst++) = *(src++);
        }
    }

    *(dst++) = '"';
    *(dst++) = '\0';
}

static bool contains_batch_metachars(char const* text)
{
    /* First part - chars explicitly documented by `cmd.exe /?` as "special" */
    return strpbrk(text, "&<>()@^|" "%!^\"") != NULL;
}

static enum tr_app_type get_app_type(char const* app)
{
    if (tr_str_has_suffix(app, ".cmd") || tr_str_has_suffix(app, ".bat"))
    {
        return TR_APP_TYPE_BATCH;
    }

    /* TODO: Support other types? */

    return TR_APP_TYPE_EXE;
}

static void append_app_launcher_arguments(enum tr_app_type app_type, char** args)
{
    switch (app_type)
    {
    case TR_APP_TYPE_EXE:
        break;

    case TR_APP_TYPE_BATCH:
        append_argument(args, "cmd.exe");
        append_argument(args, "/d");
        append_argument(args, "/e:off");
        append_argument(args, "/v:off");
        append_argument(args, "/s");
        append_argument(args, "/c");
        break;

    default:
        TR_ASSERT_MSG(false, "unsupported application type %d", (int)app_type);
        break;
    }
}

static bool construct_cmd_line(char const* const* cmd, wchar_t** cmd_line)
{
    enum tr_app_type const app_type = get_app_type(cmd[0]);

    char* args = NULL;
    size_t arg_count = 0;
    bool ret = false;

    append_app_launcher_arguments(app_type, &args);

    for (size_t i = 0; cmd[i] != NULL; ++i)
    {
        if (app_type == TR_APP_TYPE_BATCH && i > 0 && contains_batch_metachars(cmd[i]))
        {
            /* FIXME: My attempts to escape them one or another way didn't lead to anything good so far */
            goto cleanup;
        }

        append_argument(&args, cmd[i]);
        ++arg_count;
    }

    *cmd_line = args != NULL ? tr_win32_utf8_to_native(args, -1) : NULL;

    ret = true;

cleanup:
    tr_free(args);
    return ret;
}

bool tr_spawn_async(char* const* cmd, char* const* env, char const* work_dir, tr_error** error)
{
    wchar_t* env_block = NULL;

    if (!create_env_block(env, &env_block, error))
    {
        return false;
    }

    wchar_t* cmd_line;

    if (!construct_cmd_line(cmd, &cmd_line))
    {
        set_system_error(error, ERROR_INVALID_PARAMETER, "Constructing command line");
        return false;
    }

    wchar_t* current_dir = work_dir != NULL ? tr_win32_utf8_to_native(work_dir, -1) : NULL;

    STARTUPINFOW si =
    {
        .cb = sizeof(si),
        .dwFlags = STARTF_USESHOWWINDOW,
        .wShowWindow = SW_HIDE
    };

    PROCESS_INFORMATION pi;

    bool const ret = CreateProcessW(NULL, cmd_line, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT |
        CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE, env_block, current_dir, &si, &pi);

    if (ret)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else
    {
        set_system_error(error, GetLastError(), "Call to CreateProcess()");
    }

    tr_free(current_dir);
    tr_free(cmd_line);
    tr_free(env_block);

    return ret;
}
