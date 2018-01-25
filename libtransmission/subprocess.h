/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

bool tr_spawn_async(char* const* cmd, char* const* env, char const* work_dir, struct tr_error** error);
