/*
  $Id$

  Copyright (c) 2006 Joshua Elsasser. All rights reserved.
   
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   
  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef TG_IO_H
#define TG_IO_H

typedef void (*iofunc_t)(GSource*, void*);
typedef void (*ioidfunc_t)(GSource*, unsigned int, void*);
typedef unsigned int (*iodatafunc_t)(GSource*, char*, unsigned int, void*);
typedef void (*ionewfunc_t)(GSource*, int, struct sockaddr*, socklen_t, void*);

GSource *
io_new(int fd, ioidfunc_t sent, iodatafunc_t received,
       iofunc_t closed, void *cbdata);

GSource *
io_new_listening(int fd, socklen_t len, ionewfunc_t accepted,
                 iofunc_t closed, void *cbdata);

unsigned int
io_send(GSource *source, const char *data, unsigned int len);

unsigned int
io_send_keepdata(GSource *source, char *data, unsigned int len);

#endif /* TG_IO_H */
