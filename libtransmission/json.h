/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_JSON_H

int tr_jsonParse( const void *     vbuf,
                  size_t           len,
                  struct tr_benc * setme_benc,
                  const uint8_t ** setme_end );

#endif
