include Makefile.config
include Makefile.common

ifneq ($(SYSTEM),Darwin)

SRCS = transmissioncli.c
OBJS = $(SRCS:%.c=%.o)

CFLAGS += -Ilibtransmission

all: transmissioncli transmission-gtk transmission-beos

lib:
	$(MAKE) -C libtransmission

transmissioncli: lib $(OBJS)
	$(CC) -o $@ $(OBJS) libtransmission/libtransmission.a $(LDFLAGS)

transmission-gtk:
ifeq ($(GTK),yes)
	$(MAKE) -C gtk
endif

transmission-beos:
ifeq ($(SYSTEM),BeOS)
	$(MAKE) -C beos
endif

%.o: %.c Makefile.config Makefile.common Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) transmissioncli $(OBJS)
	$(MAKE) -C libtransmission clean
ifeq ($(GTK),yes)
	$(MAKE) -C gtk clean
endif
ifeq ($(SYSTEM),BeOS)
	$(MAKE) -C beos clean
endif

.depend: $(SRCS) Makefile
	$(RM) .depend
	$(foreach SRC, $(SRCS), $(CC) -MM -Ilibtransmission $(SRC) >> .depend;)

include .depend

else

all:
	make -C macosx
	xcodebuild -alltargets -activeconfiguration | grep -v "^$$"

clean:
	xcodebuild -alltargets -activeconfiguration clean | grep -v "^$$"
	make -C macosx clean

MAKELINK = printf "[InternetShortcut]\nURL=http://%s\n"

package:
	$(RM) tmp "Transmission $(VERSION_STRING)" \
	    Transmission-$(VERSION_STRING).dmg && \
	  mkdir -p tmp && \
	  cp -r Transmission.app tmp/ && \
	  cp AUTHORS tmp/AUTHORS.txt && \
	  cp LICENSE tmp/LICENSE.txt && \
	  cp NEWS tmp/NEWS.txt && \
	  strip -S tmp/Transmission.app/Contents/MacOS/Transmission && \
	  $(MAKELINK) "transmission.m0k.org/" > tmp/Homepage.url && \
	  $(MAKELINK) "transmission.m0k.org/forum" > tmp/Forums.url && \
	  $(MAKELINK) "transmission.m0k.org/contribute.php" > tmp/Contribute.url && \
	  mv tmp "Transmission $(VERSION_STRING)" && \
	  hdiutil create -format UDZO -srcfolder \
	    "Transmission $(VERSION_STRING)" Transmission-$(VERSION_STRING).dmg && \
	  rm -rf "Transmission $(VERSION_STRING)"

endif
