/*
Copyright (c) 2010 by Johannes Lieder

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

int tr_lpdInit(tr_session*, tr_address*);
void tr_lpdUninit(tr_session*);
bool tr_lpdEnabled(tr_session const*);
bool tr_lpdSendAnnounce(tr_torrent const*);

/**
* @defgroup Preproc Helper macros
* @{
*
* @def lengthof
* @brief returns the static length of a C array type
* @note A lower case macro name is tolerable here since this definition of lengthof()
* is intimately related to sizeof semantics.
* Meaningful return values are only guaranteed for true array types. */
#define lengthof(arr) (sizeof(*(arr)) > 0 ? sizeof(arr) / sizeof(*(arr)) : 0)

/**
* @} */
