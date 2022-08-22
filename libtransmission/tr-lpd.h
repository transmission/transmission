// This file Copyright © 2010 Johannes Lieder.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory> // for std::unique_ptr

#include "transmission.h"

#include "net.h" // for tr_address

struct tr_session;
struct tr_torrent;

class tr_lpd
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;
        virtual tr_port port() const = 0;
    };

    virtual ~tr_lpd() = default;
    static std::unique_ptr<tr_lpd> create(Mediator& mediator, tr_session& session, tr_address addr);

protected:
    tr_lpd() = default;
};
