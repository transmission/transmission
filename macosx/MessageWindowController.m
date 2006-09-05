/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "MessageWindowController.h"
#import <transmission.h>

#define LEVEL_ERROR 0
#define LEVEL_INFO  1
#define LEVEL_DEBUG 2

#define UPDATE_SECONDS  0.6
#define MAX_LINES       1000

@interface MessageWindowController (Private)

- (void) setDebugWarningHidden: (BOOL) hide;

@end

@implementation MessageWindowController

- (id) initWithWindowNibName: (NSString *) name
{
    if ((self = [super initWithWindowNibName: name]))
    {
        NSMutableParagraphStyle * paragraph = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraph setHeadIndent: 20.0];
        
        fAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                        [NSFont fontWithName: @"Monaco" size: 10], NSFontAttributeName,
                        paragraph, NSParagraphStyleAttributeName, nil];
        [paragraph release];
        
        fLines = 0;
        
        fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: self
                    selector: @selector(updateLog:) userInfo: nil repeats: YES];
        
        [[self window] update]; //make sure nib is loaded right away
    }
    return self;
}

- (void) dealloc
{
    [fTimer invalidate];
    [fAttributes release];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    int level = [[NSUserDefaults standardUserDefaults] integerForKey: @"MessageLevel"];
    if (level == TR_MSG_ERR)
        [fLevelButton selectItemAtIndex: LEVEL_ERROR];
    else if (level == TR_MSG_INF)
        [fLevelButton selectItemAtIndex: LEVEL_INFO];
    else if (level == TR_MSG_DBG)
        [fLevelButton selectItemAtIndex: LEVEL_DEBUG];
    else
    {
        level = TR_MSG_ERR;
        [fLevelButton selectItemAtIndex: LEVEL_ERROR];
        [[NSUserDefaults standardUserDefaults] setInteger: level forKey: @"MessageLevel"];
    }
    
    [self setDebugWarningHidden: level != TR_MSG_DBG];
    
    tr_setMessageLevel(level);
    tr_setMessageQueuing(1);
}

- (void) updateLog: (NSTimer *) timer
{
    tr_msg_list_t * messages, * currentMessage;
    if ((messages = tr_getQueuedMessages()) == NULL)
        return;
    
    //keep scrolled to bottom if already at bottom or there is no scroll bar yet
    NSScroller * scroller = [fScrollView verticalScroller];
    BOOL shouldScroll = [scroller floatValue] == 1.0 || [scroller isHidden] || [scroller knobProportion] == 1.0;
    
    NSString * levelString, * dateString, * messageString;
    for (currentMessage = messages; currentMessage != NULL; currentMessage = currentMessage->next)
    {
        int level = currentMessage->level;
        if (level == TR_MSG_ERR)
            levelString = @"ERR";
        else if (level == TR_MSG_INF)
            levelString = @"INF";
        else if (level == TR_MSG_DBG)
            levelString = @"DBG";
        else
            levelString = @"???";
        
        dateString = [[NSDate dateWithTimeIntervalSince1970: currentMessage->when]
                            descriptionWithCalendarFormat: @"%1m/%d %H:%M:%S" timeZone: nil locale: nil];
        messageString = [NSString stringWithFormat: @"%s%@ %@ %s",
                            fLines > 0 ? "\n" : "", dateString, levelString, currentMessage->message];
        
        //remove the first line if at max number of lines
        if (fLines == MAX_LINES)
        {
            unsigned int loc = [[fTextView string] rangeOfString: @"\n"].location;
            if (loc != NSNotFound)
                [[fTextView textStorage] deleteCharactersInRange: NSMakeRange(0, loc + 1)];
        }
        else
            fLines++;
        
        [[fTextView textStorage] appendAttributedString: [[[NSAttributedString alloc] initWithString:
                                        messageString attributes: fAttributes] autorelease]];
    }
    
    tr_freeMessageList(messages);
    
    if (shouldScroll)
        [fTextView scrollRangeToVisible: NSMakeRange([[fTextView string] length], 0)];
}

- (void) changeLevel: (id) sender
{
    [self updateLog: nil];
    
    int selection = [fLevelButton indexOfSelectedItem], level;
    if (selection == LEVEL_INFO)
        level = TR_MSG_INF;
    else if (selection == LEVEL_DEBUG)
        level = TR_MSG_DBG;
    else
        level = TR_MSG_ERR;
    
    [self setDebugWarningHidden: level != TR_MSG_DBG];
    
    tr_setMessageLevel(level);
    [[NSUserDefaults standardUserDefaults] setInteger: level forKey: @"MessageLevel"];
}

- (void) clearLog: (id) sender
{
    [fTextView setString: @""];
    fLines = 0;
}

- (void) writeToFile: (id) sender
{
    NSString * string = [[fTextView string] retain];
    
    NSSavePanel * panel = [NSSavePanel savePanel];
    [panel setRequiredFileType: @"txt"];
    [panel setCanSelectHiddenExtension: YES];
    
    [panel beginSheetForDirectory: nil file: @"untitled" modalForWindow: [self window] modalDelegate: self
        didEndSelector: @selector(writeToFileSheetClosed:returnCode:contextInfo:) contextInfo: string];
}

- (void) writeToFileSheetClosed: (NSSavePanel *) panel returnCode: (int) code contextInfo: (NSString *) string
{
    if (code == NSOKButton)
    {
        if (![string writeToFile: [panel filename] atomically: YES encoding: NSUTF8StringEncoding error: nil])
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: @"OK"];
            [alert setMessageText: [NSString stringWithFormat: @"Log Could Not Be Saved"]];
            [alert setInformativeText: [NSString stringWithFormat: 
                    @"There was a problem creating the file \"%@\".", [[panel filename] lastPathComponent]]];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
        }
    }
    
    [string release];
}

- (void) setDebugWarningHidden: (BOOL) hide
{
    [fDebugWarningField setHidden: hide];
    [fDebugWarningIcon setHidden: hide];
}

@end
