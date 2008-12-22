AM_CPPFLAGS = \
	-I. -I$(top_srcdir) -I$(top_srcdir)/third-party/ \
	-D__TRANSMISSION__ $(LIBEVENT_CPPFLAGS) \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"

AM_CFLAGS = \
    $(LIBCURL_CFLAGS) \
    $(OPENSSL_CFLAGS) \
    $(PTHREAD_CFLAGS) \
    $(ZLIB_CFLAGS)

noinst_LIBRARIES = libtransmission.a

libtransmission_a_SOURCES = \
    bandwidth.c \
    bencode.c \
    blocklist.c \
    clients.c \
    completion.c \
    ConvertUTF.c \
    crypto.c \
    fastresume.c \
    fdlimit.c \
    ggets.c \
    handshake.c \
    inout.c \
    json.c \
    JSON_parser.c \
    list.c \
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
    publish.c \
    ratecontrol.c \
    resume.c \
    rpcimpl.c \
    rpc-server.c \
    session.c \
    stats.c \
    torrent.c \
    torrent-ctor.c \
    tr-getopt.c \
    tracker.c \
    trevent.c \
    upnp.c \
    utils.c \
    verify.c \
    web.c \
    webseed.c \
    wildmat.c

noinst_HEADERS = \
    bandwidth.h \
    bencode.h \
    blocklist.h \
    clients.h \
    ConvertUTF.h \
    crypto.h \
    completion.h \
    fastresume.h \
    fdlimit.h \
    ggets.h \
    handshake.h \
    inout.h \
    json.h \
    JSON_parser.h \
    list.h \
    makemeta.h \
    metainfo.h \
    natpmp.h \
    net.h \
    peer-common.h \
    peer-io.h \
    peer-mgr.h \
    peer-mgr-private.h \
    peer-msgs.h \
    platform.h \
    port-forwarding.h \
    ptrarray.h \
    publish.h \
    ratecontrol.h \
    resume.h \
    rpcimpl.h \
    rpc-server.h \
    session.h \
    stats.h \
    torrent.h \
    tracker.h \
    tr-getopt.h \
    transmission.h \
    trevent.h \
    upnp.h \
    utils.h \
    verify.h \
    web.h \
    webseed.h

TESTS = \
    blocklist-test \
    bencode-test \
    clients-test \
    json-test \
    peer-msgs-test \
    rpc-test \
    test-peer-id \
    utils-test

noinst_PROGRAMS = $(TESTS)

apps_ldflags = \
    $(ZLIB_LDFLAGS)

apps_ldadd = \
    ./libtransmission.a  \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(INTLLIBS) \
    $(LIBCURL_LIBS) \
    $(OPENSSL_LIBS) \
    $(PTHREAD_LIBS) \
    $(ZLIB_LIBS) \
    -lm

bencode_test_SOURCES = bencode-test.c
bencode_test_LDADD = ${apps_ldadd}
bencode_test_LDFLAGS = ${apps_ldflags}

blocklist_test_SOURCES = blocklist-test.c
blocklist_test_LDADD = ${apps_ldadd}
blocklist_test_LDFLAGS = ${apps_ldflags}

clients_test_SOURCES = clients-test.c
clients_test_LDADD = ${apps_ldadd}
clients_test_LDFLAGS = ${apps_ldflags}

json_test_SOURCES = json-test.c
json_test_LDADD = ${apps_ldadd}
json_test_LDFLAGS = ${apps_ldflags}

rpc_test_SOURCES = rpc-test.c
rpc_test_LDADD = ${apps_ldadd}
rpc_test_LDFLAGS = ${apps_ldflags}

peer_msgs_test_SOURCES = peer-msgs-test.c
peer_msgs_test_LDADD = ${apps_ldadd}
peer_msgs_test_LDFLAGS = ${apps_ldflags}

test_peer_id_SOURCES = test-peer-id.c
test_peer_id_LDADD = ${apps_ldadd}
test_peer_id_LDFLAGS = ${apps_ldflags}

utils_test_SOURCES = utils-test.c
utils_test_LDADD = ${apps_ldadd}
utils_test_LDFLAGS = ${apps_ldflags}



clean-local:
	if test -d .svn; then if test -e version.h; then rm version.h; fi; fi;

EXTRA_libtransmission_a_SOURCES = \
    version.h

BUILT_SOURCES = \
    version.h

version.h:
	echo '#define PEERID_PREFIX         "'@PEERID_PREFIX@'"' > version.h
	echo '#define USERAGENT_PREFIX      "'@USERAGENT_PREFIX@'"' >> version.h
	echo '#define SVN_REVISION          "'`svn info | grep "Revision" | awk -F': ' '{print $$2}'`'"' >> version.h
	echo '#define SVN_REVISION_NUM      '`svn info | grep "Revision" | awk -F': ' '{print $$2}'`'' >> version.h
	echo '#define SHORT_VERSION_STRING  "'@USERAGENT_PREFIX@'"' >> version.h
	echo '#define LONG_VERSION_STRING   "'@USERAGENT_PREFIX@' ('`svn info | grep "Revision" | awk -F': ' '{print $$2}'`')"' >> version.h
