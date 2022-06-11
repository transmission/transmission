// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/log.h>

#import "MessageWindowController.h"
#import "Controller.h"
#import "NSApplicationAdditions.h"
#import "NSMutableArrayAdditions.h"
#import "NSStringAdditions.h"

#define LEVEL_ERROR 0
#define LEVEL_WARN 1
#define LEVEL_INFO 2
#define LEVEL_DEBUG 3
#define LEVEL_TRACE 4

#define UPDATE_SECONDS 0.75

@interface MessageWindowController ()

@property(nonatomic) IBOutlet NSTableView* fMessageTable;

@property(nonatomic) IBOutlet NSPopUpButton* fLevelButton;
@property(nonatomic) IBOutlet NSButton* fSaveButton;
@property(nonatomic) IBOutlet NSButton* fClearButton;
@property(nonatomic) IBOutlet NSSearchField* fFilterField;

@property(nonatomic) NSMutableArray<NSDictionary*>* fMessages;
@property(nonatomic) NSMutableArray<NSDictionary*>* fDisplayedMessages;

@property(nonatomic, copy) NSDictionary<NSAttributedStringKey, id>* fAttributes;

@property(nonatomic) NSTimer* fTimer;

@property(nonatomic) NSLock* fLock;

- (void)resizeColumn;
- (BOOL)shouldIncludeMessageForFilter:(NSString*)filterString message:(NSDictionary*)message;
- (void)updateListForFilter;
- (NSString*)stringForMessage:(NSDictionary*)message;

@end

@implementation MessageWindowController

- (instancetype)init
{
    return [super initWithWindowNibName:@"MessageWindow"];
}

