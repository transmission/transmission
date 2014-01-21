/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_VERIFY_H
#define TR_VERIFY_H 1

/**
 * @addtogroup file_io File IO
 * @{
 */

void tr_verifyAdd (tr_torrent           * tor,
                   tr_verify_done_func    callback_func,
                   void                 * callback_user_data);

void tr_verifyRemove (tr_torrent * tor);

void tr_verifyClose (tr_session *);

/* @} */

#endif
