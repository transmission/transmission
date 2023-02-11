// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>

#include "event2/buffer.h"
#include "event2/event.h"
#include "event2/http.h"

#include "transmission.h"

#include "utils-ev.h"

namespace libtransmission::evhelpers
{

void BufferDeleter::operator()(struct evbuffer* buf) const noexcept
{
    if (buf != nullptr)
    {
        evbuffer_free(buf);
    }
}

void EventBaseDeleter::operator()(struct event_base* evbase) const noexcept
{
    if (evbase != nullptr)
    {
        event_base_free(evbase);
    }
}

void EventDeleter::operator()(struct event* event) const
{
    if (event != nullptr)
    {
        event_del(event);
        event_free(event);
    }
}

void EvhttpDeleter::operator()(struct evhttp* evh) const noexcept
{
    if (evh != nullptr)
    {
        evhttp_free(evh);
    }
}

} // namespace libtransmission::evhelpers