- (void)awakeFromNib
{
    NSWindow* window = self.window;
    window.frameAutosaveName = @"MessageWindowFrame";
    [window setFrameUsingName:@"MessageWindowFrame"];
    window.restorationClass = [self class];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(resizeColumn)
                                               name:NSTableViewColumnDidResizeNotification
                                             object:self.fMessageTable];

    [window setContentBorderThickness:NSMinY(self.fMessageTable.enclosingScrollView.frame) forEdge:NSMinYEdge];

    self.window.title = NSLocalizedString(@"Message Log", "Message window -> title");

    //set images and text for popup button items
    [self.fLevelButton itemAtIndex:LEVEL_ERROR].title = NSLocalizedString(@"Error", "Message window -> level string");
    [self.fLevelButton itemAtIndex:LEVEL_WARN].title = NSLocalizedString(@"Warn", "Message window -> level string");
    [self.fLevelButton itemAtIndex:LEVEL_INFO].title = NSLocalizedString(@"Info", "Message window -> level string");
    [self.fLevelButton itemAtIndex:LEVEL_DEBUG].title = NSLocalizedString(@"Debug", "Message window -> level string");
    [self.fLevelButton itemAtIndex:LEVEL_TRACE].title = NSLocalizedString(@"Trace", "Message window -> level string");

    CGFloat const levelButtonOldWidth = NSWidth(self.fLevelButton.frame);
    [self.fLevelButton sizeToFit];

    //set table column text
    [self.fMessageTable tableColumnWithIdentifier:@"Date"].headerCell.title = NSLocalizedString(@"Date", "Message window -> table column");
    [self.fMessageTable tableColumnWithIdentifier:@"Name"].headerCell.title = NSLocalizedString(@"Process", "Message window -> table column");
    [self.fMessageTable tableColumnWithIdentifier:@"Message"].headerCell.title = NSLocalizedString(@"Message", "Message window -> table column");

    //set and size buttons
    self.fSaveButton.title = [NSLocalizedString(@"Save", "Message window -> save button") stringByAppendingEllipsis];
    [self.fSaveButton sizeToFit];

    NSRect saveButtonFrame = self.fSaveButton.frame;
    saveButtonFrame.size.width += 10.0;
    saveButtonFrame.origin.x += NSWidth(self.fLevelButton.frame) - levelButtonOldWidth;
    self.fSaveButton.frame = saveButtonFrame;

    CGFloat const oldClearButtonWidth = self.fClearButton.frame.size.width;

    self.fClearButton.title = NSLocalizedString(@"Clear", "Message window -> save button");
    [self.fClearButton sizeToFit];

    NSRect clearButtonFrame = self.fClearButton.frame;
    clearButtonFrame.size.width = MAX(clearButtonFrame.size.width + 10.0, saveButtonFrame.size.width);
    clearButtonFrame.origin.x -= NSWidth(clearButtonFrame) - oldClearButtonWidth;
    self.fClearButton.frame = clearButtonFrame;

    [self.fFilterField.cell setPlaceholderString:NSLocalizedString(@"Filter", "Message window -> filter field")];
    NSRect filterButtonFrame = self.fFilterField.frame;
    filterButtonFrame.origin.x -= NSWidth(clearButtonFrame) - oldClearButtonWidth;
    self.fFilterField.frame = filterButtonFrame;

    self.fAttributes = [[[self.fMessageTable tableColumnWithIdentifier:@"Message"].dataCell attributedStringValue]
        attributesAtIndex:0
           effectiveRange:NULL];

    //select proper level in popup button
    switch ([NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"])
    {
    case TR_LOG_ERROR:
        [self.fLevelButton selectItemAtIndex:LEVEL_ERROR];
        break;
    case TR_LOG_WARN:
        [self.fLevelButton selectItemAtIndex:LEVEL_WARN];
        break;
    case TR_LOG_INFO:
        [self.fLevelButton selectItemAtIndex:LEVEL_INFO];
        break;
    case TR_LOG_DEBUG:
        [self.fLevelButton selectItemAtIndex:LEVEL_DEBUG];
        break;
    case TR_LOG_TRACE:
        [self.fLevelButton selectItemAtIndex:LEVEL_TRACE];
        break;
    default: //safety
        [NSUserDefaults.standardUserDefaults setInteger:TR_LOG_ERROR forKey:@"MessageLevel"];
        [self.fLevelButton selectItemAtIndex:LEVEL_ERROR];
    }

    self.fMessages = [[NSMutableArray alloc] init];
    self.fDisplayedMessages = [[NSMutableArray alloc] init];

    self.fLock = [[NSLock alloc] init];
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [_fTimer invalidate];
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    if (!self.fTimer)
    {
        self.fTimer = [NSTimer scheduledTimerWithTimeInterval:UPDATE_SECONDS target:self selector:@selector(updateLog:)
                                                     userInfo:nil
                                                      repeats:YES];
        [self updateLog:nil];
    }
}

- (void)windowWillClose:(id)sender
{
    [self.fTimer invalidate];
    self.fTimer = nil;
}

+ (void)restoreWindowWithIdentifier:(NSString*)identifier
                              state:(NSCoder*)state
                  completionHandler:(void (^)(NSWindow*, NSError*))completionHandler
{
    NSAssert1([identifier isEqualToString:@"MessageWindow"], @"Trying to restore unexpected identifier %@", identifier);

    NSWindow* window = ((Controller*)NSApp.delegate).messageWindowController.window;
    completionHandler(window, nil);
}

- (void)window:(NSWindow*)window didDecodeRestorableState:(NSCoder*)coder
{
    [self.fTimer invalidate];
    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:UPDATE_SECONDS target:self selector:@selector(updateLog:) userInfo:nil
                                                  repeats:YES];
    [self updateLog:nil];
}

