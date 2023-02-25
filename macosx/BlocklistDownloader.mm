// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BlocklistDownloader.h"
#import "BlocklistDownloaderViewController.h"
#import "BlocklistScheduler.h"
#import "Controller.h"

@interface BlocklistDownloader ()

@property(nonatomic) NSURLSession* fSession;
@property(nonatomic) NSUInteger fCurrentSize;
@property(nonatomic) long long fExpectedSize;
@property(nonatomic) blocklistDownloadState fState;

@end

@implementation BlocklistDownloader

BlocklistDownloader* fBLDownloader = nil;

+ (BlocklistDownloader*)downloader
{
    if (!fBLDownloader)
    {
        fBLDownloader = [[BlocklistDownloader alloc] init];
        [fBLDownloader startDownload];
    }

    return fBLDownloader;
}

+ (BOOL)isRunning
{
    return fBLDownloader != nil;
}

- (void)setViewController:(BlocklistDownloaderViewController*)viewController
{
    _viewController = viewController;
    if (_viewController)
    {
        switch (self.fState)
        {
        case BLOCKLIST_DL_START:
            [_viewController setStatusStarting];
            break;
        case BLOCKLIST_DL_DOWNLOADING:
            [_viewController setStatusProgressForCurrentSize:self.fCurrentSize expectedSize:self.fExpectedSize];
            break;
        case BLOCKLIST_DL_PROCESSING:
            [_viewController setStatusProcessing];
            break;
        }
    }
}

- (void)cancelDownload
{
    [_viewController setFinished];

    [self.fSession invalidateAndCancel];

    [BlocklistScheduler.scheduler updateSchedule];

    fBLDownloader = nil;
}

- (void)URLSession:(NSURLSession*)session
                 downloadTask:(NSURLSessionDownloadTask*)downloadTask
                 didWriteData:(int64_t)bytesWritten
            totalBytesWritten:(int64_t)totalBytesWritten
    totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite
{
    dispatch_async(dispatch_get_main_queue(), ^{
        self.fState = BLOCKLIST_DL_DOWNLOADING;

        self.fCurrentSize = totalBytesWritten;
        self.fExpectedSize = totalBytesExpectedToWrite;

        [self.viewController setStatusProgressForCurrentSize:self.fCurrentSize expectedSize:self.fExpectedSize];
    });
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (error)
        {
            [self.viewController setFailed:error.localizedDescription];
        }

        [NSUserDefaults.standardUserDefaults setObject:[NSDate date] forKey:@"BlocklistNewLastUpdate"];
        [BlocklistScheduler.scheduler updateSchedule];

        [self.fSession finishTasksAndInvalidate];
        fBLDownloader = nil;
    });
}

- (void)URLSession:(NSURLSession*)session
                 downloadTask:(NSURLSessionDownloadTask*)downloadTask
    didFinishDownloadingToURL:(NSURL*)location
{
    self.fState = BLOCKLIST_DL_PROCESSING;

    dispatch_async(dispatch_get_main_queue(), ^{
        [self.viewController setStatusProcessing];
    });

    NSString* filename = downloadTask.response.suggestedFilename;
    if (filename == nil)
    {
        filename = @"transmission-blocklist.tmp";
    }

    NSString* tempFile = [NSTemporaryDirectory() stringByAppendingPathComponent:filename];
    NSString* blocklistFile = [NSTemporaryDirectory() stringByAppendingPathComponent:@"transmission-blocklist"];

    [NSFileManager.defaultManager moveItemAtPath:location.path toPath:tempFile error:nil];

    if ([@"text/plain" isEqualToString:downloadTask.response.MIMEType])
    {
        blocklistFile = tempFile;
    }
    else
    {
        [self decompressFrom:[NSURL fileURLWithPath:tempFile] to:[NSURL fileURLWithPath:blocklistFile] error:nil];
        [NSFileManager.defaultManager removeItemAtPath:tempFile error:nil];
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        auto const count = tr_blocklistSetContent(((Controller*)NSApp.delegate).sessionHandle, blocklistFile.UTF8String);

        //delete downloaded file
        [NSFileManager.defaultManager removeItemAtPath:blocklistFile error:nil];

        if (count > 0)
        {
            [self.viewController setFinished];
        }
        else
        {
            [self.viewController
                setFailed:NSLocalizedString(@"The specified blocklist file did not contain any valid rules.", "blocklist fail message")];
        }

        //update last updated date for schedule
        NSDate* date = [NSDate date];
        [NSUserDefaults.standardUserDefaults setObject:date forKey:@"BlocklistNewLastUpdate"];
        [NSUserDefaults.standardUserDefaults setObject:date forKey:@"BlocklistNewLastUpdateSuccess"];
        [BlocklistScheduler.scheduler updateSchedule];

        [NSNotificationCenter.defaultCenter postNotificationName:@"BlocklistUpdated" object:nil];

        [self.fSession finishTasksAndInvalidate];
        fBLDownloader = nil;
    });
}

#pragma mark - Private

- (void)startDownload
{
    self.fState = BLOCKLIST_DL_START;

    self.fSession = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration delegate:self
                                             delegateQueue:nil];

    [BlocklistScheduler.scheduler cancelSchedule];

    NSString* urlString = [NSUserDefaults.standardUserDefaults stringForKey:@"BlocklistURL"];
    if (!urlString)
    {
        urlString = @"";
    }
    else if (![urlString isEqualToString:@""] && [urlString rangeOfString:@"://"].location == NSNotFound)
    {
        urlString = [@"https://" stringByAppendingString:urlString];
    }

    NSURLSessionDownloadTask* task = [self.fSession downloadTaskWithURL:[NSURL URLWithString:urlString]];
    [task resume];
}

