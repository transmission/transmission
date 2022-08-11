// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <functional>
#include <tuple>
#include <utility>

#include "tr-macros.h"

struct tr_session;

void tr_evthread_init();

void tr_eventInit(tr_session* session);

void tr_eventClose(tr_session* session);

bool tr_amInEventThread(tr_session const* session);

void tr_runInEventThread(tr_session* session, std::function<void(void)>&& func);

template<typename Func, typename... Args>
void tr_runInEventThread(tr_session* session, Func&& func, Args&&... args)
{
    tr_runInEventThread(
        session,
        std::function<void(void)>{ [func = std::forward<Func&&>(func), args = std::make_tuple(std::forward<Args>(args)...)]()
                                   {
                                       std::apply(std::move(func), std::move(args));
                                   } });
}
