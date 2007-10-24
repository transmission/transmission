EXTRA_DIST = \
    Sparkle.framework \
    CTGradient \
    UKKQueue \
    TransmissionHelp \
    Applescript \
    Images \
    Growl.framework \
    English.lproj \
    el.lproj \
    es.lproj \
    fr.lproj \
    it.lproj \
    ko.lproj \
    AboutWindowController.h \
    AboutWindowController.m \
    ActionMenuRatioToDisplayRatioTransformer.h \
    ActionMenuRatioToDisplayRatioTransformer.m \
    ActionMenuSpeedToDisplayLimitTransformer.h \
    ActionMenuSpeedToDisplayLimitTransformer.m \
    ActionPopUpButton.h \
    ActionPopUpButton.m \
    Badger.h \
    Badger.m \
    Controller.h \
    Controller.m \
    CreatorWindowController.h \
    CreatorWindowController.m \
    DragOverlayView.h \
    DragOverlayView.m \
    DragOverlayWindow.h \
    DragOverlayWindow.m \
    ExpandedPathToIconTransformer.h \
    ExpandedPathToIconTransformer.m \
    ExpandedPathToPathTransformer.h \
    ExpandedPathToPathTransformer.m \
    FileNameCell.h \
    FileNameCell.m \
    FileOutlineView.h \
    FileOutlineView.m \
    FilePriorityCell.h \
    FilePriorityCell.m \
    FilterBarButton.h \
    FilterBarButton.m \
    FilterBarView.h \
    FilterBarView.m \
    ImageBackgroundView.h \
    ImageBackgroundView.m \
    InfoTabButtonCell.h \
    InfoTabButtonCell.m \
    InfoWindowController.h \
    InfoWindowController.m \
    IPCController.h \
    IPCController.m \
    main.m \
    MessageWindowController.h \
    MessageWindowController.m \
    NSBezierPathAdditions.h \
    NSBezierPathAdditions.m \
    NSMenuAdditions.h \
    NSMenuAdditions.m \
    NSStringAdditions.h \
    NSStringAdditions.m \
    PeerProgressIndicatorCell.h \
    PeerProgressIndicatorCell.m \
    PeerTableView.h \
    PeerTableView.m \
    PiecesView.h \
    PiecesView.m \
    PortChecker.h \
    PortChecker.m \
    PrefsController.h \
    PrefsController.m \
    PrefsWindow.h \
    PrefsWindow.m \
    SpeedLimitToTurtleIconTransformer.h \
    SpeedLimitToTurtleIconTransformer.m \
    StatusBarView.h \
    StatusBarView.m \
    TorrentCell.h \
    TorrentCell.m \
    Torrent.h \
    Torrent.m \
    TorrentTableView.h \
    TorrentTableView.m

dist-hook:
	rm -rf `find $(distdir)/macosx -name ".svn"`

# Use Xcode do make the actual build
all:
	@(cd .. && xcodebuild -alltargets -activeconfiguration | grep -v "^$$")
clean:
	@(cd .. && xcodebuild -alltargets -activeconfiguration clean | grep -v "^$$")

define PACKAGE_RULE1
	$(RM) tmp "Transmission $(VERSION_PACKAGE)" \
	  Transmission-$(VERSION_PACKAGE).dmg
	mkdir -p tmp
	cp -R macosx/Transmission.app tmp/
endef
define PACKAGE_RULE2
	mv tmp "Transmission $(VERSION_PACKAGE)"
	hdiutil create -format UDZO -imagekey zlib-level=9 -srcfolder \
	  "Transmission $(VERSION_PACKAGE)" Transmission-$(VERSION_PACKAGE).dmg
	rm -rf "Transmission $(VERSION_PACKAGE)"
endef

package:
	$(PACKAGE_RULE1)
	$(PACKAGE_RULE2)

package-release:
	$(PACKAGE_RULE1)
	strip -S tmp/Transmission.app/Contents/MacOS/Transmission
	$(PACKAGE_RULE2)
