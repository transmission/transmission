/******************************************************************************
 * $Id$
 *
 * Copyright (c) Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef GTR_CONFIG_H
#define GTR_CONFIG_H

#include <inttypes.h>
#include <libtransmission/transmission.h> /* tr_variant, tr_session */
#include <libtransmission/quark.h>

void                gtr_pref_init            (const char * config_dir);

int64_t             gtr_pref_int_get         (const tr_quark key);
void                gtr_pref_int_set         (const tr_quark key, int64_t value);

double              gtr_pref_double_get      (const tr_quark key);
void                gtr_pref_double_set      (const tr_quark key, double value);

gboolean            gtr_pref_flag_get        (const tr_quark key);
void                gtr_pref_flag_set        (const tr_quark key, gboolean value);

const char*         gtr_pref_string_get      (const tr_quark key);
void                gtr_pref_string_set      (const tr_quark key, const char * value);

void                gtr_pref_save            (tr_session *);
struct tr_variant*  gtr_pref_get_all         (void);

#endif /* GTR_CONFIG_H */
