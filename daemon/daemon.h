// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

struct tr_error;

typedef struct dtr_callbacks
{
    int (*on_start)(void* arg, bool foreground);
    void (*on_stop)(void* arg);
    void (*on_reconfigure)(void* arg);
} dtr_callbacks;

bool dtr_daemon(dtr_callbacks const* cb, void* cb_arg, bool foreground, int* exit_code, struct tr_error** error);
