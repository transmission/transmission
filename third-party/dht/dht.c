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

/* Please, please, please.

   You are welcome to integrate this code in your favourite Bittorrent
   client.  Please remember, however, that it is meant to be usable by
   others, including myself.  This means no C++, no relicensing, and no
   gratuitious changes to the coding style.  And please send back any
   improvements to the author. */

/* For memmem. */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "dht.h"

#ifndef HAVE_MEMMEM
#ifdef __GLIBC__
#define HAVE_MEMMEM
#endif
#endif

#ifndef MSG_CONFIRM
#define MSG_CONFIRM 0
#endif

/* We set sin_family to 0 to mark unused slots. */
#if AF_INET == 0 || AF_INET6 == 0
#error You lose
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* nothing */
#elif defined(__GNUC__)
#define inline __inline
#if  (__GNUC__ >= 3)
#define restrict __restrict
#else
#define restrict /**/
#endif
#else
#define inline /**/
#define restrict /**/
#endif

#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) <= (y) ? (x) : (y))

struct node {
    unsigned char id[20];
    struct sockaddr_storage ss;
    int sslen;
    time_t time;                /* time of last message received */
    time_t reply_time;          /* time of last correct reply received */
    time_t pinged_time;         /* time of last request */
    int pinged;                 /* how many requests we sent since last reply */
    struct node *next;
};

struct bucket {
    int af;
    unsigned char first[20];
    int count;                  /* number of nodes */
    int time;                   /* time of last reply in this bucket */
    struct node *nodes;
    struct sockaddr_storage cached;  /* the address of a likely candidate */
    int cachedlen;
    struct bucket *next;
};

struct search_node {
    unsigned char id[20];
    struct sockaddr_storage ss;
    int sslen;
    time_t request_time;        /* the time of the last unanswered request */
    time_t reply_time;          /* the time of the last reply */
    int pinged;
    unsigned char token[40];
    int token_len;
    int replied;                /* whether we have received a reply */
    int acked;                  /* whether they acked our announcement */
};

/* When performing a search, we search for up to SEARCH_NODES closest nodes
   to the destination, and use the additional ones to backtrack if any of
   the target 8 turn out to be dead. */
#define SEARCH_NODES 14

struct search {
    unsigned short tid;
    int af;
    time_t step_time;           /* the time of the last search_step */
    unsigned char id[20];
    unsigned short port;        /* 0 for pure searches */
    int done;
    struct search_node nodes[SEARCH_NODES];
    int numnodes;
    struct search *next;
};

struct peer {
    time_t time;
    unsigned char ip[16];
    unsigned short len;
    unsigned short port;
};

/* The maximum number of peers we store for a given hash. */
#ifndef DHT_MAX_PEERS
#define DHT_MAX_PEERS 2048
#endif

/* The maximum number of hashes we're willing to track. */
#ifndef DHT_MAX_HASHES
#define DHT_MAX_HASHES 16384
#endif

/* The maximum number of searches we keep data about. */
#ifndef DHT_MAX_SEARCHES
#define DHT_MAX_SEARCHES 1024
#endif

/* The time after which we consider a search to be expirable. */
#ifndef DHT_SEARCH_EXPIRE_TIME
#define DHT_SEARCH_EXPIRE_TIME (62 * 60)
#endif

struct storage {
    unsigned char id[20];
    int numpeers, maxpeers;
    struct peer *peers;
    struct storage *next;
};

static int send_ping(struct sockaddr *sa, int salen,
                     const unsigned char *tid, int tid_len);
static int send_pong(struct sockaddr *sa, int salen,
                     const unsigned char *tid, int tid_len);
static int send_find_node(struct sockaddr *sa, int salen,
                          const unsigned char *tid, int tid_len,
                          const unsigned char *target, int want, int confirm);
static int send_nodes_peers(struct sockaddr *sa, int salen,
                            const unsigned char *tid, int tid_len,
                            const unsigned char *nodes, int nodes_len,
                            const unsigned char *nodes6, int nodes6_len,
                            int af, struct storage *st,
                            const unsigned char *token, int token_len);
static int send_closest_nodes(struct sockaddr *sa, int salen,
                              const unsigned char *tid, int tid_len,
                              const unsigned char *id, int want,
                              int af, struct storage *st,
                              const unsigned char *token, int token_len);
static int send_get_peers(struct sockaddr *sa, int salen,
                          unsigned char *tid, int tid_len,
                          unsigned char *infohash, int want, int confirm);
static int send_announce_peer(struct sockaddr *sa, int salen,
                              unsigned char *tid, int tid_len,
                              unsigned char *infohas, unsigned short port,
                              unsigned char *token, int token_len, int confirm);
static int send_peer_announced(struct sockaddr *sa, int salen,
                               unsigned char *tid, int tid_len);
static int send_error(struct sockaddr *sa, int salen,
                      unsigned char *tid, int tid_len,
                      int code, const char *message);

#define ERROR 0
#define REPLY 1
#define PING 2
#define FIND_NODE 3
#define GET_PEERS 4
#define ANNOUNCE_PEER 5

#define WANT4 1
#define WANT6 2

static int parse_message(const unsigned char *buf, int buflen,
                         unsigned char *tid_return, int *tid_len,
                         unsigned char *id_return,
                         unsigned char *info_hash_return,
                         unsigned char *target_return,
                         unsigned short *port_return,
                         unsigned char *token_return, int *token_len,
                         unsigned char *nodes_return, int *nodes_len,
                         unsigned char *nodes6_return, int *nodes6_len,
                         unsigned char *values_return, int *values_len,
                         unsigned char *values6_return, int *values6_len,
                         int *want_return);

static const unsigned char zeroes[20] = {0};
static const unsigned char ones[20] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
static const unsigned char v4prefix[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0
};

static int dht_socket = -1;
static int dht_socket6 = -1;

static time_t search_time;
static time_t confirm_nodes_time;
static time_t rotate_secrets_time;

static unsigned char myid[20];
static int have_v = 0;
static unsigned char my_v[9];
static unsigned char secret[8];
static unsigned char oldsecret[8];

static struct bucket *buckets = NULL;
static struct bucket *buckets6 = NULL;
static struct storage *storage;
static int numstorage;

static struct search *searches = NULL;
static int numsearches;
static unsigned short search_id;

/* The maximum number of nodes that we snub.  There is probably little
   reason to increase this value. */
#ifndef DHT_MAX_BLACKLISTED
#define DHT_MAX_BLACKLISTED 10
#endif
static struct sockaddr_storage blacklist[DHT_MAX_BLACKLISTED];
int next_blacklisted;

static struct timeval now;
static time_t mybucket_grow_time, mybucket6_grow_time;
static time_t expire_stuff_time;

#define MAX_LEAKY_BUCKET_TOKENS 40
static time_t leaky_bucket_time;
static int leaky_bucket_tokens;

FILE *dht_debug = NULL;

#ifdef __GNUC__
    __attribute__ ((format (printf, 1, 2)))
#endif
static void
debugf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if(dht_debug)
        vfprintf(dht_debug, format, args);
    va_end(args);
    fflush(dht_debug);
}

static void
debug_printable(const unsigned char *buf, int buflen)
{
    int i;
    if(dht_debug) {
        for(i = 0; i < buflen; i++)
            putc(buf[i] >= 32 && buf[i] <= 126 ? buf[i] : '.', dht_debug);
    }
}

static void
print_hex(FILE *f, const unsigned char *buf, int buflen)
{
    int i;
    for(i = 0; i < buflen; i++)
        fprintf(f, "%02x", buf[i]);
}

static int
is_martian(struct sockaddr *sa)
{
    switch(sa->sa_family) {
    case AF_INET: {
        struct sockaddr_in *sin = (struct sockaddr_in*)sa;
        const unsigned char *address = (const unsigned char*)&sin->sin_addr;
        return sin->sin_port == 0 ||
            (address[0] == 0) ||
            (address[0] == 127) ||
            ((address[0] & 0xE0) == 0xE0);
    }
    case AF_INET6: {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
        const unsigned char *address = (const unsigned char*)&sin6->sin6_addr;
        return sin6->sin6_port == 0 ||
            (address[0] == 0xFF) ||
            (address[0] == 0xFE && (address[1] & 0xC0) == 0x80) ||
            (memcmp(address, zeroes, 15) == 0 &&
             (address[15] == 0 || address[15] == 1)) ||
            (memcmp(address, v4prefix, 12) == 0);
    }

    default:
        return 0;
    }
}

/* Forget about the ``XOR-metric''.  An id is just a path from the
   root of the tree, so bits are numbered from the start. */

static inline int
id_cmp(const unsigned char *restrict id1, const unsigned char *restrict id2)
{
    /* Memcmp is guaranteed to perform an unsigned comparison. */
    return memcmp(id1, id2, 20);
}

/* Find the lowest 1 bit in an id. */
static int
lowbit(const unsigned char *id)
{
    int i, j;
    for(i = 19; i >= 0; i--)
        if(id[i] != 0)
            break;

    if(i < 0)
        return -1;

    for(j = 7; j >= 0; j--)
        if((id[i] & (0x80 >> j)) != 0)
            break;

    return 8 * i + j;
}

/* Find how many bits two ids have in common. */
static int
common_bits(const unsigned char *id1, const unsigned char *id2)
{
    int i, j;
    unsigned char xor;
    for(i = 0; i < 20; i++) {
        if(id1[i] != id2[i])
            break;
    }

    if(i == 20)
        return 160;

    xor = id1[i] ^ id2[i];

    j = 0;
    while((xor & 0x80) == 0) {
        xor <<= 1;
        j++;
    }

    return 8 * i + j;
}

/* Determine whether id1 or id2 is closer to ref */
static int
xorcmp(const unsigned char *id1, const unsigned char *id2,
       const unsigned char *ref)
{
    int i;
    for(i = 0; i < 20; i++) {
        unsigned char xor1, xor2;
        if(id1[i] == id2[i])
            continue;
        xor1 = id1[i] ^ ref[i];
        xor2 = id2[i] ^ ref[i];
        if(xor1 < xor2)
            return -1;
        else
            return 1;
    }
    return 0;
}

/* We keep buckets in a sorted linked list.  A bucket b ranges from
   b->first inclusive up to b->next->first exclusive. */
