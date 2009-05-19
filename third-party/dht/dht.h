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

typedef void
dht_callback(void *closure, int event,
             unsigned char *info_hash,
             void *data, size_t data_len);

#define DHT_EVENT_NONE 0
#define DHT_EVENT_VALUES 1
#define DHT_EVENT_SEARCH_DONE 2

extern FILE *dht_debug;

int dht_init(int s, const unsigned char *id, const unsigned char *v);
int dht_insert_node(int s, const unsigned char *id, struct sockaddr_in *sin);
int dht_ping_node(int s, struct sockaddr_in *sin);
int dht_periodic(int s, int available, time_t *tosleep,
                 dht_callback *callback, void *closure);
int dht_search(int s, const unsigned char *id, int port,
               dht_callback *callback, void *closure);
int dht_nodes(int *good_return, int *dubious_return, int *cached_return,
              int *incoming_return);
void dht_dump_tables(FILE *f);
int dht_get_nodes(struct sockaddr_in *sins, int num);
int dht_uninit(int s, int dofree);

/* This must be provided by the user. */
void dht_hash(void *hash_return, int hash_size,
              const void *v1, int len1,
              const void *v2, int len2,
              const void *v3, int len3);
int dht_random_bytes(void *buf, size_t size);