- (BOOL)decompressFrom:(NSURL*)file to:(NSURL*)destination error:(NSError**)error
{
    if ([self untarFrom:file to:destination])
    {
        return YES;
    }

    if ([self unzipFrom:file to:destination])
    {
        return YES;
    }

    if ([self gunzipFrom:file to:destination])
    {
        return YES;
    }

    // If it doesn't look like archive just copy it to destination
    else
    {
        return [NSFileManager.defaultManager copyItemAtURL:file toURL:destination error:error];
    }
}

- (BOOL)untarFrom:(NSURL*)file to:(NSURL*)destination
{
    // We need to check validity of archive before listing or unpacking.
    NSTask* tarListCheck = [[NSTask alloc] init];

    tarListCheck.launchPath = @"/usr/bin/tar";
    tarListCheck.arguments = @[ @"--list", @"--file", file.path ];
    tarListCheck.standardOutput = nil;
    tarListCheck.standardError = nil;

    @try
    {
        [tarListCheck launch];
        [tarListCheck waitUntilExit];

        if (tarListCheck.terminationStatus != 0)
        {
            return NO;
        }
    }
    @catch (NSException* exception)
    {
        return NO;
    }

    NSTask* tarList = [[NSTask alloc] init];

    tarList.launchPath = @"/usr/bin/tar";
    tarList.arguments = @[ @"--list", @"--file", file.path ];

    NSPipe* pipe = [[NSPipe alloc] init];
    tarList.standardOutput = pipe;
    tarList.standardError = nil;

    NSString* filename;

    @try
    {
        [tarList launch];
        [tarList waitUntilExit];

        if (tarList.terminationStatus != 0)
        {
            return NO;
        }

        NSData* data = [pipe.fileHandleForReading readDataToEndOfFile];

        NSString* output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];

        filename = [output componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet].firstObject;
    }
    @catch (NSException* exception)
    {
        return NO;
    }

    // It's a directory, skip
    if ([filename hasSuffix:@"/"])
    {
        return NO;
    }

    NSURL* destinationDir = destination.URLByDeletingLastPathComponent;

    NSTask* untar = [[NSTask alloc] init];
    untar.launchPath = @"/usr/bin/tar";
    untar.currentDirectoryPath = destinationDir.path;
    untar.arguments = @[ @"--extract", @"--file", file.path, filename ];

    @try
    {
        [untar launch];
        [untar waitUntilExit];

        if (untar.terminationStatus != 0)
        {
            return NO;
        }
    }
    @catch (NSException* exception)
    {
        return NO;
    }

    NSURL* result = [destinationDir URLByAppendingPathComponent:filename];

    [NSFileManager.defaultManager moveItemAtURL:result toURL:destination error:nil];
    return YES;
}

- (BOOL)gunzipFrom:(NSURL*)file to:(NSURL*)destination
{
    NSURL* destinationDir = destination.URLByDeletingLastPathComponent;

    NSTask* gunzip = [[NSTask alloc] init];
    gunzip.launchPath = @"/usr/bin/gunzip";
    gunzip.currentDirectoryPath = destinationDir.path;
    gunzip.arguments = @[ @"--keep", @"--force", file.path ];

    @try
    {
        [gunzip launch];
        [gunzip waitUntilExit];

        if (gunzip.terminationStatus != 0)
        {
            return NO;
        }
    }
    @catch (NSException* exception)
    {
        return NO;
    }

    NSURL* result = file.URLByDeletingPathExtension;

    [NSFileManager.defaultManager moveItemAtURL:result toURL:destination error:nil];
    return YES;
}

- (BOOL)unzipFrom:(NSURL*)file to:(NSURL*)destination
{
    NSTask* zipinfo = [[NSTask alloc] init];
    zipinfo.launchPath = @"/usr/bin/zipinfo";
    zipinfo.arguments = @[
        @"-1", /* just the filename */
        file.path /* source zip file */
    ];
    NSPipe* pipe = [[NSPipe alloc] init];
    zipinfo.standardOutput = pipe;
    zipinfo.standardError = nil;

    NSString* filename;

    @try
    {
        [zipinfo launch];
        [zipinfo waitUntilExit];

        if (zipinfo.terminationStatus != 0)
        {
            return NO;
        }

        NSData* data = [pipe.fileHandleForReading readDataToEndOfFile];

        NSString* output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];

        filename = [output componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet].firstObject;
    }
    @catch (NSException* exception)
    {
        return NO;
    }

    // It's a directory, skip
    if ([filename hasSuffix:@"/"])
    {
        return NO;
    }

    NSURL* destinationDir = destination.URLByDeletingLastPathComponent;

    NSTask* unzip = [[NSTask alloc] init];
    unzip.launchPath = @"/usr/bin/unzip";
    unzip.currentDirectoryPath = destinationDir.path;
    unzip.arguments = @[ file.path, filename ];

    @try
    {
        [unzip launch];
        [unzip waitUntilExit];

        if (unzip.terminationStatus != 0)
        {
            return NO;
        }
    }
    @catch (NSException* exception)
    {
        return NO;
    }

    NSURL* result = [destinationDir URLByAppendingPathComponent:filename];

    [NSFileManager.defaultManager moveItemAtURL:result toURL:destination error:nil];
    return YES;
}

@end