static int
in_bucket(const unsigned char *id, struct bucket *b)
{
    return id_cmp(b->first, id) <= 0 &&
        (b->next == NULL || id_cmp(id, b->next->first) < 0);
}

static struct bucket *
find_bucket(unsigned const char *id, int af)
{
    struct bucket *b = af == AF_INET ? buckets : buckets6;

    if(b == NULL)
        return NULL;

    while(1) {
        if(b->next == NULL)
            return b;
        if(id_cmp(id, b->next->first) < 0)
            return b;
        b = b->next;
    }
}

static struct bucket *
previous_bucket(struct bucket *b)
{
    struct bucket *p = b->af == AF_INET ? buckets : buckets6;

    if(b == p)
        return NULL;

    while(1) {
        if(p->next == NULL)
            return NULL;
        if(p->next == b)
            return p;
        p = p->next;
    }
}

/* Every bucket contains an unordered list of nodes. */
static struct node *
find_node(const unsigned char *id, int af)
{
    struct bucket *b = find_bucket(id, af);
    struct node *n;

    if(b == NULL)
        return NULL;

    n = b->nodes;
    while(n) {
        if(id_cmp(n->id, id) == 0)
            return n;
        n = n->next;
    }
    return NULL;
}

/* Return a random node in a bucket. */
static struct node *
random_node(struct bucket *b)
{
    struct node *n;
    int nn;

    if(b->count == 0)
        return NULL;

    nn = random() % b->count;
    n = b->nodes;
    while(nn > 0 && n) {
        n = n->next;
        nn--;
    }
    return n;
}

/* Return the middle id of a bucket. */
static int
bucket_middle(struct bucket *b, unsigned char *id_return)
{
    int bit1 = lowbit(b->first);
    int bit2 = b->next ? lowbit(b->next->first) : -1;
    int bit = MAX(bit1, bit2) + 1;

    if(bit >= 160)
        return -1;

    memcpy(id_return, b->first, 20);
    id_return[bit / 8] |= (0x80 >> (bit % 8));
    return 1;
}

/* Return a random id within a bucket. */
static int
bucket_random(struct bucket *b, unsigned char *id_return)
{
    int bit1 = lowbit(b->first);
    int bit2 = b->next ? lowbit(b->next->first) : -1;
    int bit = MAX(bit1, bit2) + 1;
    int i;

    if(bit >= 160) {
        memcpy(id_return, b->first, 20);
        return 1;
    }

    memcpy(id_return, b->first, bit / 8);
    id_return[bit / 8] = b->first[bit / 8] & (0xFF00 >> (bit % 8));
    id_return[bit / 8] |= random() & 0xFF >> (bit % 8);
    for(i = bit / 8 + 1; i < 20; i++)
        id_return[i] = random() & 0xFF;
    return 1;
}

/* Insert a new node into a bucket. */
static struct node *
insert_node(struct node *node)
{
    struct bucket *b = find_bucket(node->id, node->ss.ss_family);

    if(b == NULL)
        return NULL;

    node->next = b->nodes;
    b->nodes = node;
    b->count++;
    return node;
}

/* This is our definition of a known-good node. */
static int
node_good(struct node *node)
{
    return
        node->pinged <= 2 &&
        node->reply_time >= now.tv_sec - 7200 &&
        node->time >= now.tv_sec - 900;
}

/* Our transaction-ids are 4-bytes long, with the first two bytes identi-
   fying the kind of request, and the remaining two a sequence number in
   host order. */

static void
make_tid(unsigned char *tid_return, const char *prefix, unsigned short seqno)
{
    tid_return[0] = prefix[0] & 0xFF;
    tid_return[1] = prefix[1] & 0xFF;
    memcpy(tid_return + 2, &seqno, 2);
}

static int
tid_match(const unsigned char *tid, const char *prefix,
          unsigned short *seqno_return)
{
    if(tid[0] == (prefix[0] & 0xFF) && tid[1] == (prefix[1] & 0xFF)) {
        if(seqno_return)
            memcpy(seqno_return, tid + 2, 2);
        return 1;
    } else
        return 0;
}

/* Every bucket caches the address of a likely node.  Ping it. */
static int
send_cached_ping(struct bucket *b)
{
    unsigned char tid[4];
    int rc;
    /* We set family to 0 when there's no cached node. */
    if(b->cached.ss_family == 0)
        return 0;

    debugf("Sending ping to cached node.\n");
    make_tid(tid, "pn", 0);
    rc = send_ping((struct sockaddr*)&b->cached, b->cachedlen, tid, 4);
    b->cached.ss_family = 0;
    b->cachedlen = 0;
    return rc;
}

/* Split a bucket into two equal parts. */
static struct bucket *
split_bucket(struct bucket *b)
{
    struct bucket *new;
    struct node *nodes;
    int rc;
    unsigned char new_id[20];

    rc = bucket_middle(b, new_id);
    if(rc < 0)
        return NULL;

    new = calloc(1, sizeof(struct bucket));
    if(new == NULL)
        return NULL;

    new->af = b->af;

    send_cached_ping(b);

    memcpy(new->first, new_id, 20);
    new->time = b->time;

    nodes = b->nodes;
    b->nodes = NULL;
    b->count = 0;
    new->next = b->next;
    b->next = new;
    while(nodes) {
        struct node *n;
        n = nodes;
        nodes = nodes->next;
        insert_node(n);
    }
    return b;
}

/* Called whenever we send a request to a node. */
static void
pinged(struct node *n, struct bucket *b)
{
    n->pinged++;
    n->pinged_time = now.tv_sec;
    if(n->pinged >= 3)
        send_cached_ping(b ? b : find_bucket(n->id, n->ss.ss_family));
}

/* We just learnt about a node, not necessarily a new one.  Confirm is 1 if
   the node sent a message, 2 if it sent us a reply. */
static struct node *
new_node(const unsigned char *id, struct sockaddr *sa, int salen, int confirm)
{
    struct bucket *b = find_bucket(id, sa->sa_family);
    struct node *n;
    int mybucket, split;

    if(b == NULL)
        return NULL;

    if(id_cmp(id, myid) == 0)
        return NULL;

    if(is_martian(sa))
        return NULL;

    mybucket = in_bucket(myid, b);

    if(confirm == 2)
        b->time = now.tv_sec;

    n = b->nodes;
    while(n) {
        if(id_cmp(n->id, id) == 0) {
            if(confirm || n->time < now.tv_sec - 15 * 60) {
                /* Known node.  Update stuff. */
                memcpy((struct sockaddr*)&n->ss, sa, salen);
                if(confirm)
                    n->time = now.tv_sec;
                if(confirm >= 2) {
                    n->reply_time = now.tv_sec;
                    n->pinged = 0;
                    n->pinged_time = 0;
                }
            }
            return n;
        }
        n = n->next;
    }

    /* New node. */

    if(mybucket) {
        if(sa->sa_family == AF_INET)
            mybucket_grow_time = now.tv_sec;
        else
            mybucket6_grow_time = now.tv_sec;
    }

    /* First, try to get rid of a known-bad node. */
    n = b->nodes;
    while(n) {
        if(n->pinged >= 3 && n->pinged_time < now.tv_sec - 15) {
            memcpy(n->id, id, 20);
            memcpy((struct sockaddr*)&n->ss, sa, salen);
            n->time = confirm ? now.tv_sec : 0;
            n->reply_time = confirm >= 2 ? now.tv_sec : 0;
            n->pinged_time = 0;
            n->pinged = 0;
            return n;
        }
        n = n->next;
    }

    if(b->count >= 8) {
        /* Bucket full.  Ping a dubious node */
        int dubious = 0;
        n = b->nodes;
        while(n) {
            /* Pick the first dubious node that we haven't pinged in the
               last 15 seconds.  This gives nodes the time to reply, but
               tends to concentrate on the same nodes, so that we get rid
               of bad nodes fast. */
            if(!node_good(n)) {
                dubious = 1;
                if(n->pinged_time < now.tv_sec - 15) {
                    unsigned char tid[4];
                    debugf("Sending ping to dubious node.\n");
                    make_tid(tid, "pn", 0);
                    send_ping((struct sockaddr*)&n->ss, n->sslen,
                              tid, 4);
                    n->pinged++;
                    n->pinged_time = now.tv_sec;
                    break;
                }
            }
            n = n->next;
        }

        split = 0;
        if(mybucket) {
            if(!dubious)
                split = 1;
            /* If there's only one bucket, split eagerly.  This is
               incorrect unless there's more than 8 nodes in the DHT. */
            else if(b->af == AF_INET && buckets->next == NULL)
                split = 1;
            else if(b->af == AF_INET6 && buckets6->next == NULL)
                split = 1;
        }

        if(split) {
            debugf("Splitting.\n");
            b = split_bucket(b);
            return new_node(id, sa, salen, confirm);
        }

        /* No space for this node.  Cache it away for later. */
        if(confirm || b->cached.ss_family == 0) {
            memcpy(&b->cached, sa, salen);
            b->cachedlen = salen;
        }

        return NULL;
    }

    /* Create a new node. */
    n = calloc(1, sizeof(struct node));
    if(n == NULL)
        return NULL;
    memcpy(n->id, id, 20);
    memcpy(&n->ss, sa, salen);
    n->sslen = salen;
    n->time = confirm ? now.tv_sec : 0;
    n->reply_time = confirm >= 2 ? now.tv_sec : 0;
    n->next = b->nodes;
    b->nodes = n;
    b->count++;
    return n;
}

/* Called periodically to purge known-bad nodes.  Note that we're very
   conservative here: broken nodes in the table don't do much harm, we'll
   recover as soon as we find better ones. */
static int
expire_buckets(struct bucket *b)
{
    while(b) {
        struct node *n, *p;
        int changed = 0;

        while(b->nodes && b->nodes->pinged >= 4) {
            n = b->nodes;
            b->nodes = n->next;
            b->count--;
            changed = 1;
            free(n);
        }

        p = b->nodes;
        while(p) {
            while(p->next && p->next->pinged >= 4) {
                n = p->next;
                p->next = n->next;
                b->count--;
                changed = 1;
                free(n);
            }
            p = p->next;
        }

        if(changed)
            send_cached_ping(b);

        b = b->next;
    }
    expire_stuff_time = now.tv_sec + 120 + random() % 240;
    return 1;
}

