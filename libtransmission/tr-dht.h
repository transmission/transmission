/*
Copyright (c) 2009 by Juliusz Chroboczek

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

enum
{
    TR_DHT_STOPPED      = 0,
    TR_DHT_BROKEN       = 1,
    TR_DHT_POOR         = 2,
    TR_DHT_FIREWALLED   = 3,
    TR_DHT_GOOD         = 4
};

int  tr_dhtInit( tr_session *, const tr_address * );
void tr_dhtUninit( tr_session * );
tr_bool tr_dhtEnabled( tr_session * );
tr_port tr_dhtPort ( tr_session * );
int tr_dhtStatus( tr_session *, int af, int * setme_nodeCount );
const char *tr_dhtPrintableStatus(int status);
int tr_dhtAddNode( tr_session *, const tr_address *, tr_port, tr_bool bootstrap );
int tr_dhtAnnounce( tr_torrent *, int af, tr_bool announce );
