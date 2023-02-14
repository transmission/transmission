// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#pragma once

#include <memory>

struct evbuffer;
struct event;
struct event_base;
struct evhttp;

namespace libtransmission::evhelpers
{

struct BufferDeleter
{
    void operator()(struct evbuffer* buf) const noexcept;
};

using evbuffer_unique_ptr = std::unique_ptr<struct evbuffer, BufferDeleter>;

struct EventBaseDeleter
{
    void operator()(struct event_base* evbase) const noexcept;
};

using evbase_unique_ptr = std::unique_ptr<struct event_base, EventBaseDeleter>;

struct EventDeleter
{
    void operator()(struct event* event) const;
};

using event_unique_ptr = std::unique_ptr<struct event, EventDeleter>;

struct EvhttpDeleter
{
    void operator()(struct evhttp* evh) const noexcept;
};

using evhttp_unique_ptr = std::unique_ptr<struct evhttp, EvhttpDeleter>;

} // namespace libtransmission::evhelpers