/* While a search is in progress, we don't necessarily keep the nodes being
   walked in the main bucket table.  A search in progress is identified by
   a unique transaction id, a short (and hence small enough to fit in the
   transaction id of the protocol packets). */

static struct search *
find_search(unsigned short tid, int af)
{
    struct search *sr = searches;
    while(sr) {
        if(sr->tid == tid && sr->af == af)
            return sr;
        sr = sr->next;
    }
    return NULL;
}

/* A search contains a list of nodes, sorted by decreasing distance to the
   target.  We just got a new candidate, insert it at the right spot or
   discard it. */

static int
insert_search_node(unsigned char *id,
                   struct sockaddr *sa, int salen,
                   struct search *sr, int replied,
                   unsigned char *token, int token_len)
{
    struct search_node *n;
    int i, j;

    if(sa->sa_family != sr->af) {
        debugf("Attempted to insert node in the wrong family.\n");
        return 0;
    }

    for(i = 0; i < sr->numnodes; i++) {
        if(id_cmp(id, sr->nodes[i].id) == 0) {
            n = &sr->nodes[i];
            goto found;
        }
        if(xorcmp(id, sr->nodes[i].id, sr->id) < 0)
            break;
    }

    if(i == SEARCH_NODES)
        return 0;

    if(sr->numnodes < SEARCH_NODES)
        sr->numnodes++;

    for(j = sr->numnodes - 1; j > i; j--) {
        sr->nodes[j] = sr->nodes[j - 1];
    }

    n = &sr->nodes[i];

    memset(n, 0, sizeof(struct search_node));
    memcpy(n->id, id, 20);

found:
    memcpy(&n->ss, sa, salen);
    n->sslen = salen;

    if(replied) {
        n->replied = 1;
        n->reply_time = now.tv_sec;
        n->request_time = 0;
        n->pinged = 0;
    }
    if(token) {
        if(token_len >= 40) {
            debugf("Eek!  Overlong token.\n");
        } else {
            memcpy(n->token, token, token_len);
            n->token_len = token_len;
        }
    }

    return 1;
}

static void
flush_search_node(struct search_node *n, struct search *sr)
{
    int i = n - sr->nodes, j;
    for(j = i; j < sr->numnodes - 1; j++)
        sr->nodes[j] = sr->nodes[j + 1];
    sr->numnodes--;
}

static void
expire_searches(void)
{
    struct search *sr = searches, *previous = NULL;

    while(sr) {
        struct search *next = sr->next;
        if(sr->step_time < now.tv_sec - DHT_SEARCH_EXPIRE_TIME) {
            if(previous)
                previous->next = next;
            else
                searches = next;
            free(sr);
            numsearches--;
        } else {
            previous = sr;
        }
        sr = next;
    }
}

/* This must always return 0 or 1, never -1, not even on failure (see below). */
static int
search_send_get_peers(struct search *sr, struct search_node *n)
{
    struct node *node;
    unsigned char tid[4];

    if(n == NULL) {
        int i;
        for(i = 0; i < sr->numnodes; i++) {
            if(sr->nodes[i].pinged < 3 && !sr->nodes[i].replied &&
               sr->nodes[i].request_time < now.tv_sec - 15)
                n = &sr->nodes[i];
        }
    }

    if(!n || n->pinged >= 3 || n->replied ||
       n->request_time >= now.tv_sec - 15)
        return 0;

    debugf("Sending get_peers.\n");
    make_tid(tid, "gp", sr->tid);
    send_get_peers((struct sockaddr*)&n->ss, n->sslen, tid, 4, sr->id, -1,
                   n->reply_time >= now.tv_sec - 15);
    n->pinged++;
    n->request_time = now.tv_sec;
    /* If the node happens to be in our main routing table, mark it
       as pinged. */
    node = find_node(n->id, n->ss.ss_family);
    if(node) pinged(node, NULL);
    return 1;
}

/* When a search is in progress, we periodically call search_step to send
   further requests. */
static void
search_step(struct search *sr, dht_callback *callback, void *closure)
{
    int i, j;
    int all_done = 1;

    /* Check if the first 8 live nodes have replied. */
    j = 0;
    for(i = 0; i < sr->numnodes && j < 8; i++) {
        struct search_node *n = &sr->nodes[i];
        if(n->pinged >= 3)
            continue;
        if(!n->replied) {
            all_done = 0;
            break;
        }
        j++;
    }

    if(all_done) {
        if(sr->port == 0) {
            goto done;
        } else {
            int all_acked = 1;
            j = 0;
            for(i = 0; i < sr->numnodes && j < 8; i++) {
                struct search_node *n = &sr->nodes[i];
                struct node *node;
                unsigned char tid[4];
                if(n->pinged >= 3)
                    continue;
                /* A proposed extension to the protocol consists in
                   omitting the token when storage tables are full.  While
                   I don't think this makes a lot of sense -- just sending
                   a positive reply is just as good, let's deal with it. */
                if(n->token_len == 0)
                    n->acked = 1;
                if(!n->acked) {
                    all_acked = 0;
                    debugf("Sending announce_peer.\n");
                    make_tid(tid, "ap", sr->tid);
                    send_announce_peer((struct sockaddr*)&n->ss,
                                       sizeof(struct sockaddr_storage),
                                       tid, 4, sr->id, sr->port,
                                       n->token, n->token_len,
                                       n->reply_time >= now.tv_sec - 15);
                    n->pinged++;
                    n->request_time = now.tv_sec;
                    node = find_node(n->id, n->ss.ss_family);
                    if(node) pinged(node, NULL);
                }
                j++;
            }
            if(all_acked)
                goto done;
        }
        sr->step_time = now.tv_sec;
        return;
    }

    if(sr->step_time + 15 >= now.tv_sec)
        return;

    j = 0;
    for(i = 0; i < sr->numnodes; i++) {
        j += search_send_get_peers(sr, &sr->nodes[i]);
        if(j >= 3)
            break;
    }
    sr->step_time = now.tv_sec;
    return;

 done:
    sr->done = 1;
    if(callback)
        (*callback)(closure,
                    sr->af == AF_INET ?
                    DHT_EVENT_SEARCH_DONE : DHT_EVENT_SEARCH_DONE6,
                    sr->id, NULL, 0);
    sr->step_time = now.tv_sec;
}

static struct search *
new_search(void)
{
    struct search *sr, *oldest = NULL;

    /* Find the oldest done search */
    sr = searches;
    while(sr) {
        if(sr->done &&
           (oldest == NULL || oldest->step_time > sr->step_time))
            oldest = sr;
        sr = sr->next;
    }

    /* The oldest slot is expired. */
    if(oldest && oldest->step_time < now.tv_sec - DHT_SEARCH_EXPIRE_TIME)
        return oldest;

    /* Allocate a new slot. */
    if(numsearches < DHT_MAX_SEARCHES) {
        sr = calloc(1, sizeof(struct search));
        if(sr != NULL) {
            sr->next = searches;
            searches = sr;
            numsearches++;
            return sr;
        }
    }

    /* Oh, well, never mind.  Reuse the oldest slot. */
    return oldest;
}

/* Insert the contents of a bucket into a search structure. */
static void
insert_search_bucket(struct bucket *b, struct search *sr)
{
    struct node *n;
    n = b->nodes;
    while(n) {
        insert_search_node(n->id, (struct sockaddr*)&n->ss, n->sslen,
                           sr, 0, NULL, 0);
        n = n->next;
    }
}

/* Start a search.  If port is non-zero, perform an announce when the
   search is complete. */
int
dht_search(const unsigned char *id, int port, int af,
           dht_callback *callback, void *closure)
{
    struct search *sr;
    struct bucket *b = find_bucket(id, af);

    if(b == NULL) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    sr = searches;
    while(sr) {
        if(sr->af == af && id_cmp(sr->id, id) == 0)
            break;
        sr = sr->next;
    }

    if(sr) {
        /* We're reusing data from an old search.  Reusing the same tid
           means that we can merge replies for both searches. */
        int i;
        sr->done = 0;
    again:
        for(i = 0; i < sr->numnodes; i++) {
            struct search_node *n;
            n = &sr->nodes[i];
            /* Discard any doubtful nodes. */
            if(n->pinged >= 3 || n->reply_time < now.tv_sec - 7200) {
                flush_search_node(n, sr);
                goto again;
            }
            n->pinged = 0;
            n->token_len = 0;
            n->replied = 0;
            n->acked = 0;
        }
    } else {
        sr = new_search();
        if(sr == NULL) {
            errno = ENOSPC;
            return -1;
        }
        sr->af = af;
        sr->tid = search_id++;
        sr->step_time = 0;
        memcpy(sr->id, id, 20);
        sr->done = 0;
        sr->numnodes = 0;
    }

    sr->port = port;

    insert_search_bucket(b, sr);

    if(sr->numnodes < SEARCH_NODES) {
        struct bucket *p = previous_bucket(b);
        if(b->next)
            insert_search_bucket(b->next, sr);
        if(p)
            insert_search_bucket(p, sr);
    }
    if(sr->numnodes < SEARCH_NODES)
        insert_search_bucket(find_bucket(myid, af), sr);

    search_step(sr, callback, closure);
    search_time = now.tv_sec;
    return 1;
}

/* A struct storage stores all the stored peer addresses for a given info
   hash. */

static struct storage *
find_storage(const unsigned char *id)
{
    struct storage *st = storage;

    while(st) {
        if(id_cmp(id, st->id) == 0)
            break;
        st = st->next;
    }
    return st;
}