- (void)updateLog:(NSTimer*)timer
{
    tr_log_message* messages;
    if ((messages = tr_logGetQueue()) == NULL)
    {
        return;
    }

    [self.fLock lock];

    static NSUInteger currentIndex = 0;

    NSScroller* scroller = self.fMessageTable.enclosingScrollView.verticalScroller;
    BOOL const shouldScroll = currentIndex == 0 || scroller.floatValue == 1.0 || scroller.hidden || scroller.knobProportion == 1.0;

    NSInteger const maxLevel = [NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"];
    NSString* filterString = self.fFilterField.stringValue;

    BOOL changed = NO;

    for (tr_log_message* currentMessage = messages; currentMessage != NULL; currentMessage = currentMessage->next)
    {
        NSString* name = currentMessage->name != NULL ? @(currentMessage->name) : NSProcessInfo.processInfo.processName;

        NSString* file = [(@(currentMessage->file)).lastPathComponent stringByAppendingFormat:@":%d", currentMessage->line];

        NSDictionary* message = @{
            @"Message" : @(currentMessage->message),
            @"Date" : [NSDate dateWithTimeIntervalSince1970:currentMessage->when],
            @"Index" : @(currentIndex++), //more accurate when sorting by date
            @"Level" : @(currentMessage->level),
            @"Name" : name,
            @"File" : file
        };
        [self.fMessages addObject:message];

        if (currentMessage->level <= maxLevel && [self shouldIncludeMessageForFilter:filterString message:message])
        {
            [self.fDisplayedMessages addObject:message];
            changed = YES;
        }
    }

    if (self.fMessages.count > TR_LOG_MAX_QUEUE_LENGTH)
    {
        NSUInteger const oldCount = self.fDisplayedMessages.count;

        NSIndexSet* removeIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fMessages.count - TR_LOG_MAX_QUEUE_LENGTH)];
        NSArray* itemsToRemove = [self.fMessages objectsAtIndexes:removeIndexes];

        [self.fMessages removeObjectsAtIndexes:removeIndexes];
        [self.fDisplayedMessages removeObjectsInArray:itemsToRemove];

        changed |= oldCount > self.fDisplayedMessages.count;
    }

    if (changed)
    {
        [self.fDisplayedMessages sortUsingDescriptors:self.fMessageTable.sortDescriptors];

        [self.fMessageTable reloadData];
        if (shouldScroll)
            [self.fMessageTable scrollRowToVisible:self.fMessageTable.numberOfRows - 1];
    }

    [self.fLock unlock];

    tr_logFreeQueue(messages);
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fDisplayedMessages.count;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)row
{
    NSString* ident = column.identifier;
    NSDictionary* message = self.fDisplayedMessages[row];

    if ([ident isEqualToString:@"Date"])
    {
        return message[@"Date"];
    }
    else if ([ident isEqualToString:@"Level"])
    {
        NSInteger const level = [message[@"Level"] integerValue];
        switch (level)
        {
        case TR_LOG_CRITICAL:
        case TR_LOG_ERROR:
            return [NSImage imageNamed:@"RedDotFlat"];

        case TR_LOG_WARN:
            return [NSImage imageNamed:@"OrangeDotFlat"];

        case TR_LOG_INFO:
            return [NSImage imageNamed:@"GreenDotFlat"];

        case TR_LOG_DEBUG:
            return [NSImage imageNamed:@"BlueDotFlat"];

        case TR_LOG_TRACE:
            return [NSImage imageNamed:@"PurpleDotFlat"];

        default:
            NSAssert1(NO, @"Unknown message log level: %ld", level);
            return nil;
        }
    }
    else if ([ident isEqualToString:@"Name"])
    {
        return message[@"Name"];
    }
    else
    {
        return message[@"Message"];
    }
}

#warning don't cut off end
- (CGFloat)tableView:(NSTableView*)tableView heightOfRow:(NSInteger)row
{
    NSString* message = self.fDisplayedMessages[row][@"Message"];

    NSTableColumn* column = [tableView tableColumnWithIdentifier:@"Message"];
    CGFloat const count = floorf([message sizeWithAttributes:self.fAttributes].width / column.width);

    return tableView.rowHeight * (count + 1.0);
}

- (void)tableView:(NSTableView*)tableView sortDescriptorsDidChange:(NSArray*)oldDescriptors
{
    [self.fDisplayedMessages sortUsingDescriptors:self.fMessageTable.sortDescriptors];
    [self.fMessageTable reloadData];
}

- (NSString*)tableView:(NSTableView*)tableView
        toolTipForCell:(NSCell*)cell
                  rect:(NSRectPointer)rect
           tableColumn:(NSTableColumn*)column
                   row:(NSInteger)row
         mouseLocation:(NSPoint)mouseLocation
{
    NSDictionary* message = self.fDisplayedMessages[row];
    return message[@"File"];
}

