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

- (id) initWithPrefsController: (PrefsController *) prefsController;
- (void) startDownload;
- (void) updateProcessString;
- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info;

@end

@implementation BlocklistDownloader

+ (id) downloadWithPrefsController: (PrefsController *) prefsController
{
    BlocklistDownloader * downloader = [[BlocklistDownloader alloc] initWithPrefsController: prefsController];
    [downloader startDownload];
}

- (void) awakeFromNib
{
    [fButton setTitle: NSLocalizedString(@"Cancel", "Blocklist -> cancel button")];
    [fTextField setStringValue: [NSLocalizedString(@"Connecting to site", "Blocklist -> message") stringByAppendingEllipsis]];
    
    [fProgressBar setUsesThreadedAnimation: YES];
    [fProgressBar startAnimation: self];
}

- (void) dealloc
{
    [fDownload release];
    [super dealloc];
}

- (void) cancelDownload: (id) sender
{
    [fDownload cancel];
    
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
    //change to indeterminate while processing
    [fProgressBar setIndeterminate: YES];
    [fProgressBar startAnimation: self];
    
    [fTextField setStringValue: [NSLocalizedString(@"Processing blocklist", "Blocklist -> message") stringByAppendingEllipsis]];
    [fButton setEnabled: NO];
    [fStatusWindow display]; //force window to be updated
    
    //process data
    tr_blocklistSetContent([fPrefsController handle], [DESTINATION UTF8String]);
    
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

- (id) initWithPrefsController: (PrefsController *) prefsController
{
    if ((self = [super init]))
    {
        fPrefsController = prefsController;
    }
    
    return self;
}

- (void) startDownload
{
    //load window and show as sheet
    [NSBundle loadNibNamed: @"BlocklistStatusWindow" owner: self];
    [NSApp beginSheet: fStatusWindow modalForWindow: [fPrefsController window] modalDelegate: nil didEndSelector: nil contextInfo: nil];
    
    //start the download
    NSURLRequest * request = [NSURLRequest requestWithURL: [NSURL URLWithString: LIST_URL]];
    
    fDownload = [[NSURLDownload alloc] initWithRequest: request delegate: self];
    [fDownload setDestination: DESTINATION allowOverwrite: YES];
}

- (void) updateProcessString
{
    NSString * string = NSLocalizedString(@"Downloading blocklist", "Blocklist -> message");
    if (fExpectedSize != NSURLResponseUnknownLength)
    {
        NSString * substring = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@", "Blocklist -> message"),
                                [NSString stringForFileSize: fCurrentSize], [NSString stringForFileSize: fExpectedSize]];
        string = [string stringByAppendingFormat: @" (%@)",  substring];
        [fProgressBar setDoubleValue: (double)fCurrentSize / fExpectedSize];
    }
    
    [fTextField setStringValue: string];
}

- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info
{
    [[alert window] orderOut: nil];
    [self release];
}

@end
