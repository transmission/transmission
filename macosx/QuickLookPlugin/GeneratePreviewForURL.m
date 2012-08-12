#import "transmission.h"
#import "NSStringAdditions.h"

OSStatus GeneratePreviewForURL(void *thisInterface, QLPreviewRequestRef preview, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options);
void CancelPreviewGeneration(void *thisInterface, QLPreviewRequestRef preview);

NSString * generateIconData(NSString * fileExtension, NSUInteger width, NSMutableDictionary * allImgProps)
{
    NSString * rawFilename = ![fileExtension isEqualToString: @""] ? fileExtension : @"blank_file_name_transmission";
    NSString * iconFileName = [NSString stringWithFormat: @"%ldx%@.tiff", width, rawFilename]; //we need to do this once per file extension, per size
    
    if (!allImgProps[iconFileName])
    {
        NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFileType: fileExtension];
        
        const NSRect iconFrame = NSMakeRect(0.0, 0.0, width, width);
        NSImage * renderedIcon = [[NSImage alloc] initWithSize: iconFrame.size];
        [renderedIcon lockFocus];
        [icon drawInRect: iconFrame fromRect: NSZeroRect operation: NSCompositeCopy fraction: 1.0];
        [renderedIcon unlockFocus];
        
        NSData * iconData = [renderedIcon TIFFRepresentation];
        [renderedIcon release];
        
        NSDictionary * imgProps = @{
            (NSString *)kQLPreviewPropertyMIMETypeKey : @"image/png",
            (NSString *)kQLPreviewPropertyAttachmentDataKey : iconData };
        [allImgProps setObject: imgProps forKey: iconFileName];
    }
    
    return [@"cid:" stringByAppendingString: iconFileName];
}

