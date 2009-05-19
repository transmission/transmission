/* This example code was written by Juliusz Chroboczek.
   You are free to cut'n'paste from it to your heart's content. */

/* For crypt */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/signal.h>

#include "dht.h"

#define MAX_BOOTSTRAP_NODES 20
static struct sockaddr_in bootstrap_nodes[MAX_BOOTSTRAP_NODES];
static int num_bootstrap_nodes = 0;

static volatile sig_atomic_t dumping = 0, searching = 0, exiting = 0;

static void
sigdump(int signo)
{
    dumping = 1;
}

static void
sigtest(int signo)
{
    searching = 1;
}

static void
sigexit(int signo)
{
    exiting = 1;
}

static void
init_signals(void)
{
    struct sigaction sa;
    sigset_t ss;

    sigemptyset(&ss);
    sa.sa_handler = sigdump;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    sigemptyset(&ss);
    sa.sa_handler = sigtest;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, NULL);

    sigemptyset(&ss);
    sa.sa_handler = sigexit;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

const unsigned char hash[20] = {
    0x54, 0x57, 0x87, 0x89, 0xdf, 0xc4, 0x23, 0xee, 0xf6, 0x03,
    0x1f, 0x81, 0x94, 0xa9, 0x3a, 0x16, 0x98, 0x8b, 0x72, 0x7b
};

/* The call-back function is called by the DHT whenever something
   interesting happens.  Right now, it only happens when we get a new value or
   when a search completes, but this may be extended in future versions. */
static void
callback(void *closure,
         int event,
         unsigned char *info_hash,
         void *data, size_t data_len)
{
    if(event == DHT_EVENT_SEARCH_DONE)
        printf("Search done.\n");
    else if(event == DHT_EVENT_VALUES)
        printf("Received %d values.\n", (int)(data_len / 6));
}