static int
storage_store(const unsigned char *id, struct sockaddr *sa)
{
    int i, len;
    struct storage *st = storage;
    unsigned char *ip;
    unsigned short port;

    st = find_storage(id);

    if(st == NULL) {
        if(numstorage >= DHT_MAX_HASHES)
            return -1;
        st = calloc(1, sizeof(struct storage));
        if(st == NULL) return -1;
        memcpy(st->id, id, 20);
        st->next = storage;
        storage = st;
        numstorage++;
    }

    if(sa->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in*)sa;
        ip = (unsigned char*)&sin->sin_addr;
        len = 4;
        port = ntohs(sin->sin_port);
    } else if(sa->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
        ip = (unsigned char*)&sin6->sin6_addr;
        len = 16;
        port = ntohs(sin6->sin6_port);
    }

    for(i = 0; i < st->numpeers; i++) {
        if(st->peers[i].port == port && st->peers[i].len == len &&
           memcmp(st->peers[i].ip, ip, len) == 0)
            break;
    }

    if(i < st->numpeers) {
        /* Already there, only need to refresh */
        st->peers[i].time = now.tv_sec;
        return 0;
    } else {
        struct peer *p;
        if(i >= st->maxpeers) {
            /* Need to expand the array. */
            struct peer *new_peers;
            int n;
            if(st->maxpeers >= DHT_MAX_PEERS)
                return 0;
            n = st->maxpeers == 0 ? 2 : 2 * st->maxpeers;
            n = MIN(n, DHT_MAX_PEERS);
            new_peers = realloc(st->peers, n * sizeof(struct peer));
            if(new_peers == NULL)
                return -1;
            st->peers = new_peers;
            st->maxpeers = n;
        }
        p = &st->peers[st->numpeers++];
        p->time = now.tv_sec;
        p->len = len;
        memcpy(p->ip, ip, len);
        p->port = port;
        return 1;
    }
}

static int
expire_storage(void)
{
    struct storage *st = storage, *previous = NULL;
    while(st) {
        int i = 0;
        while(i < st->numpeers) {
            if(st->peers[i].time < now.tv_sec - 32 * 60) {
                if(i != st->numpeers - 1)
                    st->peers[i] = st->peers[st->numpeers - 1];
                st->numpeers--;
            } else {
                i++;
            }
        }

        if(st->numpeers == 0) {
            free(st->peers);
            if(previous)
                previous->next = st->next;
            else
                storage = st->next;
            free(st);
            if(previous)
                st = previous->next;
            else
                st = storage;
            numstorage--;
            if(numstorage < 0) {
                debugf("Eek... numstorage became negative.\n");
                numstorage = 0;
            }
        } else {
            previous = st;
            st = st->next;
        }
    }
    return 1;
}

/* We've just found out that a node is buggy. */
static void
broken_node(const unsigned char *id, struct sockaddr *sa, int salen)
{
    int i;

    debugf("Blacklisting broken node.\n");

    if(id) {
        struct node *n;
        struct search *sr;
        /* Make the node easy to discard. */
        n = find_node(id, sa->sa_family);
        if(n) {
            n->pinged = 3;
            pinged(n, NULL);
        }
        /* Discard it from any searches in progress. */
        sr = searches;
        while(sr) {
            for(i = 0; i < sr->numnodes; i++)
                if(id_cmp(sr->nodes[i].id, id) == 0)
                    flush_search_node(&sr->nodes[i], sr);
            sr = sr->next;
        }
    }
    /* And make sure we don't hear from it again. */
    memcpy(&blacklist[next_blacklisted], sa, salen);
    next_blacklisted = (next_blacklisted + 1) % DHT_MAX_BLACKLISTED;
}

static int
rotate_secrets(void)
{
    int rc;

    rotate_secrets_time = now.tv_sec + 900 + random() % 1800;

    memcpy(oldsecret, secret, sizeof(secret));
    rc = dht_random_bytes(secret, sizeof(secret));

    if(rc < 0)
        return -1;

    return 1;
}

#ifndef TOKEN_SIZE
#define TOKEN_SIZE 8
#endif

static void
make_token(struct sockaddr *sa, int old, unsigned char *token_return)
{
    void *ip;
    int iplen;
    unsigned short port;

    if(sa->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in*)sa;
        ip = &sin->sin_addr;
        iplen = 4;
        port = htons(sin->sin_port);
    } else if(sa->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
        ip = &sin6->sin6_addr;
        iplen = 16;
        port = htons(sin6->sin6_port);
    } else {
        abort();
    }

    dht_hash(token_return, TOKEN_SIZE,
             old ? oldsecret : secret, sizeof(secret),
             ip, iplen, (unsigned char*)&port, 2);
}
static int
token_match(unsigned char *token, int token_len, struct sockaddr *sa)
{
    unsigned char t[TOKEN_SIZE];
    if(token_len != TOKEN_SIZE)
        return 0;
    make_token(sa, 0, t);
    if(memcmp(t, token, TOKEN_SIZE) == 0)
        return 1;
    make_token(sa, 1, t);
    if(memcmp(t, token, TOKEN_SIZE) == 0)
        return 1;
    return 0;
}

int
dht_nodes(int af, int *good_return, int *dubious_return, int *cached_return,
          int *incoming_return)
{
    int good = 0, dubious = 0, cached = 0, incoming = 0;
    struct bucket *b = af == AF_INET ? buckets : buckets6;

    while(b) {
        struct node *n = b->nodes;
        while(n) {
            if(node_good(n)) {
                good++;
                if(n->time > n->reply_time)
                    incoming++;
            } else {
                dubious++;
            }
            n = n->next;
        }
        if(b->cached.ss_family > 0)
            cached++;
        b = b->next;
    }
    if(good_return)
        *good_return = good;
    if(dubious_return)
        *dubious_return = dubious;
    if(cached_return)
        *cached_return = cached;
    if(incoming_return)
        *incoming_return = incoming;
    return good + dubious;
}

static void
dump_bucket(FILE *f, struct bucket *b)
{
    struct node *n = b->nodes;
    fprintf(f, "Bucket ");
    print_hex(f, b->first, 20);
    fprintf(f, " count %d age %d%s%s:\n",
            b->count, (int)(now.tv_sec - b->time),
            in_bucket(myid, b) ? " (mine)" : "",
            b->cached.ss_family ? " (cached)" : "");
    while(n) {
        char buf[512];
        unsigned short port;
        fprintf(f, "    Node ");
        print_hex(f, n->id, 20);
        if(n->ss.ss_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in*)&n->ss;
            inet_ntop(AF_INET, &sin->sin_addr, buf, 512);
            port = ntohs(sin->sin_port);
        } else if(n->ss.ss_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)&n->ss;
            inet_ntop(AF_INET6, &sin6->sin6_addr, buf, 512);
            port = ntohs(sin6->sin6_port);
        } else {
            snprintf(buf, 512, "unknown(%d)", n->ss.ss_family);
            port = 0;
        }

        if(n->ss.ss_family == AF_INET6)
            fprintf(f, " [%s]:%d ", buf, port);
        else
            fprintf(f, " %s:%d ", buf, port);
        if(n->time != n->reply_time)
            fprintf(f, "age %ld, %ld",
                    (long)(now.tv_sec - n->time),
                    (long)(now.tv_sec - n->reply_time));
        else
            fprintf(f, "age %ld", (long)(now.tv_sec - n->time));
        if(n->pinged)
            fprintf(f, " (%d)", n->pinged);
        if(node_good(n))
            fprintf(f, " (good)");
        fprintf(f, "\n");
        n = n->next;
    }

}

void
dht_dump_tables(FILE *f)
{
    int i;
    struct bucket *b;
    struct storage *st = storage;
    struct search *sr = searches;

    fprintf(f, "My id ");
    print_hex(f, myid, 20);
    fprintf(f, "\n");

    b = buckets;
    while(b) {
        dump_bucket(f, b);
        b = b->next;
    }

    fprintf(f, "\n");

    b = buckets6;
    while(b) {
        dump_bucket(f, b);
        b = b->next;
    }

    while(sr) {
        fprintf(f, "\nSearch%s id ", sr->af == AF_INET6 ? " (IPv6)" : "");
        print_hex(f, sr->id, 20);
        fprintf(f, " age %d%s\n", (int)(now.tv_sec - sr->step_time),
               sr->done ? " (done)" : "");
        for(i = 0; i < sr->numnodes; i++) {
            struct search_node *n = &sr->nodes[i];
            fprintf(f, "Node %d id ", i);
            print_hex(f, n->id, 20);
            fprintf(f, " bits %d age ", common_bits(sr->id, n->id));
            if(n->request_time)
                fprintf(f, "%d, ", (int)(now.tv_sec - n->request_time));
            fprintf(f, "%d", (int)(now.tv_sec - n->reply_time));
            if(n->pinged)
                fprintf(f, " (%d)", n->pinged);
            fprintf(f, "%s%s.\n",
                    find_node(n->id, AF_INET) ? " (known)" : "",
                    n->replied ? " (replied)" : "");
        }
        sr = sr->next;
    }

    while(st) {
        fprintf(f, "\nStorage ");
        print_hex(f, st->id, 20);
        fprintf(f, " %d/%d nodes:", st->numpeers, st->maxpeers);
        for(i = 0; i < st->numpeers; i++) {
            char buf[100];
            if(st->peers[i].len == 4) {
                inet_ntop(AF_INET, st->peers[i].ip, buf, 100);
            } else if(st->peers[i].len == 16) {
                buf[0] = '[';
                inet_ntop(AF_INET6, st->peers[i].ip, buf + 1, 98);
                strcat(buf, "]");
            } else {
                strcpy(buf, "???");
            }
            fprintf(f, " %s:%u (%ld)",
                    buf, st->peers[i].port,
                    (long)(now.tv_sec - st->peers[i].time));
        }
        st = st->next;
    }

    fprintf(f, "\n\n");
    fflush(f);
}

