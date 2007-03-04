# $Id$

TMPCFLAGS   = -g -Wall -W -O0 -funroll-loops -D_FILE_OFFSET_BITS=64 \
              -D_LARGEFILE_SOURCE -D_GNU_SOURCE \
              -DSYS_$(shell echo $(SYSTEM) | tr a-z A-Z)
TMPCXXFLAGS = $(TMPCFLAGS)
TMPLDFLAGS  =

ifeq ($(SYSTEM),BeOS)
TMPCXXFLAGS += -Wno-multichar
ifeq ($(BEOS_NETSERVER),yes)
TMPCFLAGS  += -DBEOS_NETSERVER
TMPLDFLAGS += -lnet
else
TMPLDFLAGS += -lbind -lsocket
endif
endif

ifeq ($(PTHREAD),yes)
ifneq ($(filter FreeBSD OpenBSD,$(SYSTEM)),)
TMPCFLAGS  += -pthread
TMPLDFLAGS += -pthread
else
TMPLDFLAGS += -lpthread
endif
endif

ifeq ($(OPENSSL),yes)
TMPCFLAGS  += -DHAVE_OPENSSL
TMPLDFLAGS += -lcrypto
endif

CFLAGS   := $(TMPCFLAGS) $(CFLAGS)
CXXFLAGS := $(TMPCXXFLAGS) $(CXXFLAGS)
LDFLAGS  := $(TMPLDFLAGS) $(LDFLAGS)

#
# Utils
#

define DEP_RULE
	@echo "Checking dependencies..."
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(CC) -MM $(SRC) $(CFLAGS) >> .depend;)
endef

define CC_RULE
	@echo "Cc $@"
	@CMD="$(CC) $(CFLAGS) -o $@ -c $<"; $$CMD || \
	  ( echo "Compile line for $@ was:"; echo $$CMD; false )
endef

define LINK_RULE
	@echo "Link $@"
	@CMD="$(CC) -o $@ $(OBJS) $(LDLIBS) $(LDFLAGS)"; $$CMD || \
	  ( echo "Compile line for $@ was:"; echo $$CMD; false )
endef

define MSGFMT_RULE
       @echo "Msgfmt $<"
       @msgfmt -f $< -o $@
endef

define INSTALL_BIN_RULE
       @echo "Install $<"
       @$(MKDIR) $(DESTDIR)$(PREFIX)/bin
       @$(CP) $< $(DESTDIR)$(PREFIX)/bin/
endef

define INSTALL_LOCALE_RULE
       @echo "Install $<"
       @$(MKDIR) $(DESTDIR)$(LOCALEDIR)/$*/LC_MESSAGES
       @$(CP) $< $(DESTDIR)$(LOCALEDIR)/$*/LC_MESSAGES/transmission-gtk.mo
endef

define INSTALL_MAN_RULE
	@echo "Install $<"
	@$(MKDIR) $(DESTDIR)$(PREFIX)/man/man1
	@$(CP) $< $(DESTDIR)$(PREFIX)/man/man1/
endef

define INSTALL_DESKTOP_RULE
	@echo "Install $<"
	@$(MKDIR) $(DESTDIR)$(PREFIX)/share/applications
	@$(CP) $< $(DESTDIR)$(PREFIX)/share/applications/
endef

define INSTALL_ICON_RULE
	@echo "Install $<"
	@$(MKDIR) $(DESTDIR)$(PREFIX)/share/pixmaps
	@$(CP) $< $(DESTDIR)$(PREFIX)/share/pixmaps/
endef

RM       = rm -Rf
CP       = cp -f
MKDIR    = mkdir -p
MAKEARGS = --no-print-directory
