/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

struct tr_error;

typedef struct dtr_callbacks
{
    int (* on_start)(void* arg, bool foreground);
    void (* on_stop)(void* arg);
    void (* on_reconfigure)(void* arg);
}
dtr_callbacks;

bool dtr_daemon(dtr_callbacks const* cb, void* cb_arg, bool foreground, int* exit_code, struct tr_error** error);