int
dht_init(int s, int s6, const unsigned char *id, const unsigned char *v)
{
    int rc;

    if(dht_socket >= 0 || dht_socket6 >= 0 || buckets || buckets6) {
        errno = EBUSY;
        return -1;
    }

    searches = NULL;
    numsearches = 0;

    storage = NULL;
    numstorage = 0;

    if(s >= 0) {
        buckets = calloc(sizeof(struct bucket), 1);
        if(buckets == NULL)
            return -1;
        buckets->af = AF_INET;

        rc = fcntl(s, F_GETFL, 0);
        if(rc < 0)
            goto fail;

        rc = fcntl(s, F_SETFL, (rc | O_NONBLOCK));
        if(rc < 0)
            goto fail;
    }

    if(s6 >= 0) {
        buckets6 = calloc(sizeof(struct bucket), 1);
        if(buckets6 == NULL)
            return -1;
        buckets6->af = AF_INET6;

        rc = fcntl(s6, F_GETFL, 0);
        if(rc < 0)
            goto fail;

        rc = fcntl(s6, F_SETFL, (rc | O_NONBLOCK));
        if(rc < 0)
            goto fail;
    }

    memcpy(myid, id, 20);
    if(v) {
        memcpy(my_v, "1:v4:", 5);
        memcpy(my_v + 5, v, 4);
        have_v = 1;
    } else {
        have_v = 0;
    }

    gettimeofday(&now, NULL);

    mybucket_grow_time = now.tv_sec;
    mybucket6_grow_time = now.tv_sec;
    confirm_nodes_time = now.tv_sec + random() % 3;

    search_id = random() & 0xFFFF;
    search_time = 0;

    next_blacklisted = 0;

    leaky_bucket_time = now.tv_sec;
    leaky_bucket_tokens = MAX_LEAKY_BUCKET_TOKENS;

    memset(secret, 0, sizeof(secret));
    rc = rotate_secrets();
    if(rc < 0)
        goto fail;

    dht_socket = s;
    dht_socket6 = s6;

    expire_buckets(buckets);
    expire_buckets(buckets6);

    return 1;

 fail:
    free(buckets);
    buckets = NULL;
    return -1;
}

int
dht_uninit(int dofree)
{
    if(dht_socket < 0) {
        errno = EINVAL;
        return -1;
    }

    if(!dofree)
        return 1;

    while(buckets) {
        struct bucket *b = buckets;
        buckets = b->next;
        while(b->nodes) {
            struct node *n = b->nodes;
            b->nodes = n->next;
            free(n);
        }
        free(b);
    }

    while(storage) {
        struct storage *st = storage;
        storage = storage->next;
        free(st->peers);
        free(st);
    }

    while(searches) {
        struct search *sr = searches;
        searches = searches->next;
        free(sr);
    }

    return 1;
}

/* Rate control for requests we receive. */

static int
leaky_bucket(void)
{
    if(leaky_bucket_tokens == 0) {
        leaky_bucket_tokens = MIN(MAX_LEAKY_BUCKET_TOKENS,
                                  4 * (now.tv_sec - leaky_bucket_time));
        leaky_bucket_time = now.tv_sec;
    }

    if(leaky_bucket_tokens == 0)
        return 0;

    leaky_bucket_tokens--;
    return 1;
}

static int
neighbourhood_maintenance(int af)
{
    unsigned char id[20];
    struct bucket *b = find_bucket(myid, af);
    struct bucket *q;
    struct node *n;

    if(b == NULL)
        return 0;

    memcpy(id, myid, 20);
    id[19] = random() & 0xFF;
    q = b;
    if(q->next && (q->count == 0 || (random() & 7) == 0))
        q = b->next;
    if(q->count == 0 || (random() & 7) == 0) {
        struct bucket *r;
        r = previous_bucket(b);
        if(r && r->count > 0)
            q = r;
    }

    if(q) {
        /* Since our node-id is the same in both DHTs, it's probably
           profitable to query both families. */
        int want = dht_socket >= 0 && dht_socket6 >= 0 ? (WANT4 | WANT6) : -1;
        n = random_node(q);
        if(n) {
            unsigned char tid[4];
            debugf("Sending find_node for%s neighborhood maintenance.\n",
                   af == AF_INET6 ? " IPv6" : "");
            make_tid(tid, "fn", 0);
            send_find_node((struct sockaddr*)&n->ss, n->sslen,
                           tid, 4, id, want,
                           n->reply_time >= now.tv_sec - 15);
            pinged(n, q);
        }
        return 1;
    }
    return 0;
}

static int
bucket_maintenance(int af)
{
    struct bucket *b;

    b = af == AF_INET ? buckets : buckets6;

    while(b) {
        struct bucket *q;
        if(b->time < now.tv_sec - 600) {
            /* This bucket hasn't seen any positive confirmation for a long
               time.  Pick a random id in this bucket's range, and send
               a request to a random node. */
            unsigned char id[20];
            struct node *n;
            int rc;

            rc = bucket_random(b, id);
            if(rc < 0)
                memcpy(id, b->first, 20);

            q = b;
            /* If the bucket is empty, we try to fill it from a neighbour.
               We also sometimes do it gratuitiously to recover from
               buckets full of broken nodes. */
            if(q->next && (q->count == 0 || (random() & 7) == 0))
                q = b->next;
            if(q->count == 0 || (random() & 7) == 0) {
                struct bucket *r;
                r = previous_bucket(b);
                if(r && r->count > 0)
                    q = r;
            }

            if(q) {
                n = random_node(q);
                if(n) {
                    unsigned char tid[4];
                    int want = -1;

                    if(dht_socket >= 0 && dht_socket6 >= 0) {
                        struct bucket *otherbucket;
                        otherbucket =
                            find_bucket(id, af == AF_INET ? AF_INET6 : AF_INET);
                        if(otherbucket && otherbucket->count < 8)
                            /* The corresponding bucket in the other family
                               is emptyish -- querying both is useful. */
                            want = WANT4 | WANT6;
                        else if(random() % 37 == 0)
                            /* Most of the time, this just adds overhead.
                               However, it might help stitch back one of
                               the DHTs after a network collapse, so query
                               both, but only very occasionally. */
                            want = WANT4 | WANT6;
                    }

                    debugf("Sending find_node for%s bucket maintenance.\n",
                           af == AF_INET6 ? " IPv6" : "");
                    make_tid(tid, "fn", 0);
                    send_find_node((struct sockaddr*)&n->ss, n->sslen,
                                   tid, 4, id, want,
                                   n->reply_time >= now.tv_sec - 15);
                    pinged(n, q);
                    /* In order to avoid sending queries back-to-back,
                       give up for now and reschedule us soon. */
                    return 1;
                }
            }
        }
        b = b->next;
    }
    return 0;
}

