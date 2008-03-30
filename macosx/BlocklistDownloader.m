/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008 Transmission authors and contributors
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

#import "BlocklistDownloader.h"
#import "PrefsController.h"
#import "NSStringAdditions.h"
#import "NSApplicationAdditions.h"

#define LIST_URL @"http://download.m0k.org/transmission/files/level1.gz"
#define DESTINATION [NSTemporaryDirectory() stringByAppendingPathComponent: @"level1.gz"]

@interface BlocklistDownloader (Private)

- (id) initWithPrefsController: (PrefsController *) prefsController withHandle: (tr_handle *) handle;
- (void) startDownload;
- (void) updateProcessString;
- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info;

@end

@implementation BlocklistDownloader

+ (id) downloadWithPrefsController: (PrefsController *) prefsController withHandle: (tr_handle *) handle
{
    BlocklistDownloader * downloader = [[BlocklistDownloader alloc] initWithPrefsController: prefsController withHandle: handle];
    [downloader startDownload];
}

- (void) dealloc
{
    [fDownload release];
    [super dealloc];
}

- (void) cancelDownload: (id) sender
{
    [fDownload release];
    fDownload = nil;
    
    [NSApp endSheet: fStatusWindow];
    [fStatusWindow orderOut: self];
    [self release];
}

- (void) download: (NSURLDownload *) download didReceiveResponse: (NSURLResponse *) response
{
    fCurrentSize = 0;
    fExpectedSize = [response expectedContentLength];
    
    //change from indeterminate to progress
    
    [fProgressBar setIndeterminate: fExpectedSize == NSURLResponseUnknownLength];
    [self updateProcessString];
}

- (void) download: (NSURLDownload *) download didReceiveDataOfLength: (NSUInteger) length
{
    fCurrentSize += length;
    [self updateProcessString];
}

- (void) download: (NSURLDownload *) download didFailWithError: (NSError *) error
{
    [fDownload release];
    fDownload = nil;
    
    [fProgressBar setHidden: YES];
    
    [NSApp endSheet: fStatusWindow];
    [fStatusWindow orderOut: self];
    
    NSAlert * alert = [[[NSAlert alloc] init] autorelease];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Blocklist -> button")];
    [alert setMessageText: NSLocalizedString(@"Download of the blocklist failed.", "Blocklist -> message")];
    [alert setAlertStyle: NSWarningAlertStyle];
    
    [alert setInformativeText: [NSString stringWithFormat: @"%@ - %@", NSLocalizedString(@"Error", "Blocklist -> message"),
        [error localizedDescription]]];
    
    [alert beginSheetModalForWindow: [fPrefsController window] modalDelegate: self
        didEndSelector: @selector(failureSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) downloadDidFinish: (NSURLDownload *) download
{
    [fDownload release];
    fDownload = nil;
    
    //display progress as 100%
    fCurrentSize = fExpectedSize;
    [self updateProcessString];
    
    //process data
    tr_blocklistSetContent(fHandle, [DESTINATION UTF8String]);
    
    //delete downloaded file
    if ([NSApp isOnLeopardOrBetter])
        [[NSFileManager defaultManager] removeItemAtPath: DESTINATION error: NULL];
    else
        [[NSFileManager defaultManager] removeFileAtPath: DESTINATION handler: nil];
    
    [fPrefsController updateBlocklistFields];
    
    [NSApp endSheet: fStatusWindow];
    [fStatusWindow orderOut: self];
    [self release];
}

@end

@implementation BlocklistDownloader (Private)

- (id) initWithPrefsController: (PrefsController *) prefsController withHandle: (tr_handle *) handle
{
    if ((self = [super init]))
    {
        fPrefsController = prefsController;
        fHandle = handle;
    }
    
    return self;
}

- (void) startDownload
{
    NSURLRequest * request = [NSURLRequest requestWithURL: [NSURL URLWithString: LIST_URL]];
    
    fDownload = [[NSURLDownload alloc] initWithRequest: request delegate: self];
    [fDownload setDestination: DESTINATION allowOverwrite: YES];
    
    [NSBundle loadNibNamed: @"BlocklistStatusWindow" owner: self];
    
    [fButton setTitle: NSLocalizedString(@"Cancel", "Blocklist -> cancel button")];
    [fTextField setStringValue: [NSLocalizedString(@"Connecting to site", "Blocklist -> message") stringByAppendingEllipsis]];
    
    [NSApp beginSheet: fStatusWindow modalForWindow: [fPrefsController window] modalDelegate: nil didEndSelector: nil contextInfo: nil];
}

- (void) updateProcessString
{
    NSString * string = NSLocalizedString(@"Downloading blocklist", "Blocklist -> message");
    if (fExpectedSize != NSURLResponseUnknownLength)
    {
        double progress = (double)fCurrentSize / fExpectedSize;
        string = [string stringByAppendingString: [NSString localizedStringWithFormat: @" (%.1f%%)", 100.0 * progress]];
        [fProgressBar setDoubleValue: progress];
    }
    
    [fTextField setStringValue: string];
}

- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info
{
    [[alert window] orderOut: nil];
    [self release];
}

@end
