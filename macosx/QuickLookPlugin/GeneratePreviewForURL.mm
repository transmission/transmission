#if __has_feature(modules)
@import AppKit;
@import CoreFoundation;
@import QuickLook;
#else
#import <AppKit/AppKit.h>
#import <CoreFoundation/CFPlugInCOM.h>
#import <QuickLook/QuickLook.h>
#endif

#include <string>
#include <unordered_map>
#include <vector>

#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/utils.h>

#import "NSStringAdditions.h"

QL_EXTERN_C_BEGIN
OSStatus GeneratePreviewForURL(void* thisInterface, QLPreviewRequestRef preview, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options);
void CancelPreviewGeneration(void* thisInterface, QLPreviewRequestRef preview);
QL_EXTERN_C_END

static NSUInteger const kIconWidth = 16;

namespace
{

class FileTreeNode
{
  public:
    FileTreeNode() = default;
    ~FileTreeNode() = default;

    auto MaybeCreateChild(std::string_view child_name)
    {
        return children_.try_emplace(std::string{ child_name });
    }

  private:
    FileTreeNode(FileTreeNode const&) = delete;
    FileTreeNode& operator=(FileTreeNode&) = delete;

    std::unordered_map<std::string, FileTreeNode> children_;
};

} // namespace

NSString* generateIconData(NSString* fileExtension, NSUInteger width, NSMutableDictionary* allImgProps)
{
    NSString* rawFilename = ![fileExtension isEqualToString:@""] ? fileExtension : @"blank_file_name_transmission";
    // we need to do this once per file extension, per size
    NSString* iconFileName = [NSString stringWithFormat:@"%ldx%@.tiff", width, rawFilename];

    if (!allImgProps[iconFileName])
    {
        NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFileType:fileExtension];

        NSRect const iconFrame = NSMakeRect(0.0, 0.0, width, width);
        NSImage* renderedIcon = [[NSImage alloc] initWithSize:iconFrame.size];
        [renderedIcon lockFocus];
        [icon drawInRect:iconFrame fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1.0];
        [renderedIcon unlockFocus];

        NSData* iconData = renderedIcon.TIFFRepresentation;

        NSDictionary* imgProps = @{
            (NSString*)kQLPreviewPropertyMIMETypeKey : @"image/png",
            (NSString*)kQLPreviewPropertyAttachmentDataKey : iconData
        };
        allImgProps[iconFileName] = imgProps;
    }

    return [@"cid:" stringByAppendingString:iconFileName];
}