int
dht_periodic(int available, time_t *tosleep,
             dht_callback *callback, void *closure)
{
    int i;

    gettimeofday(&now, NULL);

    if(available) {
        int rc, message;
        unsigned char tid[16], id[20], info_hash[20], target[20];
        unsigned char buf[1536], nodes[256], nodes6[1024], token[128];
        int tid_len = 16, token_len = 128;
        int nodes_len = 256, nodes6_len = 1024;
        unsigned short port;
        unsigned char values[2048], values6[2048];
        int values_len = 2048, values6_len = 2048;
        int want, want4, want6;
        struct sockaddr_storage source_storage;
        struct sockaddr *source = (struct sockaddr*)&source_storage;
        socklen_t sourcelen = sizeof(source_storage);
        unsigned short ttid;

        rc = -1;
        if(dht_socket >= 0) {
            rc = recvfrom(dht_socket, buf, 1536, 0, source, &sourcelen);
            if(rc < 0 && errno != EAGAIN) {
                    return rc;
            }
        }
        if(dht_socket6 >= 0 && rc < 0) {
            rc = recvfrom(dht_socket6, buf, 1536, 0,
                          source, &sourcelen);
            if(rc < 0 && errno != EAGAIN) {
                    return rc;
            }
        }

        if(rc < 0 || sourcelen > sizeof(struct sockaddr_storage))
            goto dontread;

        if(is_martian(source))
            goto dontread;

        for(i = 0; i < DHT_MAX_BLACKLISTED; i++) {
            if(memcmp(&blacklist[i], source, sourcelen) == 0) {
                debugf("Received packet from blacklisted node.\n");
                goto dontread;
            }
        }

        /* There's a bug in parse_message -- it will happily overflow the
           buffer if it's not NUL-terminated.  For now, put a NUL at the
           end of buffers. */

        if(rc < 1536) {
            buf[rc] = '\0';
        } else {
            debugf("Overlong message.\n");
            goto dontread;
        }

        message = parse_message(buf, rc, tid, &tid_len, id, info_hash,
                                target, &port, token, &token_len,
                                nodes, &nodes_len, nodes6, &nodes6_len,
                                values, &values_len, values6, &values6_len,
                                &want);

        if(message < 0 || message == ERROR || id_cmp(id, zeroes) == 0) {
            debugf("Unparseable message: ");
            debug_printable(buf, rc);
            debugf("\n");
            goto dontread;
        }

        if(id_cmp(id, myid) == 0) {
            debugf("Received message from self.\n");
            goto dontread;
        }

        if(message > REPLY) {
            /* Rate limit requests. */
            if(!leaky_bucket()) {
                debugf("Dropping request due to rate limiting.\n");
                goto dontread;
            }
        }

        if(want > 0) {
            want4 = (want & WANT4);
            want6 = (want & WANT6);
        } else {
            want4 = source->sa_family == AF_INET;
            want6 = source->sa_family == AF_INET6;
        }

        switch(message) {
        case REPLY:
            if(tid_len != 4) {
                debugf("Broken node truncates transaction ids: ");
                debug_printable(buf, rc);
                debugf("\n");
                /* This is really annoying, as it means that we will
                   time-out all our searches that go through this node.
                   Kill it. */
                broken_node(id, source, sourcelen);
                goto dontread;
            }
            if(tid_match(tid, "pn", NULL)) {
                debugf("Pong!\n");
                new_node(id, source, sourcelen, 2);
            } else if(tid_match(tid, "fn", NULL) ||
                      tid_match(tid, "gp", NULL)) {
                int gp = 0;
                struct search *sr = NULL;
                if(tid_match(tid, "gp", &ttid)) {
                    gp = 1;
                    sr = find_search(ttid, source->sa_family);
                }
                debugf("Nodes found (%d+%d)%s!\n", nodes_len/26, nodes6_len/38,
                       gp ? " for get_peers" : "");
                if(nodes_len % 26 != 0 || nodes6_len % 38 != 0) {
                    debugf("Unexpected length for node info!\n");
                    broken_node(id, source, sourcelen);
                } else if(gp && sr == NULL) {
                    debugf("Unknown search!\n");
                    new_node(id, source, sourcelen, 1);
                } else {
                    int i;
                    new_node(id, source, sourcelen, 2);
                    for(i = 0; i < nodes_len / 26; i++) {
                        unsigned char *ni = nodes + i * 26;
                        struct sockaddr_in sin;
                        if(id_cmp(ni, myid) == 0)
                            continue;
                        memset(&sin, 0, sizeof(sin));
                        sin.sin_family = AF_INET;
                        memcpy(&sin.sin_addr, ni + 20, 4);
                        memcpy(&sin.sin_port, ni + 24, 2);
                        new_node(ni, (struct sockaddr*)&sin, sizeof(sin), 0);
                        if(sr && sr->af == AF_INET) {
                            insert_search_node(ni,
                                               (struct sockaddr*)&sin,
                                               sizeof(sin),
                                               sr, 0, NULL, 0);
                        }
                    }
                    for(i = 0; i < nodes6_len / 38; i++) {
                        unsigned char *ni = nodes6 + i * 38;
                        struct sockaddr_in6 sin6;
                        if(id_cmp(ni, myid) == 0)
                            continue;
                        memset(&sin6, 0, sizeof(sin6));
                        sin6.sin6_family = AF_INET6;
                        memcpy(&sin6.sin6_addr, ni + 20, 16);
                        memcpy(&sin6.sin6_port, ni + 36, 2);
                        new_node(ni, (struct sockaddr*)&sin6, sizeof(sin6), 0);
                        if(sr && sr->af == AF_INET6) {
                            insert_search_node(ni,
                                               (struct sockaddr*)&sin6,
                                               sizeof(sin6),
                                               sr, 0, NULL, 0);
                        }
                    }
                    if(sr)
                        /* Since we received a reply, the number of
                           requests in flight has decreased.  Let's push
                           another request. */
                        search_send_get_peers(sr, NULL);
                }
                if(sr) {
                    insert_search_node(id, source, sourcelen, sr,
                                       1, token, token_len);
                    if(values_len > 0 || values6_len > 0) {
                        debugf("Got values (%d+%d)!\n",
                               values_len / 6, values6_len / 18);
                        if(callback) {
                            if(values_len > 0)
                                (*callback)(closure, DHT_EVENT_VALUES, sr->id,
                                            (void*)values, values_len);

                            if(values6_len > 0)
                                (*callback)(closure, DHT_EVENT_VALUES6, sr->id,
                                            (void*)values6, values6_len);
                        }
                    }
                }
            } else if(tid_match(tid, "ap", &ttid)) {
                struct search *sr;
                debugf("Got reply to announce_peer.\n");
                sr = find_search(ttid, source->sa_family);
                if(!sr) {
                    debugf("Unknown search!\n");
                    new_node(id, source, sourcelen, 1);
                } else {
                    int i;
                    new_node(id, source, sourcelen, 2);
                    for(i = 0; i < sr->numnodes; i++)
                        if(id_cmp(sr->nodes[i].id, id) == 0) {
                            sr->nodes[i].request_time = 0;
                            sr->nodes[i].reply_time = now.tv_sec;
                            sr->nodes[i].acked = 1;
                            sr->nodes[i].pinged = 0;
                            break;
                        }
                    /* See comment for gp above. */
                    search_send_get_peers(sr, NULL);
                }
            } else {
                debugf("Unexpected reply: ");
                debug_printable(buf, rc);
                debugf("\n");
            }
            break;
        case PING:
            debugf("Ping (%d)!\n", tid_len);
            new_node(id, source, sourcelen, 1);
            debugf("Sending pong.\n");
            send_pong(source, sourcelen, tid, tid_len);
            break;
        case FIND_NODE:
            debugf("Find node!\n");
            new_node(id, source, sourcelen, 1);
            debugf("Sending closest nodes (%d).\n", want);
            send_closest_nodes(source, sourcelen,
                               tid, tid_len, target, want,
                               0, NULL, NULL, 0);
            break;
        case GET_PEERS:
            debugf("Get_peers!\n");
            new_node(id, source, sourcelen, 1);
            if(id_cmp(info_hash, zeroes) == 0) {
                debugf("Eek!  Got get_peers with no info_hash.\n");
                send_error(source, sourcelen, tid, tid_len,
                           203, "Get_peers with no info_hash");
                break;
            } else {
                struct storage *st = find_storage(info_hash);
                unsigned char token[TOKEN_SIZE];
                make_token(source, 0, token);
                if(st && st->numpeers > 0) {
                     debugf("Sending found%s peers.\n",
                            source->sa_family == AF_INET6 ? " IPv6" : "");
                     send_closest_nodes(source, sourcelen,
                                        tid, tid_len,
                                        info_hash, want,
                                        source->sa_family, st,
                                        token, TOKEN_SIZE);
                } else {
                    debugf("Sending nodes for get_peers.\n");
                    send_closest_nodes(source, sourcelen,
                                       tid, tid_len, info_hash, want,
                                       0, NULL, token, TOKEN_SIZE);
                }
            }
            break;
        case ANNOUNCE_PEER:
            debugf("Announce peer!\n");
            new_node(id, source, sourcelen, 1);
            if(id_cmp(info_hash, zeroes) == 0) {
                debugf("Announce_peer with no info_hash.\n");
                send_error(source, sourcelen, tid, tid_len,
                           203, "Announce_peer with no info_hash");
                break;
            }
            if(!token_match(token, token_len, source)) {
                debugf("Incorrect token for announce_peer.\n");
                send_error(source, sourcelen, tid, tid_len,
                           203, "Announce_peer with wrong token");
                break;
            }
            if(port == 0) {
                debugf("Announce_peer with forbidden port %d.\n", port);
                send_error(source, sourcelen, tid, tid_len,
                           203, "Announce_peer with forbidden port number");
                break;
            }
            storage_store(info_hash, source);
            /* Note that if storage_store failed, we lie to the requestor.
               This is to prevent them from backtracking, and hence
               polluting the DHT. */
            debugf("Sending peer announced.\n");
            send_peer_announced(source, sourcelen, tid, tid_len);
        }
    }

 dontread:
    if(now.tv_sec >= rotate_secrets_time)
        rotate_secrets();

    if(now.tv_sec >= expire_stuff_time) {
        expire_buckets(buckets);
        expire_buckets(buckets6);
        expire_storage();
        expire_searches();
    }

    if(search_time > 0 && now.tv_sec >= search_time) {
        struct search *sr;
        sr = searches;
        while(sr) {
            if(!sr->done && sr->step_time + 5 <= now.tv_sec) {
                search_step(sr, callback, closure);
            }
            sr = sr->next;
        }

        search_time = 0;

        sr = searches;
        while(sr) {
            if(!sr->done) {
                time_t tm = sr->step_time + 15 + random() % 10;
                if(search_time == 0 || search_time > tm)
                    search_time = tm;
            }
            sr = sr->next;
        }
    }

    if(now.tv_sec >= confirm_nodes_time) {
        int soon = 0;

        soon |= bucket_maintenance(AF_INET);
        soon |= bucket_maintenance(AF_INET6);

        if(!soon) {
            if(mybucket_grow_time >= now.tv_sec - 150)
                soon |= neighbourhood_maintenance(AF_INET);
            if(mybucket6_grow_time >= now.tv_sec - 150)
                soon |= neighbourhood_maintenance(AF_INET6);
        }

        /* In order to maintain all buckets' age within 600 seconds, worst
           case is roughly 27 seconds, assuming the table is 22 bits deep.
           We want to keep a margin for neighborhood maintenance, so keep
           this within 25 seconds. */
        if(soon)
            confirm_nodes_time = now.tv_sec + 5 + random() % 20;
        else
            confirm_nodes_time = now.tv_sec + 60 + random() % 120;
    }

    if(confirm_nodes_time > now.tv_sec)
        *tosleep = confirm_nodes_time - now.tv_sec;
    else
        *tosleep = 0;

    if(search_time > 0) {
        if(search_time <= now.tv_sec)
            *tosleep = 0;
        else if(*tosleep > search_time - now.tv_sec)
            *tosleep = search_time - now.tv_sec;
    }

    return 1;
}

int
dht_get_nodes(struct sockaddr_in *sin, int *num,
              struct sockaddr_in6 *sin6, int *num6)
{
    int i, j;
    struct bucket *b;
    struct node *n;

    i = 0;

    /* For restoring to work without discarding too many nodes, the list
       must start with the contents of our bucket. */
    b = find_bucket(myid, AF_INET);
    if(b == NULL)
        goto no_ipv4;

    n = b->nodes;
    while(n && i < *num) {
        if(node_good(n)) {
            sin[i] = *(struct sockaddr_in*)&n->ss;
            i++;
        }
        n = n->next;
    }

    b = buckets;
    while(b && i < *num) {
        if(!in_bucket(myid, b)) {
            n = b->nodes;
            while(n && i < *num) {
                if(node_good(n)) {
                    sin[i] = *(struct sockaddr_in*)&n->ss;
                    i++;
                }
                n = n->next;
            }
        }
        b = b->next;
    }

 no_ipv4:

    j = 0;

    b = find_bucket(myid, AF_INET6);
    if(b == NULL)
        goto no_ipv6;

    n = b->nodes;
    while(n && j < *num6) {
        if(node_good(n)) {
            sin6[j] = *(struct sockaddr_in6*)&n->ss;
            j++;
        }
        n = n->next;
    }

    b = buckets6;
    while(b && j < *num6) {
        if(!in_bucket(myid, b)) {
            n = b->nodes;
            while(n && j < *num6) {
                if(node_good(n)) {
                    sin6[j] = *(struct sockaddr_in6*)&n->ss;
                    j++;
                }
                n = n->next;
            }
        }
        b = b->next;
    }

 no_ipv6:

    *num = i;
    *num6 = j;
    return i + j;
}

