AM_CPPFLAGS = \
    -I. \
    -I$(top_srcdir) \
    -I$(top_srcdir)/third-party/ \
    -D__TRANSMISSION__ \
    -DPACKAGE_DATA_DIR=\""$(datadir)"\"

AM_CFLAGS = \
    @DHT_CFLAGS@ \
    @LIBUTP_CFLAGS@ \
    @LIBEVENT_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @OPENSSL_CFLAGS@ \
    @PTHREAD_CFLAGS@ \
    @ZLIB_CFLAGS@

noinst_LIBRARIES = libtransmission.a

libtransmission_a_SOURCES = \
    announcer.c \
    announcer-http.c \
    announcer-udp.c \
    bandwidth.c \
    bencode.c \
    bitfield.c \
    blocklist.c \
    cache.c \
    clients.c \
    completion.c \
    ConvertUTF.c \
    crypto.c \
    fdlimit.c \
    handshake.c \
    history.c \
    inout.c \
    json.c \
    JSON_parser.c \
    list.c \
    magnet.c \
    makemeta.c \
    metainfo.c \
    natpmp.c \
    net.c \
    peer-io.c \
    peer-mgr.c \
    peer-msgs.c \
    platform.c \
    port-forwarding.c \
    ptrarray.c \
    resume.c \
    rpcimpl.c \
    rpc-server.c \
    session.c \
    stats.c \
    torrent.c \
    torrent-ctor.c \
    torrent-magnet.c \
    tr-dht.c \
    tr-lpd.c \
    tr-udp.c \
    tr-utp.c \
    tr-getopt.c \
    trevent.c \
    upnp.c \
    utils.c \
    verify.c \
    web.c \
    webseed.c \
    wildmat.c

noinst_HEADERS = \
    announcer.h \
    announcer-common.h \
    bandwidth.h \
    bencode.h \
    bitfield.h \
    blocklist.h \
    cache.h \
    clients.h \
    ConvertUTF.h \
    crypto.h \
    completion.h \
    fdlimit.h \
    handshake.h \
    history.h \
    inout.h \
    json.h \
    JSON_parser.h \
    list.h \
    magnet.h \
    makemeta.h \
    metainfo.h \
    natpmp.h \
    net.h \
    peer-common.h \
    peer-io.h \
    peer-mgr.h \
    peer-msgs.h \
    platform.h \
    port-forwarding.h \
    ptrarray.h \
    resume.h \
    rpcimpl.h \
    rpc-server.h \
    session.h \
    stats.h \
    torrent.h \
    torrent-magnet.h \
    tr-getopt.h \
    transmission.h \
    tr-dht.h \
    tr-udp.h \
    tr-utp.h \
    tr-lpd.h \
    trevent.h \
    upnp.h \
    utils.h \
    verify.h \
    version.h \
    web.h \
    webseed.h

TESTS = \
    blocklist-test \
    bencode-test \
    clients-test \
    history-test \
    json-test \
    magnet-test \
    peer-msgs-test \
    rpc-test \
    test-peer-id \
    utils-test

noinst_PROGRAMS = $(TESTS)

apps_ldflags = \
    @ZLIB_LDFLAGS@

apps_ldadd = \
    ./libtransmission.a  \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    @INTLLIBS@ \
    @DHT_LIBS@ \
    @LIBUTP_LIBS@ \
    @LIBCURL_LIBS@ \
    @LIBEVENT_LIBS@ \
    @OPENSSL_LIBS@ \
    @PTHREAD_LIBS@ \
    @ZLIB_LIBS@

bencode_test_SOURCES = bencode-test.c
bencode_test_LDADD = ${apps_ldadd}
bencode_test_LDFLAGS = ${apps_ldflags}

blocklist_test_SOURCES = blocklist-test.c
blocklist_test_LDADD = ${apps_ldadd}
blocklist_test_LDFLAGS = ${apps_ldflags}

clients_test_SOURCES = clients-test.c
clients_test_LDADD = ${apps_ldadd}
clients_test_LDFLAGS = ${apps_ldflags}

history_test_SOURCES = history-test.c
history_test_LDADD = ${apps_ldadd}
history_test_LDFLAGS = ${apps_ldflags}

json_test_SOURCES = json-test.c
json_test_LDADD = ${apps_ldadd}
json_test_LDFLAGS = ${apps_ldflags}

magnet_test_SOURCES = magnet-test.c
magnet_test_LDADD = ${apps_ldadd}
magnet_test_LDFLAGS = ${apps_ldflags}

rpc_test_SOURCES = rpc-test.c
rpc_test_LDADD = ${apps_ldadd}
rpc_test_LDFLAGS = ${apps_ldflags}

test_peer_id_SOURCES = test-peer-id.c
test_peer_id_LDADD = ${apps_ldadd}
test_peer_id_LDFLAGS = ${apps_ldflags}

peer_msgs_test_SOURCES = peer-msgs-test.c
peer_msgs_test_LDADD = ${apps_ldadd}
peer_msgs_test_LDFLAGS = ${apps_ldflags}

utils_test_SOURCES = utils-test.c
utils_test_LDADD = ${apps_ldadd}
utils_test_LDFLAGS = ${apps_ldflags}
