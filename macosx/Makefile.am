EXTRA_DIST = \
    Credits.rtf \
    AboutWindow.xib \
    BlocklistStatusWindow.xib \
    FileRenameSheetController.xib \
    FilterBar.xib \
    InfoFileView.xib \
    InfoPeersView.xib \
    InfoTrackersView.xib \
    InfoWindow.xib \
    MessageWindow.xib \
    StatsWindow.xib \
    StatusBar.xib \
    URLSheetWindow.xib \
    Defaults.plist \
    Info.plist \
    Transmission_Prefix.pch \
    Sparkle.framework \
    sparkle_dsa_pub.pem \
    TransmissionHelp \
    QuickLookPlugin \
    VDKQueue \
    Images \
    da.lproj \
    de.lproj \
    en.lproj \
    es.lproj \
    fr.lproj \
    it.lproj \
    nl.lproj \
    pt_PT.lproj \
    ru.lproj \
    tr.lproj \
    AboutWindowController.h \
    AboutWindowController.m \
    AddMagnetWindowController.h \
    AddMagnetWindowController.m \
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
    FileRenameSheetController.h \
    FileRenameSheetController.m \
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
    FilterBarController.h \
    FilterBarController.m \
    FilterBarView.h \
    FilterBarView.m \
    FilterButton.h \
    FilterButton.m \
    GlobalOptionsPopoverViewController.h \
    GlobalOptionsPopoverViewController.m \
    GroupsController.h \
    GroupsController.m \
    GroupsPrefsController.h \
    GroupsPrefsController.m \
    GroupToolbarItem.h \
    GroupToolbarItem.m \
    InfoTabButtonBack.h \
    InfoTabButtonBack.m \
    InfoTabButtonCell.h \
    InfoTabButtonCell.m \
    InfoTextField.h \
    InfoTextField.m \
    InfoActivityViewController.h \
    InfoActivityViewController.m \
    InfoFileViewController.h \
    InfoFileViewController.m \
    InfoGeneralViewController.h \
    InfoGeneralViewController.m \
    InfoOptionsViewController.h \
    InfoOptionsViewController.m \
    InfoPeersViewController.h \
    InfoPeersViewController.m \
    InfoTrackersViewController.h \
    InfoTrackersViewController.m \
    InfoViewController.h \
    InfoWindowController.h \
    InfoWindowController.m \
    main.m \
    MessageWindowController.h \
    MessageWindowController.m \
    NSApplicationAdditions.h \
    NSApplicationAdditions.m \
    NSImageAdditions.h \
    NSImageAdditions.m \
    NSMutableArrayAdditions.h \
    NSMutableArrayAdditions.m \
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
    ShareToolbarItem.h \
    ShareToolbarItem.m \
    ShareTorrentFileHelper.h \
    ShareTorrentFileHelper.m \
    StatsWindowController.h \
    StatsWindowController.m \
    StatusBarController.h \
    StatusBarController.m \
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
    TrackerTableView.m \
    URLSheetWindowController.h \
    URLSheetWindowController.m \
    WebSeedTableView.h \
    WebSeedTableView.m

dist-hook:
	rm -rf `find $(distdir)/ -type d -name .svn`
	rm -rf `find $(distdir)/ -type d -name .deps`

if BUILD_MAC
# Use Xcode do make the actual build
all:
	@(cd .. && xcodebuild -alltargets -activeconfiguration | grep -v "^$$")
clean-local:
	@(cd .. && xcodebuild -alltargets -activeconfiguration clean | grep -v "^$$")
endif

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
