EXTRA_DIST = \
    Credits.rtf \
    AboutWindow.xib \
    BlocklistStatusWindow.xib \
    GroupsWindow.xib \
    MessageWindow.xib \
    StatsWindow.xib \
    Defaults.plist \
    Info.plist \
    Transmission_Prefix.pch \
    version.sh \
    Growl.framework \
    Sparkle.framework \
    CTGradient \
    UKKQueue \
    TransmissionHelp \
    Images \
    appcast \
    en.lproj \
    es.lproj \
    fr.lproj \
    it.lproj \
    nl.lproj \
    ru.lproj \
    tr.lproj \
    zh_TW.lproj \
    AboutWindowController.h \
    AboutWindowController.m \
    ActionPopUpButton.h \
    ActionPopUpButton.m \
    AddWindowController.h \
    AddWindowController.m \
    Badger.h \
    Badger.m \
    BadgeView.h \
    BadgeView.m \
    BlocklistDownloader.h \
    BlocklistDownloader.m \
    BlocklistDownloaderViewController.h \
    BlocklistDownloaderViewController.m \
    BlocklistScheduler.h \
    BlocklistScheduler.m \
    ButtonToolbarItem.h \
    ButtonToolbarItem.m \
    ColorTextField.h \
    ColorTextField.m \
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
    FileListNode.h \
    FileListNode.m \
    FileNameCell.h \
    FileNameCell.m \
    FileOutlineController.h \
    FileOutlineController.m \
    FileOutlineView.h \
    FileOutlineView.m \
    FilePriorityCell.h \
    FilePriorityCell.m \
    FilterButton.h \
    FilterButton.m \
    GroupsController.h \
    GroupsController.m \
    GroupsWindowController.h \
    GroupsWindowController.m \
    GroupToolbarItem.h \
    GroupToolbarItem.m \
    InfoTabButtonCell.h \
    InfoTabButtonCell.m \
    InfoWindowController.h \
    InfoWindowController.m \
    main.m \
    MenuLabel.h \
    MenuLabel.m \
    MessageWindowController.h \
    MessageWindowController.m \
    NSApplicationAdditions.h \
    NSApplicationAdditions.m \
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
    ProgressGradients.h \
    ProgressGradients.m \
    QuickLookController.h \
    QuickLookController.m \
    QuickLook.h \
    SpeedLimitToTurtleIconTransformer.h \
    SpeedLimitToTurtleIconTransformer.m \
    StatsWindowController.h \
    StatsWindowController.m \
    StatusBarView.h \
    StatusBarView.m \
    ToolbarSegmentedCell.h \
    ToolbarSegmentedCell.m \
    TorrentCell.h \
    TorrentCell.m \
    TorrentGroup.h \
    TorrentGroup.m \
    Torrent.h \
    Torrent.m \
    TorrentTableView.h \
    TorrentTableView.m \
    TrackerTableView.h \
    TrackerTableView.m

dist-hook:
	rm -rf `find $(distdir)/ -type d -name .svn`
	rm -rf `find $(distdir)/ -type d -name .deps`

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
