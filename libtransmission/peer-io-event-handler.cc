// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/peer-io.h"
#include "libtransmission/peer-io-event-handler.h"
#include "libtransmission/log.h"
#include "libtransmission/socket-event-handler.h"

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

namespace libtransmission
{

PeerIoEventHandler::PeerIoEventHandler(tr_peerIo* io, tr_socket_t socket)
    : io_{ io }
{
    read_event_handler_ = io_->create_socket_read_event_handler(socket);
    write_event_handler_ = io_->create_socket_write_event_handler(socket);
}

PeerIoEventHandler::~PeerIoEventHandler()
{
    close();
}

void PeerIoEventHandler::enable_read()
{
    if (!read_enabled_ && read_event_handler_)
    {
        tr_logAddTraceIo(io_, "enabling ready-to-read polling");
        read_event_handler_->start();
        read_enabled_ = true;
    }
}

void PeerIoEventHandler::enable_write()
{
    if (!write_enabled_ && write_event_handler_)
    {
        tr_logAddTraceIo(io_, "enabling ready-to-write polling");
        write_event_handler_->start();
        write_enabled_ = true;
    }
}

void PeerIoEventHandler::disable_read()
{
    if (read_enabled_ && read_event_handler_)
    {
        tr_logAddTraceIo(io_, "disabling ready-to-read polling");
        read_event_handler_->stop();
        read_enabled_ = false;
    }
}

void PeerIoEventHandler::disable_write()
{
    if (write_enabled_ && write_event_handler_)
    {
        tr_logAddTraceIo(io_, "disabling ready-to-write polling (libuv)");
        write_event_handler_->stop();
        write_enabled_ = false;
    }
}

bool PeerIoEventHandler::is_read_enabled() const
{
    return read_enabled_;
}

bool PeerIoEventHandler::is_write_enabled() const
{
    return write_enabled_;
}

void PeerIoEventHandler::close()
{
    disable_read();
    disable_write();
    read_event_handler_.reset();
    write_event_handler_.reset();
}

} // namespace libtransmission
