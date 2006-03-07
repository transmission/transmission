include Makefile.config
include Makefile.common

SRCS = transmissioncli.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS += -Ilibtransmission

all: transmissioncli
ifeq ($(SYSTEM),Darwin)
	$(MAKE) -C macosx
endif

transmissioncli: lib $(OBJS)
	$(CC) -o $@ $(OBJS) libtransmission/libtransmission.a $(LDFLAGS)

lib:
	$(MAKE) -C libtransmission

%.o: %.c Makefile.config Makefile.common Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

package-macosx:
	$(RM) tmp "Transmission $(VERSION_STRING)" \
	    Transmission-$(VERSION_STRING).dmg && \
	  mkdir -p tmp/Transmission.app && \
	  ditto macosx/build/Debug/Transmission.app tmp/Transmission.app && \
	  ditto AUTHORS tmp/AUTHORS.txt && \
	  ditto LICENSE tmp/LICENSE.txt && \
	  ditto NEWS tmp/NEWS.txt && \
	  strip -S tmp/Transmission.app/Contents/MacOS/Transmission && \
	  ( echo "[InternetShortcut]"; \
	    echo "URL=http://transmission.m0k.org/" ) > \
		tmp/Homepage.url && \
	  ( echo "[InternetShortcut]"; \
	    echo "URL=http://transmission.m0k.org/forum/" ) > \
		tmp/Forums.url && \
	  ( echo "[InternetShortcut]"; \
	    echo "URL=http://transmission.m0k.org/contribute.php" ) > \
	    tmp/Contribute.url && \
	  mv tmp "Transmission $(VERSION_STRING)" && \
	  hdiutil create -format UDZO -srcfolder \
	    "Transmission $(VERSION_STRING)" Transmission-$(VERSION_STRING).dmg && \
	  rm -rf "Transmission $(VERSION_STRING)"

clean:
	$(RM) transmissioncli $(OBJS)
	$(MAKE) -C libtransmission clean
ifeq ($(SYSTEM),Darwin)
	$(MAKE) -C macosx clean
endif

.depend: $(SRCS) Makefile
	$(RM) .depend
	$(foreach SRC, $(SRCS), $(CC) -MM -Ilibtransmission $(SRC) >> .depend;)

include .depend
