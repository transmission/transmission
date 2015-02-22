NULL =

public_icons_themes = \
	hicolor	\
	$(NULL)

public_icons = \
	16x16/apps/transmission.png \
	22x22/apps/transmission.png \
	24x24/apps/transmission.png \
	32x32/apps/transmission.png \
	48x48/apps/transmission.png \
	256x256/apps/transmission.png \
	scalable/apps/transmission.svg \
	$(NULL)

private_icons = \
	$(NULL)

noinst_DATA = \
	scalable/actions/alternative-speed-off-symbolic.svg \
	scalable/actions/alternative-speed-on-symbolic.svg \
	scalable/actions/statistics-symbolic.svg \
	source.svg \
	$(NULL)

EXTRA_DIST = \
	$(public_icons)		\
	$(private_icons)	\
	$(noinst_DATA)		\
	$(NULL)

###############################################################################

gtk_update_icon_cache = gtk-update-icon-cache -f -t

update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		for theme in $(public_icons_themes); do \
			$(gtk_update_icon_cache) $(datadir)/icons/$$theme; \
		done; \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		for theme in $(public_icons_themes); do \
			echo "***   $(gtk_update_icon_cache) $(datadir)/icons/$$theme"; \
		done; \
	fi

install-icons:
	for icon in $(public_icons); do \
		THEME="hicolor"; \
		ICONDIR=`dirname $$icon` \
		mkdir -p $(DESTDIR)$(datadir)/icons/$$THEME/$$ICONDIR; \
		$(INSTALL_DATA) $(srcdir)/$$icon $(DESTDIR)$(datadir)/icons/$$THEME/$$icon; \
	done; \
	for icon in $(private_icons); do \
		THEME="hicolor"; \
		ICONDIR=`dirname $$icon` \
		mkdir -p $(DESTDIR)$(pkgdatadir)/icons/$$THEME/$$ICONDIR; \
		$(INSTALL_DATA) $(srcdir)/$$icon $(DESTDIR)$(pkgdatadir)/icons/$$THEME/$$icon; \
	done

uninstall-icons:
	-for icon in $(public_icons); do \
		THEME="hicolor"; \
		rm -f $(DESTDIR)$(datadir)/icons/$$THEME/$$icon; \
	done; \
	for icon in $(private_icons); do \
		THEME="hicolor"; \
		rm -f $(DESTDIR)$(pkgdatadir)/icons/$$THEME/$$icon; \
	done

install-data-local: install-icons update-icon-cache

uninstall-local: uninstall-icons update-icon-cache
