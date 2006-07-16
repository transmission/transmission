# $Id$

include mk/config.mk
include mk/common.mk

TARGETS = .cli
ifeq ($(GTK),yes)
TARGETS += .gtk
endif
ifeq ($(SYSTEM),BeOS)
TARGETS += .beos
endif

all: $(TARGETS)

.lib:
	@echo "* Building libtransmission"
	@$(MAKE) -C libtransmission -f ../mk/lib.mk

.cli: .lib
	@echo "* Building Transmission CLI client"
	@$(MAKE) -C cli -f ../mk/cli.mk

.gtk: .lib
	@echo "* Building Transmission GTK+ client"
	@$(MAKE) -C gtk -f ../mk/gtk.mk

.beos: .lib
	@echo "* Building Transmission BeOS client"
	@make -C beos -f ../mk/beos.mk

install: all $(foreach SUB,$(TARGETS),.install$(SUB))

.install.cli: .cli
	@echo "* Installing Transmission CLI client"
	@$(MAKE) -C cli -f ../mk/cli.mk install

.install.gtk: .gtk
	@echo "* Installing Transmission GTK+ client"
	@$(MAKE) -C gtk -f ../mk/gtk.mk install

.install.beos:

clean:
	@$(MAKE) -C libtransmission -f ../mk/lib.mk clean
	@$(MAKE) -C cli -f ../mk/cli.mk clean
ifeq ($(GTK),yes)
	@$(MAKE) -C gtk -f ../mk/gtk.mk clean
endif
ifeq ($(SYSTEM),BeOS)
	@$(MAKE) -C beos -f ../mk/beos.mk clean
endif