- (void)copy:(id)sender
{
    NSIndexSet* indexes = self.fMessageTable.selectedRowIndexes;
    NSMutableArray* messageStrings = [NSMutableArray arrayWithCapacity:indexes.count];

    for (NSDictionary* message in [self.fDisplayedMessages objectsAtIndexes:indexes])
    {
        [messageStrings addObject:[self stringForMessage:message]];
    }

    NSString* messageString = [messageStrings componentsJoinedByString:@"\n"];

    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    [pb writeObjects:@[ messageString ]];
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL action = menuItem.action;

    if (action == @selector(copy:))
    {
        return self.fMessageTable.numberOfSelectedRows > 0;
    }

    return YES;
}

- (void)changeLevel:(id)sender
{
    NSInteger level;
    switch (self.fLevelButton.indexOfSelectedItem)
    {
    case LEVEL_ERROR:
        level = TR_LOG_ERROR;
        break;
    case LEVEL_WARN:
        level = TR_LOG_WARN;
        break;
    case LEVEL_INFO:
        level = TR_LOG_INFO;
        break;
    case LEVEL_DEBUG:
        level = TR_LOG_DEBUG;
        break;
    case LEVEL_TRACE:
        level = TR_LOG_TRACE;
        break;
    default:
        NSAssert1(NO, @"Unknown message log level: %ld", [self.fLevelButton indexOfSelectedItem]);
        level = TR_LOG_INFO;
    }

    if ([NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"] == level)
    {
        return;
    }

    [NSUserDefaults.standardUserDefaults setInteger:level forKey:@"MessageLevel"];

    [self.fLock lock];

    [self updateListForFilter];

    [self.fLock unlock];
}

- (void)changeFilter:(id)sender
{
    [self.fLock lock];

    [self updateListForFilter];

    [self.fLock unlock];
}

- (void)clearLog:(id)sender
{
    [self.fLock lock];

    [self.fMessages removeAllObjects];

    [self.fMessageTable beginUpdates];
    [self.fMessageTable removeRowsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fDisplayedMessages.count)]
                              withAnimation:NSTableViewAnimationSlideLeft];

    [self.fDisplayedMessages removeAllObjects];

    [self.fMessageTable endUpdates];

    [self.fLock unlock];
}

- (void)writeToFile:(id)sender
{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.allowedFileTypes = @[ @"txt" ];
    panel.canSelectHiddenExtension = YES;

    panel.nameFieldStringValue = NSLocalizedString(@"untitled", "Save log panel -> default file name");

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK)
        {
            //make the array sorted by date
            NSSortDescriptor* descriptor = [NSSortDescriptor sortDescriptorWithKey:@"Index" ascending:YES];
            NSArray* descriptors = @[ descriptor ];
            NSArray* sortedMessages = [self.fDisplayedMessages sortedArrayUsingDescriptors:descriptors];

            //create the text to output
            NSMutableArray* messageStrings = [NSMutableArray arrayWithCapacity:sortedMessages.count];
            for (NSDictionary* message in sortedMessages)
            {
                [messageStrings addObject:[self stringForMessage:message]];
            }

            NSString* fileString = [messageStrings componentsJoinedByString:@"\n"];

            if (![fileString writeToFile:panel.URL.path atomically:YES encoding:NSUTF8StringEncoding error:nil])
            {
                NSAlert* alert = [[NSAlert alloc] init];
                [alert addButtonWithTitle:NSLocalizedString(@"OK", "Save log alert panel -> button")];
                alert.messageText = NSLocalizedString(@"Log Could Not Be Saved", "Save log alert panel -> title");
                alert.informativeText = [NSString
                    stringWithFormat:NSLocalizedString(@"There was a problem creating the file \"%@\".", "Save log alert panel -> message"),
                                     panel.URL.path.lastPathComponent];
                alert.alertStyle = NSAlertStyleWarning;

                [alert runModal];
            }
        }
    }];
}

#pragma mark - Private