OSStatus GeneratePreviewForURL(void *thisInterface, QLPreviewRequestRef preview, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options)
{
    // Before proceeding make sure the user didn't cancel the request
    if (QLPreviewRequestIsCancelled(preview))
        return noErr;
    
    //try to parse the torrent file
    tr_info inf;
    tr_ctor * ctor = tr_ctorNew(NULL);
    tr_ctorSetMetainfoFromFile(ctor, [[(NSURL *)url path] UTF8String]);
    const int err = tr_torrentParse(ctor, &inf);
    tr_ctorFree(ctor);
    if (err)
        return noErr;
    
    NSURL * styleURL = [[NSBundle bundleWithIdentifier: @"org.m0k.transmission.QuickLookPlugin"] URLForResource: @"style" withExtension: @"css"];
    NSString * styleContents = [NSString stringWithContentsOfURL: styleURL encoding: NSUTF8StringEncoding error: NULL];
    
    NSMutableString * htmlString = [NSMutableString string];
    [htmlString appendFormat: @"<html><style type=\"text/css\">%@</style><body>", styleContents];
    
    NSMutableDictionary * allImgProps = [NSMutableDictionary dictionary];
    
    NSString * name = [NSString stringWithUTF8String: inf.name];
    NSString * fileTypeString = inf.isMultifile ? NSFileTypeForHFSTypeCode(kGenericFolderIcon) : [name pathExtension];
    
    const NSUInteger width = 32;
    [htmlString appendFormat: @"<h2><img class=\"icon\" src=\"%@\" width=\"%ld\" height=\"%ld\">%@</h2>", generateIconData(fileTypeString, width, allImgProps), width, width, name];
    
    NSString * fileSizeString = [NSString stringForFileSize: inf.totalSize];
    if (inf.isMultifile)
    {
        NSString * fileCountString;
        if (inf.fileCount == 1)
            fileCountString = NSLocalizedString(@"1 file", "quicklook file count");
        else
            fileCountString= [NSString stringWithFormat: NSLocalizedString(@"%@ files", "quicklook file count"), [NSString formattedUInteger: inf.fileCount]];
        fileSizeString = [NSString stringWithFormat: @"%@, %@", fileCountString, fileSizeString];
    }
    [htmlString appendFormat: @"<p>%@</p>", fileSizeString];
    if (inf.comment)
    {
        NSString * comment = [NSString stringWithUTF8String: inf.comment];
        if (![comment isEqualToString: @""])
            [htmlString appendFormat: @"<p>%@</p>", comment];
    }
    
    [htmlString appendString: @"<hr/>"];
    
    if (inf.webseedCount > 0)
    {
        [htmlString appendString: @"<br><br><table>"];
        
        NSString * headerTitleString = inf.webseedCount == 1 ? NSLocalizedString(@"1 Web Seed", "quicklook web seed header") : [NSString stringWithFormat: NSLocalizedString(@"%@ Web Seeds", "quicklook web seed header"), [NSString formattedUInteger: inf.webseedCount]];
        [htmlString appendFormat: @"<tr><th>%@</th></tr>", headerTitleString];
        
        for (int i = 0; i < inf.webseedCount; ++i)
            [htmlString appendFormat: @"<tr><td>%s<td></tr>", inf.webseeds[i]];
        
        [htmlString appendString:@"</table>"];
    }
    
    if (inf.trackerCount > 0)
    {
        [htmlString appendString: @"<br><br><table>"];
        
        NSString * headerTitleString = inf.trackerCount == 1 ? NSLocalizedString(@"1 Tracker", "quicklook tracker header") : [NSString stringWithFormat: NSLocalizedString(@"%@ Trackers", "quicklook tracker header"), [NSString formattedUInteger: inf.trackerCount]];
        [htmlString appendFormat: @"<tr><th>%@</th></tr>", headerTitleString];
        
#warning handle tiers?
        for (int i = 0; i < inf.trackerCount; ++i)
            [htmlString appendFormat: @"<tr><td>%s<td></tr>", inf.trackers[i].announce];
        
        [htmlString appendString:@"</table>"];
    }
    
    if (inf.isMultifile)
    {
        [htmlString appendString: @"<br><br><table>"];
        
        NSString * fileTitleString = inf.fileCount == 1 ? NSLocalizedString(@"1 File", "quicklook file header") : [NSString stringWithFormat: NSLocalizedString(@"%@ Files", "quicklook file header"), [NSString formattedUInteger: inf.fileCount]];
        [htmlString appendFormat: @"<tr><th>%@</th></tr>", fileTitleString];
        
#warning display size?
#warning display folders?
        for (int i = 0; i < inf.fileCount; ++i)
        {
            NSString * fullFilePath = [NSString stringWithUTF8String: inf.files[i].name];
            NSCAssert([fullFilePath hasPrefix: [name stringByAppendingString: @"/"]], @"Expected file path %@ to begin with %@/", fullFilePath, name);
            
            NSString * shortenedFilePath = [fullFilePath substringFromIndex: [name length]+1];
            
            const NSUInteger width = 16;
            [htmlString appendFormat: @"<tr><td><img class=\"icon\" src=\"%@\" width=\"%ld\" height=\"%ld\">%@<td></tr>", generateIconData([shortenedFilePath pathExtension], width, allImgProps), width, width, shortenedFilePath];
        }
        
        [htmlString appendString:@"</table>"];
    }
    
    [htmlString appendString: @"</body></html>"];
    
    tr_metainfoFree(&inf);
    
    NSDictionary * props = @{ (NSString *)kQLPreviewPropertyTextEncodingNameKey : @"UTF-8",
                                (NSString *)kQLPreviewPropertyMIMETypeKey : @"text/html",
                                (NSString *)kQLPreviewPropertyAttachmentsKey : allImgProps };
    
    QLPreviewRequestSetDataRepresentation(preview, (CFDataRef)[htmlString dataUsingEncoding: NSUTF8StringEncoding], kUTTypeHTML, (CFDictionaryRef)props);
    
    return noErr;
}

void CancelPreviewGeneration(void *thisInterface, QLPreviewRequestRef preview)
{
    // Implement only if supported
}
