// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#pragma once

#include <memory>

#include <event2/buffer.h>
#include <event2/event.h>

namespace libtransmission::evhelpers
{

struct BufferDeleter
{
    void operator()(struct evbuffer* buf) const noexcept
    {
        if (buf != nullptr)
        {
            evbuffer_free(buf);
        }
    }
};

using evbuffer_unique_ptr = std::unique_ptr<struct evbuffer, BufferDeleter>;

struct EventBaseDeleter
{
    void operator()(struct event_base* evbase) const noexcept
    {
        if (evbase != nullptr)
        {
            event_base_free(evbase);
        }
    }
};

using evbase_unique_ptr = std::unique_ptr<struct event_base, EventBaseDeleter>;

struct EventDeleter
{
    void operator()(struct event* event) const
    {
        if (event != nullptr)
        {
            event_del(event);
            event_free(event);
        }
    }
};

using event_unique_ptr = std::unique_ptr<struct event, EventDeleter>;

} // namespace libtransmission::evhelpers
