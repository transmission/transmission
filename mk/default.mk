# $Id$

include mk/config.mk
include mk/common.mk

TARGETS = .cli
ifeq ($(GTK),yes)
TARGETS += .gtk
endif
ifeq ($(SYSTEM),BeOS)
#TARGETS += .beos
endif

all: $(TARGETS)

.lib:
	@echo "* Building libtransmission"
	@$(MAKE) $(MAKEARGS) -C libtransmission -f ../mk/lib.mk

.cli: .lib
	@echo "* Building Transmission CLI client"
	@$(MAKE) $(MAKEARGS) -C cli -f ../mk/cli.mk

.gtk: .lib
	@echo "* Building Transmission GTK+ client"
	@$(MAKE) $(MAKEARGS) -C gtk -f ../mk/gtk.mk

.beos: .lib
	@echo "* Building Transmission BeOS client"
	@$(MAKE) $(MAKEARGS) -C beos -f ../mk/beos.mk

install: all $(foreach SUB,$(TARGETS),.install$(SUB)) .install.misc

.install.cli: .cli
	@echo "* Installing Transmission CLI client"
	@$(MAKE) $(MAKEARGS) -C cli -f ../mk/cli.mk install

.install.gtk: .gtk
	@echo "* Installing Transmission GTK+ client"
	@$(MAKE) $(MAKEARGS) -C gtk -f ../mk/gtk.mk install

.install.beos:

.install.misc:
	@echo "* Installing Zsh completion file"
	@$(MKDIR) $(DESTDIR)$(PREFIX)/share/zsh/site-functions
	@$(CP) misc/transmissioncli.zsh $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_transmissioncli

clean:
	@$(MAKE) $(MAKEARGS) -C libtransmission -f ../mk/lib.mk clean
	@$(MAKE) $(MAKEARGS) -C cli -f ../mk/cli.mk clean
ifeq ($(GTK),yes)
	@$(MAKE) $(MAKEARGS) -C gtk -f ../mk/gtk.mk clean
endif
ifeq ($(SYSTEM),BeOS)
	@$(MAKE) $(MAKEARGS) -C beos -f ../mk/beos.mk clean
endif
