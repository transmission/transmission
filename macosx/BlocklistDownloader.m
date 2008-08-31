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
#import "BlocklistDownloaderViewController.h"
#import "NSApplicationAdditions.h"

#define LIST_URL @"http://download.m0k.org/transmission/files/level1.gz"
#define DESTINATION [NSTemporaryDirectory() stringByAppendingPathComponent: @"level1.gz"]

@interface BlocklistDownloader (Private)

- (id) initWithHandle: (tr_handle *) handle;
- (void) startDownload;
- (void) finishDownloadSuccess;

@end

@implementation BlocklistDownloader

BlocklistDownloader * fDownloader = nil;
+ (BlocklistDownloader *) downloader: (tr_handle *) handle
{
    if (!fDownloader)
    {
        fDownloader = [[BlocklistDownloader alloc] initWithHandle: handle];
        [fDownloader startDownload];
    }
    
    return fDownloader;
}

- (void) setViewController: (BlocklistDownloaderViewController *) viewController
{
    fViewController = viewController;
    if (fViewController)
    {
        #warning set actual status
        [fViewController setStatusStarting];
    }
}

- (void) dealloc
{
    [fDownload release];
    [super dealloc];
}

- (void) cancelDownload
{
    [fViewController setFinished];
    
    [fDownload cancel];
    
    fDownloader = nil;
    [self release];
}

- (void) download: (NSURLDownload *) download didReceiveResponse: (NSURLResponse *) response
{
    fCurrentSize = 0;
    fExpectedSize = [response expectedContentLength];
    
    [fViewController setStatusProgressForCurrentSize: fCurrentSize expectedSize: fExpectedSize];
}

- (void) download: (NSURLDownload *) download didReceiveDataOfLength: (NSUInteger) length
{
    fCurrentSize += length;
    [fViewController setStatusProgressForCurrentSize: fCurrentSize expectedSize: fExpectedSize];
}

- (void) download: (NSURLDownload *) download didFailWithError: (NSError *) error
{
    [fViewController setFailed: [error localizedDescription]];
    
    fDownloader = nil;
    [self release];
}

- (void) downloadDidFinish: (NSURLDownload *) download
{
    if ([NSApp isOnLeopardOrBetter])
        [self performSelectorInBackground: @selector(finishDownloadSuccess) withObject: nil];
    else
        [self finishDownloadSuccess];
}

@end

@implementation BlocklistDownloader (Private)

- (id) initWithHandle: (tr_handle *) handle
{
    if ((self = [super init]))
    {
        fHandle = handle;
    }
    
    return self;
}

- (void) startDownload
{
    NSURLRequest * request = [NSURLRequest requestWithURL: [NSURL URLWithString: LIST_URL]];
    
    fDownload = [[NSURLDownload alloc] initWithRequest: request delegate: self];
    [fDownload setDestination: DESTINATION allowOverwrite: YES];
}

- (void) finishDownloadSuccess
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    
    [fViewController setStatusProcessing];
    
    //process data
    tr_blocklistSetContent(fHandle, [DESTINATION UTF8String]);
    
    //delete downloaded file
    if ([NSApp isOnLeopardOrBetter])
        [[NSFileManager defaultManager] removeItemAtPath: DESTINATION error: NULL];
    else
        [[NSFileManager defaultManager] removeFileAtPath: DESTINATION handler: nil];
    
    [fViewController setFinished];
    
    [[NSUserDefaults standardUserDefaults] setObject: [NSDate date] forKey: @"BlocklistLastUpdate"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"BlocklistUpdated" object: nil];
    
    [pool release];
    
    fDownloader = nil;
    [self release];
}

@end
