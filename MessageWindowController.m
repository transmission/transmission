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

@interface MessageWindowController (Private)

MessageWindowController * selfReference; //I'm not sure why I can't use self directly

@end

@implementation MessageWindowController

- (id) initWithWindowNibName: (NSString *) name
{
    if ((self = [super initWithWindowNibName: name]))
    {
        selfReference = self;
        
        [[self window] update]; //make sure nib is loaded right away
        
        fLock = [[NSLock alloc] init];
    }
    return self;
}

- (void) dealloc
{
    [fLock release];
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
    
    tr_setMessageLevel(level);
    tr_setMessageFunction(addMessage);
}

void addMessage(int level, const char * message)
{
    [selfReference addMessageLevel: level message: message];
}

- (void) addMessageLevel: (int) level message: (const char *) message
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

    NSString * levelString;
    
    if (level == TR_MSG_ERR)
        levelString = @"ERR";
    else if (level == TR_MSG_INF)
        levelString = @"INF";
    else if (level == TR_MSG_DBG)
        levelString = @"DBG";
    else
        levelString = @"???";
    
    NSAttributedString * messageString = [[[NSAttributedString alloc] initWithString:
                            [NSString stringWithFormat: @"%@: %s\n", levelString, message]] autorelease];
    
    [fLock lock];
    [[fTextView textStorage] appendAttributedString: messageString];
    [fLock unlock];
    
    [pool release];
}

- (void) changeLevel: (id) sender
{
    int selection = [fLevelButton indexOfSelectedItem], level;
    if (selection == LEVEL_INFO)
        level = TR_MSG_INF;
    else if (selection == LEVEL_DEBUG)
        level = TR_MSG_DBG;
    else
        level = TR_MSG_ERR;
    
    tr_setMessageLevel(level);
    [[NSUserDefaults standardUserDefaults] setInteger: level forKey: @"MessageLevel"];
}

- (void) clearLog: (id) sender
{
    [fTextView setString: @""];
}

@end