- (void)resizeColumn
{
    [self.fMessageTable
        noteHeightOfRowsWithIndexesChanged:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fMessageTable.numberOfRows)]];
}

- (BOOL)shouldIncludeMessageForFilter:(NSString*)filterString message:(NSDictionary*)message
{
    if ([filterString isEqualToString:@""])
    {
        return YES;
    }

    NSStringCompareOptions const searchOptions = NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch;
    return [message[@"Name"] rangeOfString:filterString options:searchOptions].location != NSNotFound ||
        [message[@"Message"] rangeOfString:filterString options:searchOptions].location != NSNotFound;
}

- (void)updateListForFilter
{
    NSInteger const level = [NSUserDefaults.standardUserDefaults integerForKey:@"MessageLevel"];
    NSString* filterString = self.fFilterField.stringValue;

    NSIndexSet* indexes = [self.fMessages
        indexesOfObjectsWithOptions:NSEnumerationConcurrent passingTest:^BOOL(NSDictionary* message, NSUInteger idx, BOOL* stop) {
            return [message[@"Level"] integerValue] <= level && [self shouldIncludeMessageForFilter:filterString message:message];
        }];

    NSArray* tempMessages = [[self.fMessages objectsAtIndexes:indexes] sortedArrayUsingDescriptors:self.fMessageTable.sortDescriptors];

    [self.fMessageTable beginUpdates];

    //figure out which rows were added/moved
    NSUInteger currentIndex = 0, totalCount = 0;
    NSMutableArray* itemsToAdd = [NSMutableArray array];
    NSMutableIndexSet* itemsToAddIndexes = [NSMutableIndexSet indexSet];

    for (NSDictionary* message in tempMessages)
    {
        NSUInteger const previousIndex = [self.fDisplayedMessages
            indexOfObject:message
                  inRange:NSMakeRange(currentIndex, self.fDisplayedMessages.count - currentIndex)];
        if (previousIndex == NSNotFound)
        {
            [itemsToAdd addObject:message];
            [itemsToAddIndexes addIndex:totalCount];
        }
        else
        {
            if (previousIndex != currentIndex)
            {
                [self.fDisplayedMessages moveObjectAtIndex:previousIndex toIndex:currentIndex];
                [self.fMessageTable moveRowAtIndex:previousIndex toIndex:currentIndex];
            }
            ++currentIndex;
        }

        ++totalCount;
    }

    //remove trailing items - those are the unused
    if (currentIndex < self.fDisplayedMessages.count)
    {
        NSRange const removeRange = NSMakeRange(currentIndex, self.fDisplayedMessages.count - currentIndex);
        [self.fDisplayedMessages removeObjectsInRange:removeRange];
        [self.fMessageTable removeRowsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:removeRange]
                                  withAnimation:NSTableViewAnimationSlideDown];
    }

    //add new items
    [self.fDisplayedMessages insertObjects:itemsToAdd atIndexes:itemsToAddIndexes];
    [self.fMessageTable insertRowsAtIndexes:itemsToAddIndexes withAnimation:NSTableViewAnimationSlideUp];

    [self.fMessageTable endUpdates];

    NSAssert2([self.fDisplayedMessages isEqualToArray:tempMessages], @"Inconsistency between message arrays! %@ %@", self.fDisplayedMessages, tempMessages);
}

- (NSString*)stringForMessage:(NSDictionary*)message
{
    NSString* levelString;
    NSInteger const level = [message[@"Level"] integerValue];
    switch (level)
    {
    case TR_LOG_ERROR:
        levelString = NSLocalizedString(@"Error", "Message window -> level");
        break;
    case TR_LOG_INFO:
        levelString = NSLocalizedString(@"Info", "Message window -> level");
        break;
    case TR_LOG_DEBUG:
        levelString = NSLocalizedString(@"Debug", "Message window -> level");
        break;
    default:
        NSAssert1(NO, @"Unknown message log level: %ld", level);
        levelString = @"?";
    }

    return [NSString
        stringWithFormat:@"%@ %@ [%@] %@: %@", message[@"Date"], message[@"File"], levelString, message[@"Name"], message[@"Message"], nil];
}

@end
