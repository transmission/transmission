/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#ifndef TG_IO_H
#define TG_IO_H

typedef void (*iofunc_t)(GSource*, void*);
typedef void (*ioidfunc_t)(GSource*, size_t, void*);
typedef size_t (*iodatafunc_t)(GSource*, void*, size_t, void*);
typedef void (*ionewfunc_t)(GSource*, int, struct sockaddr*, socklen_t, void*);

GSource *
io_new(int fd, ioidfunc_t sent, iodatafunc_t received,
       iofunc_t closed, void *cbdata);

GSource *
io_new_listening(int fd, socklen_t len, ionewfunc_t accepted,
                 iofunc_t closed, void *cbdata);

unsigned int
io_send(GSource *source, const void *data, size_t len);

unsigned int
io_send_keepdata(GSource *source, void *data, size_t len);

#endif /* TG_IO_H */
