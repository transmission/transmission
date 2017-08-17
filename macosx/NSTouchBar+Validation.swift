/*
 
 NSTouchBar+Validation.swift
 
 CotEditor
 https://coteditor.com
 
 Created by 1024jp on 2016-12-02.
 
 ------------------------------------------------------------------------------
 
 © 2016-2017 1024jp
 
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 
 https://www.apache.org/licenses/LICENSE-2.0
 
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 
 */

import Cocoa

@available(macOS 10.12.2, *)
protocol TouchBarItemValidations: class {
    
    func validateTouchBarItem(_ item: NSTouchBarItem) -> Bool
}



@available(macOS 10.12.2, *)
extension NSTouchBar {
    
    /// flag to enable automatic touch bar item validation
    static var isAutomaticValidationEnabled = false {
        
        didSet {
            TouchBarValidator.shared.isEnabled = isAutomaticValidationEnabled
        }
    }
    
    
    
    /// validate currently visible touch bar items
    func validateVisibleItems() {
        
        guard self.isVisible else { return }
        
        for identifier in self.itemIdentifiers {
            guard let item = self.item(forIdentifier: identifier), item.isVisible else { continue }
            
            switch item {
            case let item as NSCustomTouchBarItem:
                item.validate()
                
            case let item as NSGroupTouchBarItem:
                item.groupTouchBar.validateVisibleItems()
                
            case let item as NSPopoverTouchBarItem:
                item.popoverTouchBar.validateVisibleItems()
                item.pressAndHoldTouchBar?.validateVisibleItems()
                
            default: break
            }
        }
    }
    
}



// MARK: -

@available(macOS 10.12.2, *)
private final class TouchBarValidator {
    
    // MARK: Public Properties
    
    static let shared = TouchBarValidator()
    
    
    var isEnabled: Bool = false {
        
        didSet {
            guard self.isEnabled != oldValue else { return }
            
            if self.isEnabled {
                NotificationCenter.default.addObserver(self, selector: #selector(applicationDidUpdate(_:)), name: .NSApplicationDidUpdate, object: nil)
            } else {
                NotificationCenter.default.removeObserver(self, name: .NSApplicationDidUpdate, object: nil)
            }
        }
    }
    
    
    
    // MARK: Private Properties
    
    private weak var validationTimer: Timer?
    
    private enum ValidationDelay: TimeInterval {
        
        case normal = 0.1
        case lazy = 0.85
    }
    
    
    
    // MARK: -
    // MARK: Lifecycle
    
    private init() { }
    
    
    deinit {
        self.isEnabled = false  // remove observer if needed
        self.validationTimer?.invalidate()
    }
    
    
    
    // MARK: Private Methods
    
    /// application did update
    @objc private func applicationDidUpdate(_ notification: Notification) {
        
        self.validateTouchBarIfNeeded()
    }
    
    
    /// validate current touch bar
    @objc private func validateTouchBar(timer: Timer?) {
        
        self.validationTimer?.invalidate()
        
        NSApp.touchBar?.validateVisibleItems()
        
        guard let window = NSApp.mainWindow else { return }
        
        for responder in sequence(first: window.firstResponder, next: { $0?.nextResponder }) {
            responder?.touchBar?.validateVisibleItems()
        }
    }
    
    
    /// check necessity of touch bar validation and schedule with a delay if needed
    private func validateTouchBarIfNeeded() {
        
        guard
            self.isEnabled,
            let event = NSApp.currentEvent
            else { return }
        
        // skip validation for specific events just like NSToolbar does
        //   -> See Apple's API reference for NSToolbar's `validateVisibleItems()` to see which events should be skipped:
        //        cf. https://developer.apple.com/reference/appkit/nstoolbar/1516947-validatevisibleitems
        let isLazy: Bool
        switch event.type {
        case .leftMouseDragged,
             .rightMouseDragged,
             .otherMouseDragged,
             .mouseEntered,
             .mouseExited,
             .scrollWheel,
             .cursorUpdate,
             .keyDown,
             .mouseMoved:
            return
            
        case .keyUp,
             .flagsChanged:
            isLazy = true
            
        default:
            isLazy = false
        }
        
        // schedule validation with delay
        // -> A tiny delay makes sense:
        //      1. To wait for state change.
        //      2. To gather multiple events.
        if let timer = self.validationTimer, timer.isValid {
            timer.fireDate = Date(timeIntervalSinceNow: ValidationDelay.normal.rawValue)
        } else {
            let delay: ValidationDelay = isLazy ? .lazy : .normal
            self.validationTimer = Timer.scheduledTimer(timeInterval: delay.rawValue,
                                                        target: self,
                                                        selector: #selector(validateTouchBar(timer:)),
                                                        userInfo: nil,
                                                        repeats: false)
            self.validationTimer?.tolerance = 0.1 * delay.rawValue
        }
    }
    
}



// MARK: -

@available(macOS 10.12.2, *)
extension NSCustomTouchBarItem: NSValidatedUserInterfaceItem {
    
    /// validate item if content view is NSControl
    fileprivate func validate() {
        
        // validate content control
        guard
            let control = self.control,
            let action = control.action,
            let validator = NSApp.target(forAction: action, to: control.target, from: self) as AnyObject?
            else { return }
        
        // -> Casting from Any to AnyObject before putting it to `switch` statement is really important. Be careful if you wanna make a change. (2017-02-03 on SDK macOS 10.12)
        switch validator {
        case let validator as TouchBarItemValidations:
            control.isEnabled = validator.validateTouchBarItem(self)
        case let validator as NSUserInterfaceValidations:
            control.isEnabled = validator.validateUserInterfaceItem(self)
        default: break
        }
    }
    
    
    
    // MARK: Validated User Interface Item Protocol
    
    public var action: Selector? {
        
        return self.control?.action
    }
    
    
    public var tag: Int {
        
        return self.control?.tag ?? 0
    }
    
    
    
    // MARK: Private Methods
    
    private var control: NSControl? {
        
        return self.view as? NSControl
    }
    
}