int
main(int argc, char **argv)
{
    int i, rc, fd;
    int s, port;
    int have_id = 0;
    unsigned char myid[20];
    time_t tosleep = 0;

    /* Ids need to be distributed evenly, so you cannot just use your
       bittorrent id.  Either generate it randomly, or take the SHA-1 of
       something. */
    fd = open("dht-example.id", O_RDONLY);
    if(fd >= 0) {
        rc = read(fd, myid, 20);
        if(rc == 20)
            have_id = 1;
        close(fd);
    }
    
    if(!have_id) {
        fd = open("/dev/urandom", O_RDONLY);
        if(fd < 0) {
            perror("open(random)");
            exit(1);
        }
        rc = read(fd, myid, 20);
        if(rc < 0) {
            perror("read(random)");
            exit(1);
        }
        have_id = 1;
        close(fd);

        fd = open("dht-example.id", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if(fd >= 0) {
            rc = write(fd, myid, 20);
            if(rc < 20)
                unlink("dht-example.id");
            close(fd);
        }
    }

    if(argc < 2)
        goto usage;

    i = 1;

    if(argc < i + 1)
        goto usage;

    port = atoi(argv[i++]);
    if(port <= 0 || port >= 0x10000)
        goto usage;

    while(i < argc) {
        struct addrinfo hints, *info, *infop;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        rc = getaddrinfo(argv[i], NULL, &hints, &info);
        if(rc != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
            exit(1);
        }

        i++;

        infop = info;
        while(infop) {
            if(infop->ai_addr->sa_family == AF_INET) {
                struct sockaddr_in sin;
                memcpy(&sin, infop->ai_addr, infop->ai_addrlen);
                sin.sin_port = htons(atoi(argv[i]));
                bootstrap_nodes[num_bootstrap_nodes] = sin;
                num_bootstrap_nodes++;
            }
            infop = infop->ai_next;
        }
        freeaddrinfo(info);

        i++;
    }

    if(i < argc)
        goto usage;

    /* If you set dht_debug to a stream, every action taken by the DHT will
       be logged. */
    dht_debug = stdout;

    /* We need an IPv4 socket, bound to a stable port.  Rumour has it that
       uTorrent works better when it is the same as your Bittorrent port. */
    s = socket(PF_INET, SOCK_DGRAM, 0);
    if(s < 0) {
        perror("socket");
        exit(1);
    }

    {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        rc = bind(s, (struct sockaddr*)&sin, sizeof(sin));
        if(rc < 0) {
            perror("bind");
            exit(1);
        }
    }

    /* Init the dht.  This sets the socket into non-blocking mode. */
    rc = dht_init(s, myid);
    if(rc < 0) {
        perror("dht_init");
        exit(1);
    }

    init_signals();

    /* For bootstrapping, we need an initial list of nodes.  This could be
       hard-wired, but can also be obtained from the nodes key of a torrent
       file, or from the PORT bittorrent message.

       Dht_ping_node is the brutal way of bootstrapping -- it actually
       sends a message to the peer.  If you're going to bootstrap from
       a massive number of nodes (for example because you're restoring from
       a dump) and you already know their ids, it's better to use
       dht_insert_node.  If the ids are incorrect, the DHT will recover. */
    for(i = 0; i < num_bootstrap_nodes; i++) {
        dht_ping_node(s, &bootstrap_nodes[i]);
        usleep(random() % 100000);
    }

    while(1) {
        struct timeval tv;
        fd_set readfds;
        tv.tv_sec = tosleep;
        tv.tv_usec = random() % 1000000;

        FD_ZERO(&readfds);
        FD_SET(s, &readfds);
        rc = select(s + 1, &readfds, NULL, NULL, &tv);
        if(rc < 0) {
            if(errno != EINTR) {
                perror("select");
                sleep(1);
            }
        }
        
        if(exiting)
            break;

        rc = dht_periodic(s, rc > 0, &tosleep, callback, NULL);
        if(rc < 0) {
            if(errno == EINTR) {
                continue;
            } else {
                perror("dht_periodic");
                if(rc == EINVAL || rc == EFAULT)
                    abort();
                tosleep = 1;
            }
        }

        /* This is how you trigger a search for a torrent hash.  If port
           (the third argument) is non-zero, it also performs an announce.
           Since peers expire announced data after 30 minutes, it's a good
           idea to reannounce every 28 minutes or so. */
        if(searching) {
            dht_search(s, hash, 0, callback, NULL);
            searching = 0;
        }

        /* For debugging, or idle curiosity. */
        if(dumping) {
            dht_dump_tables(stdout);
            dumping = 0;
        }
    }

    {
        struct sockaddr_in sins[500];
        int i;
        i = dht_get_nodes(sins, 500);
        printf("Found %d good nodes.\n", i);
    }

    dht_uninit(s, 1);
    return 0;
    
 usage:
    fprintf(stderr, "Foo!\n");
    exit(1);
}

/* We need to provide a reasonably strong cryptographic hashing function.
   Here's how we'd do it if we had RSA's MD5 code. */
#if 0
void
dht_hash(void *hash_return, int hash_size,
         const void *v1, int len1,
         const void *v2, int len2,
         const void *v3, int len3)
{
    static MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, v1, len1);
    MD5Update(&ctx, v2, len2);
    MD5Update(&ctx, v3, len3);
    MD5Final(&ctx);
    if(hash_size > 16)
        memset((char*)hash_return + 16, 0, hash_size - 16);
    memcpy(hash_return, ctx.digest, hash_size > 16 ? 16 : hash_size);
}
#else
/* But for this example, we might as well use something weaker. */
void
dht_hash(void *hash_return, int hash_size,
         const void *v1, int len1,
         const void *v2, int len2,
         const void *v3, int len3)
{
    const char *c1 = v1, *c2 = v2, *c3 = v3;
    char key[9];                /* crypt is limited to 8 characters */
    int i;

    memset(key, 0, 9);
#define CRYPT_HAPPY(c) ((c % 0x60) + 0x20)

    for(i = 0; i < 2 && i < len1; i++)
        key[i] = CRYPT_HAPPY(c1[i]);
    for(i = 0; i < 4 && i < len1; i++)
        key[2 + i] = CRYPT_HAPPY(c2[i]);
    for(i = 0; i < 2 && i < len1; i++)
        key[6 + i] = CRYPT_HAPPY(c3[i]);
    strncpy(hash_return, crypt(key, "jc"), hash_size);
}
#endif
