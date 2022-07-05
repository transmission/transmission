// This file Copyright © 2010-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PreviewViewController.h"

#include <libtransmission/error.h>
#include <libtransmission/torrent-metainfo.h>

#import "NSStringAdditions.h"

#import <QuickLookUI/QLPreviewingController.h>

@interface PreviewViewController ()<QLPreviewingController>

#pragma mark - IBOutlet
@property(nonatomic, weak) IBOutlet NSImageView* fileIconImage;
@property(nonatomic, weak) IBOutlet NSTextField* nameField;
@property(nonatomic, weak) IBOutlet NSTextField* totalSizeField;
@property(nonatomic, weak) IBOutlet NSTextField* filesField;
@property(nonatomic, weak) IBOutlet NSTextField* trackersField;

@end

#pragma mark -

@implementation PreviewViewController

#pragma mark - NSViewController

- (NSString*)nibName
{
    return @"PreviewViewController";
}

#pragma mark - QLPreviewingController

- (void)preparePreviewOfFileAtURL:(NSURL*)url completionHandler:(void (^)(NSError* _Nullable))handler
{
    //try to parse the torrent file
    auto metainfo = tr_torrent_metainfo{};
    tr_error error{};
    if (!metainfo.parse_torrent_file(url.path.UTF8String, nullptr, &error))
    {
        NSError* err = nil;
        if (error)
        {
            err = [NSError errorWithDomain:@"TransmissionQuickLookAppexError"
                                      code:error.code()
                                  userInfo:@{ NSLocalizedDescriptionKey : @(std::data(error.message())) }];
        }
        handler(err);
        return;
    }

    NSString* name = @(metainfo.name().c_str());
    self.nameField.stringValue = name;

    NSString* fileSizeString = [NSString stringForFileSize:metainfo.total_size()];
    self.totalSizeField.stringValue = fileSizeString;

    auto const& announce_list = metainfo.announce_list();
    if (!std::empty(announce_list))
    {
        NSMutableString* listSection = [NSMutableString stringWithString:@"Trackers:\n"];

        for (auto const& tracker : announce_list)
        {
            [listSection appendFormat:@"%@\n", @(tracker.announce.c_str())];
        }

        self.trackersField.hidden = NO;
        self.trackersField.stringValue = listSection;
    }

    auto const& files_list = metainfo.files();
    if (!std::empty(files_list))
    {
        auto const n_files = metainfo.file_count();
        auto const is_multifile = n_files > 1;

        if (is_multifile)
        {
            NSMutableString* listSection = [NSMutableString stringWithFormat:@"Files (%lu):\n", n_files];

            for (auto const& [path, size] : files_list.sorted_by_path())
            {
                NSString* fullFilePath = @(path.c_str());
                NSCAssert([fullFilePath hasPrefix:[name stringByAppendingString:@"/"]], @"Expected file path %@ to begin with %@/", fullFilePath, name);

                NSString* shortenedFilePath = [fullFilePath substringFromIndex:name.length + 1];
                NSString* shortenedFilePathAndSize = [NSString
                    stringWithFormat:@"%@ - %@", shortenedFilePath, [NSString stringForFileSize:size]];

                [listSection appendFormat:@"%@\n", shortenedFilePathAndSize];
            }

            self.filesField.hidden = NO;
            self.filesField.stringValue = listSection;
        }
        else
        {
            self.fileIconImage.image = [NSWorkspace.sharedWorkspace iconForFileType:name.pathExtension];
        }
    }

    handler(nil);
}

@end
