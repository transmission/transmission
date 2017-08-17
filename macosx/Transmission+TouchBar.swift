//
//  Transmission+TouchBar.swift
//  Transmission
//
//  Created by Alexandre Jouandin on 16/08/2017.
//  Copyright © 2017 The Transmission Project. All rights reserved.
//

import Foundation

@available(OSX 10.12.2, *)
extension NSTouchBar {
    static var touchbarDomain: String {
        return (Bundle.main.infoDictionary![kCFBundleNameKey as String] as! String) + ".touchbar"
    }
}

@available(OSX 10.12.2, *)
fileprivate extension NSTouchBarCustomizationIdentifier {
    static let touchBar = NSTouchBarCustomizationIdentifier(NSTouchBar.touchbarDomain)
}

@available(OSX 10.12.2, *)
fileprivate extension NSTouchBarItemIdentifier {
    static let create = NSTouchBarItemIdentifier(NSTouchBar.touchbarDomain+"_create_torrent")
    static let openFile = NSTouchBarItemIdentifier(NSTouchBar.touchbarDomain+"_open_file")
    static let openWeb = NSTouchBarItemIdentifier(NSTouchBar.touchbarDomain+"_open_web")
    static let info = NSTouchBarItemIdentifier(NSTouchBar.touchbarDomain+"_info")
    static let pauseResumeSelected = NSTouchBarItemIdentifier(NSTouchBar.touchbarDomain+"_pause_resume_selected")
}

@available(OSX 10.12.2, *)
enum TouchbarGroupTag: Int {
    case pause = 0
    case resume = 1
}

// MARK: - TouchBar set up
@available(OSX 10.12.2, *)
extension TorrentTableView {
    override open func makeTouchBar() -> NSTouchBar? {
        NSTouchBar.isAutomaticValidationEnabled = true
        let touchBar = NSTouchBar()
        touchBar.delegate = self
        touchBar.customizationIdentifier = .touchBar
        touchBar.defaultItemIdentifiers = [.create, .openFile, .flexibleSpace, .pauseResumeSelected, .flexibleSpace, .info, .otherItemsProxy]
        touchBar.customizationAllowedItemIdentifiers = [.create, .openFile, .openWeb, .info, .flexibleSpace, .pauseResumeSelected]
        
        return touchBar
    }
}