OSStatus GeneratePreviewForURL(void* /*thisInterface*/, QLPreviewRequestRef preview, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options)
{
    // Before proceeding make sure the user didn't cancel the request
    if (QLPreviewRequestIsCancelled(preview))
    {
        return noErr;
    }

    //we need this call to ensure NSApp is initialized (not done automatically for plugins)
    [NSApplication sharedApplication];

    //try to parse the torrent file
    auto metainfo = tr_torrent_metainfo{};
    if (!metainfo.parse_torrent_file(((__bridge NSURL*)url).path.UTF8String))
    {
        return noErr;
    }

    NSBundle* bundle = [NSBundle bundleWithIdentifier:@"org.m0k.transmission.QuickLookPlugin"];

    NSURL* styleURL = [bundle URLForResource:@"style" withExtension:@"css"];
    NSString* styleContents = [NSString stringWithContentsOfURL:styleURL encoding:NSUTF8StringEncoding error:NULL];

    NSMutableString* htmlString = [NSMutableString string];
    [htmlString appendFormat:@"<html><style type=\"text/css\">%@</style><body>", styleContents];

    NSMutableDictionary* allImgProps = [NSMutableDictionary dictionary];

    NSString* name = @(metainfo.name().c_str());

    auto const n_files = metainfo.file_count();
    auto const is_multifile = n_files > 1;
    NSString* fileTypeString = is_multifile ? NSFileTypeForHFSTypeCode(kGenericFolderIcon) : name.pathExtension;

    NSUInteger const width = 32;
    [htmlString appendFormat:@"<h2><img class=\"icon\" src=\"%@\" width=\"%ld\" height=\"%ld\" />%@</h2>",
                             generateIconData(fileTypeString, width, allImgProps),
                             width,
                             width,
                             name];

    NSString* fileSizeString = [NSString stringForFileSize:metainfo.total_size()];
    if (is_multifile)
    {
        NSString* fileCountString = [NSString
            localizedStringWithFormat:NSLocalizedStringFromTableInBundle(@"%lu files", nil, bundle, "quicklook file count"), n_files];
        fileSizeString = [NSString stringWithFormat:@"%@, %@", fileCountString, fileSizeString];
    }
    [htmlString appendFormat:@"<p>%@</p>", fileSizeString];

    auto const date_created = metainfo.date_created();
    NSString* dateCreatedString = date_created > 0 ?
        [NSDateFormatter localizedStringFromDate:[NSDate dateWithTimeIntervalSince1970:date_created] dateStyle:NSDateFormatterLongStyle
                                       timeStyle:NSDateFormatterShortStyle] :
        nil;
    auto const& creator = metainfo.creator();
    NSString* creatorString = !std::empty(creator) ? @(creator.c_str()) : nil;
    if ([creatorString isEqualToString:@""])
    {
        creatorString = nil;
    }
    NSString* creationString = nil;
    if (dateCreatedString && creatorString)
    {
        creationString = [NSString
            stringWithFormat:NSLocalizedStringFromTableInBundle(@"Created on %@ with %@", nil, bundle, "quicklook creation info"),
                             dateCreatedString,
                             creatorString];
    }
    else if (dateCreatedString)
    {
        creationString = [NSString
            stringWithFormat:NSLocalizedStringFromTableInBundle(@"Created on %@", nil, bundle, "quicklook creation info"), dateCreatedString];
    }
    else if (creatorString)
    {
        creationString = [NSString
            stringWithFormat:NSLocalizedStringFromTableInBundle(@"Created with %@", nil, bundle, "quicklook creation info"), creatorString];
    }
    if (creationString)
    {
        [htmlString appendFormat:@"<p>%@</p>", creationString];
    }

    auto const& commentStr = metainfo.comment();
    if (!std::empty(commentStr))
    {
        NSString* comment = @(commentStr.c_str());
        if (![comment isEqualToString:@""])
            [htmlString appendFormat:@"<p>%@</p>", comment];
    }

    NSMutableArray* lists = [NSMutableArray array];

    auto const n_webseeds = metainfo.webseed_count();
    if (n_webseeds > 0)
    {
        NSMutableString* listSection = [NSMutableString string];
        [listSection appendString:@"<table>"];

        NSString* headerTitleString = n_webseeds == 1 ?
            NSLocalizedStringFromTableInBundle(@"1 Web Seed", nil, bundle, "quicklook web seed header") :
            [NSString localizedStringWithFormat:NSLocalizedStringFromTableInBundle(@"%lu Web Seeds", nil, bundle, "quicklook web seed header"),
                                                n_webseeds];
        [listSection appendFormat:@"<tr><th>%@</th></tr>", headerTitleString];

        for (size_t i = 0; i < n_webseeds; ++i)
        {
            [listSection appendFormat:@"<tr><td>%s</td></tr>", metainfo.webseed(i).c_str()];
        }

        [listSection appendString:@"</table>"];

        [lists addObject:listSection];
    }

    auto const& announce_list = metainfo.announce_list();
    if (!std::empty(announce_list))
    {
        NSMutableString* listSection = [NSMutableString string];
        [listSection appendString:@"<table>"];

        auto const n = std::size(announce_list);
        NSString* headerTitleString = n == 1 ?
            NSLocalizedStringFromTableInBundle(@"1 Tracker", nil, bundle, "quicklook tracker header") :
            [NSString localizedStringWithFormat:NSLocalizedStringFromTableInBundle(@"%lu Trackers", nil, bundle, "quicklook tracker header"), n];
        [listSection appendFormat:@"<tr><th>%@</th></tr>", headerTitleString];

        // TODO: handle tiers?
        for (auto const& tracker : announce_list)
        {
            [listSection appendFormat:@"<tr><td>%s</td></tr>", tracker.announce.c_str()];
        }

        [listSection appendString:@"</table>"];

        [lists addObject:listSection];
    }

    if (is_multifile)
    {
        NSMutableString* listSection = [NSMutableString string];
        [listSection appendString:@"<table>"];

        NSString* fileTitleString = [NSString
            localizedStringWithFormat:NSLocalizedStringFromTableInBundle(@"%lu Files", nil, bundle, "quicklook file header"), n_files];
        [listSection appendFormat:@"<tr><th>%@</th></tr>", fileTitleString];

        FileTreeNode root{};

        for (auto const& [path, size] : metainfo.files().sorted_by_path())
        {
            FileTreeNode* curNode = &root;
            size_t level = 0;

            auto subpath = std::string_view{ path };
            auto path_vec = std::vector<std::string_view>{};
            auto token = std::string_view{};
            while (tr_strv_sep(&subpath, &token, '/'))
            {
                path_vec.emplace_back(token);
            }
            size_t const last = path_vec.size() - 1;

            for (auto const& part : path_vec)
            {
                auto [it, inserted] = curNode->MaybeCreateChild(part);
                if (inserted)
                {
                    NSString* prefix = @"";
                    for (size_t i = 0; i < level; ++i)
                    {
                        prefix = [prefix stringByAppendingString:@"&emsp;"];
                    }

                    NSString* pathPart = @(it->first.c_str());
                    NSString* pathExt = nil;
                    NSString* fileSize = nil;
                    if (level < last)
                    {
                        // This node is a directory.
                        pathExt = NSFileTypeForHFSTypeCode(kGenericFolderIcon);
                        fileSize = @"";
                    }
                    else
                    {
                        // This node is a leaf file.
                        pathExt = pathPart.pathExtension;
                        fileSize = [NSString stringForFileSize:size];
                    }

                    [listSection appendFormat:@"<tr><td>%@<img class=\"icon\" src=\"%@\" width=\"%ld\" height=\"%ld\" />%@</td><td class=\"grey\">%@</td></tr>",
                                              prefix,
                                              generateIconData(pathExt, kIconWidth, allImgProps),
                                              kIconWidth,
                                              kIconWidth,
                                              pathPart,
                                              fileSize];
                }

                curNode = &it->second;
                level++;
            }
        }

        [listSection appendString:@"</table>"];

        [lists addObject:listSection];
    }

    if (lists.count > 0)
    {
        [htmlString appendFormat:@"<hr/><br>%@", [lists componentsJoinedByString:@"<br>"]];
    }

    [htmlString appendString:@"</body></html>"];

    NSDictionary* props = @{
        (NSString*)kQLPreviewPropertyTextEncodingNameKey : @"UTF-8",
        (NSString*)kQLPreviewPropertyMIMETypeKey : @"text/html",
        (NSString*)kQLPreviewPropertyAttachmentsKey : allImgProps
    };

    QLPreviewRequestSetDataRepresentation(
        preview,
        (__bridge CFDataRef)[htmlString dataUsingEncoding:NSUTF8StringEncoding],
        kUTTypeHTML,
        (__bridge CFDictionaryRef)props);

    return noErr;
}

void CancelPreviewGeneration(void* /*thisInterface*/, QLPreviewRequestRef preview)
{
    // Implement only if supported
}