int
dht_insert_node(const unsigned char *id, struct sockaddr *sa, int salen)
{
    struct node *n;

    if(sa->sa_family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    n = new_node(id, (struct sockaddr*)sa, salen, 0);
    return !!n;
}

int
dht_ping_node(struct sockaddr *sa, int salen)
{
    unsigned char tid[4];

    debugf("Sending ping.\n");
    make_tid(tid, "pn", 0);
    return send_ping(sa, salen, tid, 4);
}

/* We could use a proper bencoding printer and parser, but the format of
   DHT messages is fairly stylised, so this seemed simpler. */

#define CHECK(offset, delta, size)                      \
    if(delta < 0 || offset + delta > size) goto fail

#define INC(offset, delta, size)                        \
    CHECK(offset, delta, size);                         \
    offset += delta

#define COPY(buf, offset, src, delta, size)             \
    CHECK(offset, delta, size);                         \
    memcpy(buf + offset, src, delta);                   \
    offset += delta;

#define ADD_V(buf, offset, size)                        \
    if(have_v) {                                        \
        COPY(buf, offset, my_v, sizeof(my_v), size);    \
    }

static int
dht_send(const void *buf, size_t len, int flags,
         const struct sockaddr *sa, int salen)
{
    int s;

    if(salen == 0)
        abort();

    if(sa->sa_family == AF_INET)
        s = dht_socket;
    else if(sa->sa_family == AF_INET6)
        s = dht_socket6;
    else
        s = -1;

    if(s < 0) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    return sendto(s, buf, len, flags, sa, salen);
}

int
send_ping(struct sockaddr *sa, int salen,
          const unsigned char *tid, int tid_len)
{
    char buf[512];
    int i = 0, rc;
    rc = snprintf(buf + i, 512 - i, "d1:ad2:id20:"); INC(i, rc, 512);
    COPY(buf, i, myid, 20, 512);
    rc = snprintf(buf + i, 512 - i, "e1:q4:ping1:t%d:", tid_len);
    INC(i, rc, 512);
    COPY(buf, i, tid, tid_len, 512);
    ADD_V(buf, i, 512);
    rc = snprintf(buf + i, 512 - i, "1:y1:qe"); INC(i, rc, 512);
    return dht_send(buf, i, 0, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

int
send_pong(struct sockaddr *sa, int salen,
          const unsigned char *tid, int tid_len)
{
    char buf[512];
    int i = 0, rc;
    rc = snprintf(buf + i, 512 - i, "d1:rd2:id20:"); INC(i, rc, 512);
    COPY(buf, i, myid, 20, 512);
    rc = snprintf(buf + i, 512 - i, "e1:t%d:", tid_len); INC(i, rc, 512);
    COPY(buf, i, tid, tid_len, 512);
    ADD_V(buf, i, 512);
    rc = snprintf(buf + i, 512 - i, "1:y1:re"); INC(i, rc, 512);
    return dht_send(buf, i, 0, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

int
send_find_node(struct sockaddr *sa, int salen,
               const unsigned char *tid, int tid_len,
               const unsigned char *target, int want, int confirm)
{
    char buf[512];
    int i = 0, rc;
    rc = snprintf(buf + i, 512 - i, "d1:ad2:id20:"); INC(i, rc, 512);
    COPY(buf, i, myid, 20, 512);
    rc = snprintf(buf + i, 512 - i, "6:target20:"); INC(i, rc, 512);
    COPY(buf, i, target, 20, 512);
    if(want > 0) {
        rc = snprintf(buf + i, 512 - i, "4:wantl%s%se",
                      (want & WANT4) ? "2:n4" : "",
                      (want & WANT6) ? "2:n6" : "");
        INC(i, rc, 512);
    }
    rc = snprintf(buf + i, 512 - i, "e1:q9:find_node1:t%d:", tid_len);
    INC(i, rc, 512);
    COPY(buf, i, tid, tid_len, 512);
    ADD_V(buf, i, 512);
    rc = snprintf(buf + i, 512 - i, "1:y1:qe"); INC(i, rc, 512);
    return dht_send(buf, i, confirm ? MSG_CONFIRM : 0, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

int
send_nodes_peers(struct sockaddr *sa, int salen,
                 const unsigned char *tid, int tid_len,
                 const unsigned char *nodes, int nodes_len,
                 const unsigned char *nodes6, int nodes6_len,
                 int af, struct storage *st,
                 const unsigned char *token, int token_len)
{
    char buf[2048];
    int i = 0, rc, j0, j, k, len;

    rc = snprintf(buf + i, 2048 - i, "d1:rd2:id20:"); INC(i, rc, 2048);
    COPY(buf, i, myid, 20, 2048);
    if(token_len > 0) {
        rc = snprintf(buf + i, 2048 - i, "5:token%d:", token_len);
        INC(i, rc, 2048);
        COPY(buf, i, token, token_len, 2048);
    }
    if(nodes_len > 0) {
        rc = snprintf(buf + i, 2048 - i, "5:nodes%d:", nodes_len);
        INC(i, rc, 2048);
        COPY(buf, i, nodes, nodes_len, 2048);
    }
    if(nodes6_len > 0) {
         rc = snprintf(buf + i, 2048 - i, "6:nodes6%d:", nodes6_len);
         INC(i, rc, 2048);
         COPY(buf, i, nodes6, nodes6_len, 2048);
    }

    if(st && st->numpeers > 0) {
        /* We treat the storage as a circular list, and serve a randomly
           chosen slice.  In order to make sure we fit within 1024 octets,
           we limit ourselves to 50 peers. */

        len = af == AF_INET ? 4 : 16;
        j0 = random() % st->numpeers;
        j = j0;
        k = 0;

        rc = snprintf(buf + i, 2048 - i, "6:valuesl"); INC(i, rc, 2048);
        do {
            if(st->peers[j].len == len) {
                unsigned short swapped;
                swapped = htons(st->peers[j].port);
                rc = snprintf(buf + i, 2048 - i, "%d:", len + 2);
                INC(i, rc, 2048);
                COPY(buf, i, st->peers[j].ip, len, 2048);
                COPY(buf, i, &swapped, 2, 2048);
                k++;
            }
            j = (j + 1) % st->numpeers;
        } while(j != j0 && k < 50);
        rc = snprintf(buf + i, 2048 - i, "e"); INC(i, rc, 2048);
    }

    rc = snprintf(buf + i, 2048 - i, "e1:t%d:", tid_len); INC(i, rc, 2048);
    COPY(buf, i, tid, tid_len, 2048);
    ADD_V(buf, i, 2048);
    rc = snprintf(buf + i, 2048 - i, "1:y1:re"); INC(i, rc, 2048);

    return dht_send(buf, i, 0, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

static int
insert_closest_node(unsigned char *nodes, int numnodes,
                    const unsigned char *id, struct node *n)
{
    int i, size;

    if(n->ss.ss_family == AF_INET)
        size = 26;
    else if(n->ss.ss_family == AF_INET6)
        size = 38;
    else
        abort();

    for(i = 0; i< numnodes; i++) {
        if(id_cmp(n->id, nodes + size * i) == 0)
            return numnodes;
        if(xorcmp(n->id, nodes + size * i, id) < 0)
            break;
    }

    if(i == 8)
        return numnodes;

    if(numnodes < 8)
        numnodes++;

    if(i < numnodes - 1)
        memmove(nodes + size * (i + 1), nodes + size * i,
                size * (numnodes - i - 1));

    if(n->ss.ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in*)&n->ss;
        memcpy(nodes + size * i, n->id, 20);
        memcpy(nodes + size * i + 20, &sin->sin_addr, 4);
        memcpy(nodes + size * i + 24, &sin->sin_port, 2);
    } else if(n->ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)&n->ss;
        memcpy(nodes + size * i, n->id, 20);
        memcpy(nodes + size * i + 20, &sin6->sin6_addr, 16);
        memcpy(nodes + size * i + 36, &sin6->sin6_port, 2);
    } else {
        abort();
    }

    return numnodes;
}

static int
buffer_closest_nodes(unsigned char *nodes, int numnodes,
                     const unsigned char *id, struct bucket *b)
{
    struct node *n = b->nodes;
    while(n) {
        if(node_good(n))
            numnodes = insert_closest_node(nodes, numnodes, id, n);
        n = n->next;
    }
    return numnodes;
}

int
send_closest_nodes(struct sockaddr *sa, int salen,
                   const unsigned char *tid, int tid_len,
                   const unsigned char *id, int want,
                   int af, struct storage *st,
                   const unsigned char *token, int token_len)
{
    unsigned char nodes[8 * 26];
    unsigned char nodes6[8 * 38];
    int numnodes = 0, numnodes6 = 0;
    struct bucket *b;

    if(want < 0)
        want = sa->sa_family == AF_INET ? WANT4 : WANT6;

    if((want & WANT4)) {
        b = find_bucket(id, AF_INET);
        if(b) {
            numnodes = buffer_closest_nodes(nodes, numnodes, id, b);
            if(b->next)
                numnodes = buffer_closest_nodes(nodes, numnodes, id, b->next);
            b = previous_bucket(b);
            if(b)
                numnodes = buffer_closest_nodes(nodes, numnodes, id, b);
        }
    }

    if((want & WANT6)) {
        b = find_bucket(id, AF_INET6);
        if(b) {
            numnodes6 = buffer_closest_nodes(nodes6, numnodes6, id, b);
            if(b->next)
                numnodes6 =
                    buffer_closest_nodes(nodes6, numnodes6, id, b->next);
            b = previous_bucket(b);
            if(b)
                numnodes6 = buffer_closest_nodes(nodes6, numnodes6, id, b);
        }
    }
    debugf("  (%d+%d nodes.)\n", numnodes, numnodes6);

    return send_nodes_peers(sa, salen, tid, tid_len,
                            nodes, numnodes * 26,
                            nodes6, numnodes6 * 38,
                            af, st, token, token_len);
}

int
send_get_peers(struct sockaddr *sa, int salen,
               unsigned char *tid, int tid_len, unsigned char *infohash,
               int want, int confirm)
{
    char buf[512];
    int i = 0, rc;

    rc = snprintf(buf + i, 512 - i, "d1:ad2:id20:"); INC(i, rc, 512);
    COPY(buf, i, myid, 20, 512);
    rc = snprintf(buf + i, 512 - i, "9:info_hash20:"); INC(i, rc, 512);
    COPY(buf, i, infohash, 20, 512);
    if(want > 0) {
        rc = snprintf(buf + i, 512 - i, "4:wantl%s%se",
                      (want & WANT4) ? "2:n4" : "",
                      (want & WANT6) ? "2:n6" : "");
        INC(i, rc, 512);
    }
    rc = snprintf(buf + i, 512 - i, "e1:q9:get_peers1:t%d:", tid_len);
    INC(i, rc, 512);
    COPY(buf, i, tid, tid_len, 512);
    ADD_V(buf, i, 512);
    rc = snprintf(buf + i, 512 - i, "1:y1:qe"); INC(i, rc, 512);
    return dht_send(buf, i, confirm ? MSG_CONFIRM : 0, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

int
send_announce_peer(struct sockaddr *sa, int salen,
                   unsigned char *tid, int tid_len,
                   unsigned char *infohash, unsigned short port,
                   unsigned char *token, int token_len, int confirm)
{
    char buf[512];
    int i = 0, rc;

    rc = snprintf(buf + i, 512 - i, "d1:ad2:id20:"); INC(i, rc, 512);
    COPY(buf, i, myid, 20, 512);
    rc = snprintf(buf + i, 512 - i, "9:info_hash20:"); INC(i, rc, 512);
    COPY(buf, i, infohash, 20, 512);
    rc = snprintf(buf + i, 512 - i, "4:porti%ue5:token%d:", (unsigned)port,
                  token_len);
    INC(i, rc, 512);
    COPY(buf, i, token, token_len, 512);
    rc = snprintf(buf + i, 512 - i, "e1:q13:announce_peer1:t%d:", tid_len);
    INC(i, rc, 512);
    COPY(buf, i, tid, tid_len, 512);
    ADD_V(buf, i, 512);
    rc = snprintf(buf + i, 512 - i, "1:y1:qe"); INC(i, rc, 512);

    return dht_send(buf, i, confirm ? 0 : MSG_CONFIRM, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

static int
send_peer_announced(struct sockaddr *sa, int salen,
                    unsigned char *tid, int tid_len)
{
    char buf[512];
    int i = 0, rc;

    rc = snprintf(buf + i, 512 - i, "d1:rd2:id20:"); INC(i, rc, 512);
    COPY(buf, i, myid, 20, 512);
    rc = snprintf(buf + i, 512 - i, "e1:t%d:", tid_len);
    INC(i, rc, 512);
    COPY(buf, i, tid, tid_len, 512);
    ADD_V(buf, i, 512);
    rc = snprintf(buf + i, 512 - i, "1:y1:re"); INC(i, rc, 512);
    return dht_send(buf, i, 0, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

static int
send_error(struct sockaddr *sa, int salen,
           unsigned char *tid, int tid_len,
           int code, const char *message)
{
    char buf[512];
    int i = 0, rc;

    rc = snprintf(buf + i, 512 - i, "d1:eli%de%d:",
                  code, (int)strlen(message));
    INC(i, rc, 512);
    COPY(buf, i, message, (int)strlen(message), 512);
    rc = snprintf(buf + i, 512 - i, "e1:t%d:", tid_len); INC(i, rc, 512);
    COPY(buf, i, tid, tid_len, 512);
    ADD_V(buf, i, 512);
    rc = snprintf(buf + i, 512 - i, "1:y1:ee"); INC(i, rc, 512);
    return dht_send(buf, i, 0, sa, salen);

 fail:
    errno = ENOSPC;
    return -1;
}

#undef CHECK
#undef INC
#undef COPY
#undef ADD_V

#ifndef HAVE_MEMMEM
static void *
memmem(const void *haystack, size_t haystacklen,
       const void *needle, size_t needlelen)
{
    const char *h = haystack;
    const char *n = needle;
    size_t i;

    /* size_t is unsigned */
    if(needlelen > haystacklen)
        return NULL;

    for(i = 0; i <= haystacklen - needlelen; i++) {
        if(memcmp(h + i, n, needlelen) == 0)
            return (void*)(h + i);
    }
    return NULL;
}
#endif

static int
parse_message(const unsigned char *buf, int buflen,
              unsigned char *tid_return, int *tid_len,
              unsigned char *id_return, unsigned char *info_hash_return,
              unsigned char *target_return, unsigned short *port_return,
              unsigned char *token_return, int *token_len,
              unsigned char *nodes_return, int *nodes_len,
              unsigned char *nodes6_return, int *nodes6_len,
              unsigned char *values_return, int *values_len,
              unsigned char *values6_return, int *values6_len,
              int *want_return)
{
    const unsigned char *p;

    /* This code will happily crash if the buffer is not NUL-terminated. */
    if(buf[buflen] != '\0') {
        debugf("Eek!  parse_message with unterminated buffer.\n");
        return -1;
    }

#define CHECK(ptr, len)                                                 \
    if(((unsigned char*)ptr) + (len) > (buf) + (buflen)) goto overflow;

    if(tid_return) {
        p = memmem(buf, buflen, "1:t", 3);
        if(p) {
            long l;
            char *q;
            l = strtol((char*)p + 3, &q, 10);
            if(q && *q == ':' && l > 0 && l < *tid_len) {
                CHECK(q + 1, l);
                memcpy(tid_return, q + 1, l);
                *tid_len = l;
            } else
                *tid_len = 0;
        }
    }
    if(id_return) {
        p = memmem(buf, buflen, "2:id20:", 7);
        if(p) {
            CHECK(p + 7, 20);
            memcpy(id_return, p + 7, 20);
        } else {
            memset(id_return, 0, 20);
        }
    }
    if(info_hash_return) {
        p = memmem(buf, buflen, "9:info_hash20:", 14);
        if(p) {
            CHECK(p + 14, 20);
            memcpy(info_hash_return, p + 14, 20);
        } else {
            memset(info_hash_return, 0, 20);
        }
    }
    if(port_return) {
        p = memmem(buf, buflen, "porti", 5);
        if(p) {
            long l;
            char *q;
            l = strtol((char*)p + 5, &q, 10);
            if(q && *q == 'e' && l > 0 && l < 0x10000)
                *port_return = l;
            else
                *port_return = 0;
        } else
            *port_return = 0;
    }
    if(target_return) {
        p = memmem(buf, buflen, "6:target20:", 11);
        if(p) {
            CHECK(p + 11, 20);
            memcpy(target_return, p + 11, 20);
        } else {
            memset(target_return, 0, 20);
        }
    }
    if(token_return) {
        p = memmem(buf, buflen, "5:token", 7);
        if(p) {
            long l;
            char *q;
            l = strtol((char*)p + 7, &q, 10);
            if(q && *q == ':' && l > 0 && l < *token_len) {
                CHECK(q + 1, l);
                memcpy(token_return, q + 1, l);
                *token_len = l;
            } else
                *token_len = 0;
        } else
            *token_len = 0;
    }

    if(nodes_len) {
        p = memmem(buf, buflen, "5:nodes", 7);
        if(p) {
            long l;
            char *q;
            l = strtol((char*)p + 7, &q, 10);
            if(q && *q == ':' && l > 0 && l < *nodes_len) {
                CHECK(q + 1, l);
                memcpy(nodes_return, q + 1, l);
                *nodes_len = l;
            } else
                *nodes_len = 0;
        } else
            *nodes_len = 0;
    }

    if(nodes6_len) {
        p = memmem(buf, buflen, "6:nodes6", 8);
        if(p) {
            long l;
            char *q;
            l = strtol((char*)p + 8, &q, 10);
            if(q && *q == ':' && l > 0 && l < *nodes6_len) {
                CHECK(q + 1, l);
                memcpy(nodes6_return, q + 1, l);
                *nodes6_len = l;
            } else
                *nodes6_len = 0;
        } else
            *nodes6_len = 0;
    }

    if(values_len || values6_len) {
        p = memmem(buf, buflen, "6:valuesl", 9);
        if(p) {
            int i = p - buf + 9;
            int j = 0, j6 = 0;
            while(1) {
                long l;
                char *q;
                l = strtol((char*)buf + i, &q, 10);
                if(q && *q == ':' && l > 0) {
                    CHECK(q + 1, l);
                    if(l == 6) {
                        if(j + l > *values_len)
                            continue;
                        i = q + 1 + l - (char*)buf;
                        memcpy((char*)values_return + j, q + 1, l);
                        j += l;
                    } else if(l == 18) {
                        if(j6 + l > *values6_len)
                            continue;
                        i = q + 1 + l - (char*)buf;
                        memcpy((char*)values6_return + j6, q + 1, l);
                        j6 += l;
                    } else {
                        debugf("Received weird value -- %d bytes.\n", (int)l);
                        i = q + 1 + l - (char*)buf;
                    }
                } else {
                    break;
                }
            }
            if(i >= buflen || buf[i] != 'e')
                debugf("eek... unexpected end for values.\n");
            if(values_len)
                *values_len = j;
            if(values6_len)
                *values6_len = j6;
        } else {
            *values_len = 0;
            *values6_len = 0;
        }
    }

    if(want_return) {
        p = memmem(buf, buflen, "4:wantl", 7);
        if(p) {
            int i = p - buf + 7;
            *want_return = 0;
            while(buf[i] > '0' && buf[i] <= '9' && buf[i + 1] == ':' &&
                  i + 2 + buf[i] - '0' < buflen) {
                CHECK(buf + i + 2, buf[i] - '0');
                if(buf[i] == '2' && memcmp(buf + i + 2, "n4", 2) == 0)
                    *want_return |= WANT4;
                else if(buf[i] == '2' && memcmp(buf + i + 2, "n6", 2) == 0)
                    *want_return |= WANT6;
                else
                    debugf("eek... unexpected want flag (%c)\n", buf[i]);
                i += 2 + buf[i] - '0';
            }
            if(i >= buflen || buf[i] != 'e')
                debugf("eek... unexpected end for want.\n");
        } else {
            *want_return = -1;
        }
    }

#undef CHECK

    if(memmem(buf, buflen, "1:y1:r", 6))
        return REPLY;
    if(memmem(buf, buflen, "1:y1:e", 6))
        return ERROR;
    if(!memmem(buf, buflen, "1:y1:q", 6))
        return -1;
    if(memmem(buf, buflen, "1:q4:ping", 9))
        return PING;
    if(memmem(buf, buflen, "1:q9:find_node", 14))
       return FIND_NODE;
    if(memmem(buf, buflen, "1:q9:get_peers", 14))
        return GET_PEERS;
    if(memmem(buf, buflen, "1:q13:announce_peer", 19))
       return ANNOUNCE_PEER;
    return -1;

 overflow:
    debugf("Truncated message.\n");
    return -1;
}
