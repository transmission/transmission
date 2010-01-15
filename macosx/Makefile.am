EXTRA_DIST = \
    Credits.rtf \
    AboutWindow.xib \
    BlocklistStatusWindow.xib \
    MessageWindow.xib \
    StatsWindow.xib \
    Defaults.plist \
    Info.plist \
    Transmission_Prefix.pch \
    Growl.framework \
    Sparkle.framework \
    sparkle_dsa_pub.pem \
    UKKQueue \
    TransmissionHelp \
    Images \
    en.lproj \
    es.lproj \
    fr.lproj \
    it.lproj \
    nl.lproj \
    ru.lproj \
    zh_CN.lproj \
    zh_TW.lproj \
    AboutWindowController.h \
    AboutWindowController.m \
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
    BonjourController.h \
    BonjourController.m \
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
    GroupsPrefsController.h \
    GroupsPrefsController.m \
    GroupToolbarItem.h \
    GroupToolbarItem.m \
    InfoTabButtonCell.h \
    InfoTabButtonCell.m \
    InfoTextField.h \
    InfoTextField.m \
    InfoWindowController.h \
    InfoWindowController.m \
    main.m \
    MessageWindowController.h \
    MessageWindowController.m \
    NSApplicationAdditions.h \
    NSApplicationAdditions.m \
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
    PredicateEditorRowTemplateAny.h \
    PredicateEditorRowTemplateAny.m \
    PrefsController.h \
    PrefsController.m \
    PrefsWindow.h \
    PrefsWindow.m \
    ProgressGradients.h \
    ProgressGradients.m \
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
    TrackerCell.h \
    TrackerCell.m \
    TrackerNode.h \
    TrackerNode.m \
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
