# $Id$

include ../../Makefile.config
include ../../Makefile.common

POFILES = $(wildcard *.po)
MOFILES = $(POFILES:%.po=%.mo)

all: $(MOFILES)
	@true

%.mo: %.po ../../Makefile.config ../../Makefile.common Makefile
	$(MSGFMT_RULE)

%.mo.install: %.mo
	$(INSTALL_LOCALE_RULE)

clean:
	@echo "Clean $(MOFILES)"
	$(RM) $(MOFILES)

install: $(MOFILES) $(MOFILES:%.mo=%.mo.install)

-include .depend