// MARK: - TouchBar items builder
@available(OSX 10.12.2, *)
extension TorrentTableView: NSTouchBarDelegate {
    public func touchBar(_ touchBar: NSTouchBar, makeItemForIdentifier identifier: NSTouchBarItemIdentifier) -> NSTouchBarItem? {
        switch identifier {
        case .create:
            let touchbarItem = NSCustomTouchBarItem(identifier: identifier)
            guard let buttonImage = NSImage(named: "ToolbarCreateTemplate") else {
                assertionFailure("Cannot instantiate image for touchbar button")
                return touchbarItem
            }
            let button = NSButton(image: buttonImage,
                                  target: self.controller,
                                  action: #selector(Controller.createFile(_:)))
            button.isHidden = false
            button.setAccessibilityLabel(NSLocalizedString("Create Torrent File", comment: "Create toolbar item -> palette label"))
            
            touchbarItem.view = button
            touchbarItem.customizationLabel = NSLocalizedString("Create Torrent File", comment: "Create toolbar item -> palette label")
            return touchbarItem
        case .openFile:
            let touchbarItem = NSCustomTouchBarItem(identifier: identifier)
            guard let buttonImage = NSImage(named: "ToolbarOpenTemplate") else {
                assertionFailure("Cannot instantiate image for touchbar button")
                return touchbarItem
            }
            let button = NSButton(image: buttonImage,
                                  target: self.controller,
                                  action: #selector(Controller.openShowSheet(_:)))
            button.isHidden = false
            button.setAccessibilityLabel(NSLocalizedString("Open Torrent Files", comment: "Open toolbar item -> palette label"))
            
            touchbarItem.view = button
            touchbarItem.customizationLabel = NSLocalizedString("Open Torrent Files", comment: "Open toolbar item -> palette label")
            return touchbarItem
        case .openWeb:
            let touchbarItem = NSCustomTouchBarItem(identifier: identifier)
            guard let buttonImage = NSImage(named: "ToolbarOpenWebTemplate") else {
                assertionFailure("Cannot instantiate image for touchbar button")
                return touchbarItem
            }
            let button = NSButton(image: buttonImage,
                                  target: self.controller,
                                  action: #selector(Controller.openURLShowSheet(_:)))
            button.isHidden = false
            button.setAccessibilityLabel(NSLocalizedString("Open Torrent Address", comment: "Open address toolbar item -> palette label"))
            
            touchbarItem.view = button
            touchbarItem.customizationLabel = NSLocalizedString("Open Torrent Address", comment: "Open address toolbar item -> palette label")
            return touchbarItem
        case .info:
            let touchbarItem = NSCustomTouchBarItem(identifier: identifier)
            guard let buttonImage = NSImage(named: "ToolbarInfoTemplate") else {
                assertionFailure("Cannot instantiate image for touchbar button")
                return touchbarItem
            }
            let button = NSButton(image: buttonImage,
                                  target: self.controller,
                                  action: #selector(Controller.showInfo(_:)))
            button.isHidden = false
            button.setButtonType(.onOff)
            button.setAccessibilityLabel(NSLocalizedString("Toggle Inspector", comment: "Inspector toolbar item -> palette label")) // FIXME: Check if this works
            touchbarItem.view = button
            touchbarItem.customizationLabel = NSLocalizedString("Toggle Inspector", comment: "Inspector toolbar item -> palette label")
            return touchbarItem
        case .pauseResumeSelected:
            let groupItem = NSCustomTouchBarItem(identifier: identifier)
            let segmentedControl = NSSegmentedControl(frame: NSZeroRect)
            segmentedControl.cell = ToolbarSegmentedCell()
            let segmentedCell = segmentedControl.cell as! NSSegmentedCell
            groupItem.view = segmentedControl
            segmentedControl.target = self
            segmentedControl.action = #selector(selectedTouchbarClicked)
            segmentedControl.trackingMode = .momentary
            
            // Set up segmented control
            segmentedControl.segmentStyle = .separated  // OSX 10.10+ (10.12.2 in this scope)
            segmentedControl.segmentCount = 2
            segmentedCell.trackingMode = .momentary
            
            segmentedCell.setTag(TouchbarGroupTag.pause.rawValue, forSegment: TouchbarGroupTag.pause.rawValue)
            segmentedControl.setImage(NSImage(named: "ToolbarPauseSelectedTemplate"), forSegment: TouchbarGroupTag.pause.rawValue)
            if #available(OSX 10.13, *) {
                segmentedCell.setToolTip(NSLocalizedString("Pause selected transfers", comment: "Selected toolbar item -> tooltip"), forSegment: TouchbarGroupTag.pause.rawValue)
            }
            
            segmentedCell.setTag(TouchbarGroupTag.resume.rawValue, forSegment: TouchbarGroupTag.resume.rawValue)
            segmentedControl.setImage(NSImage(named: "ToolbarResumeSelectedTemplate"), forSegment: TouchbarGroupTag.resume.rawValue)
            if #available(OSX 10.13, *) {
                segmentedCell.setToolTip(NSLocalizedString("Resume selected transfers", comment: "Selected toolbar item -> tooltip"), forSegment: TouchbarGroupTag.resume.rawValue)
            }
            
            groupItem.visibilityPriority = .high
            groupItem.customizationLabel = NSLocalizedString("Pause / Resume Selected", comment: "Selected toolbar item -> palette label")
            
            return groupItem
        default:
            return nil
        }
    }
    
    @objc func selectedTouchbarClicked(_ sender: NSControl) {
            self.controller.selectedToolbarClicked(sender)
            
            // update UI manually
            //   -> workaround for the issue where UI doesn't update on a touch bar event (2017-01 macOS 10.12.2 SDK)
            self.window?.toolbar?.validateVisibleItems()
            self.touchBar?.validateVisibleItems()
    }
}

// MARK: - TouchBar items validation
@available(OSX 10.12.2, *)
extension Controller: TouchBarItemValidations {
    func validateTouchBarItem(_ item: NSTouchBarItem) -> Bool {
        switch item.identifier {
        case .info:
            guard let button = item.view as? NSButton else {
                assertionFailure("Unexpected view for info item in TouchBar")
                return true
            }
            button.state = self.infoController.window != nil && self.infoController.window!.isVisible ? NSOnState : NSOffState
        default:
            return true
        }
        return true
    }
}

@available(OSX 10.12.2, *)
extension TorrentTableView: TouchBarItemValidations {
    func validateTouchBarItem(_ item: NSTouchBarItem) -> Bool {
        switch item.identifier {
        case .pauseResumeSelected:
            guard let segmentedControl = item.view as? NSSegmentedControl else {
                assertionFailure("Unexpected view for pauseResumeSelected item in TouchBar")
                return true
            }
            // Pause enabled?
            let pauseEnabled: Bool = {
                for torrent in (self.controller.torrentTableView.selectedTorrents() as! [Torrent]) {
                    if (torrent.isActive() || torrent.waitingToStart()) {
                        return true
                    }
                }
                return false
            }()
            segmentedControl.setEnabled(pauseEnabled, forSegment: TouchbarGroupTag.pause.rawValue)
            // Resume enabled?
            let resumeEnabled: Bool = {
                for torrent in (self.controller.torrentTableView.selectedTorrents() as! [Torrent]) {
                    if (!torrent.isActive() && !torrent.waitingToStart()) {
                        return true
                    }
                }
                return false
            }()
            segmentedControl.setEnabled(resumeEnabled, forSegment: TouchbarGroupTag.resume.rawValue)
        default:
            return true
        }
        return true
    }
}
