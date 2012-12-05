/*
Copyright (c) 2010 by Juliusz Chroboczek

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
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef _TR_UTP_H_
#define _TR_UTP_H_

int tr_utpPacket (const unsigned char *buf, size_t buflen,
                 const struct sockaddr *from, socklen_t fromlen,
                 tr_session *ss);

void tr_utpClose (tr_session *);

void tr_utpSendTo (void *closure, const unsigned char *buf, size_t buflen,
                  const struct sockaddr *to, socklen_t tolen);

#endif /* #ifndef _TR_UTP_H_ */
