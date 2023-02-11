// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BlocklistDownloaderViewController.h"
#import "BlocklistDownloader.h"
#import "PrefsController.h"
#import "NSStringAdditions.h"

@interface BlocklistDownloaderViewController ()

@property(nonatomic) IBOutlet NSWindow* fStatusWindow;
@property(nonatomic) IBOutlet NSProgressIndicator* fProgressBar;
@property(nonatomic) IBOutlet NSTextField* fTextField;
@property(nonatomic) IBOutlet NSButton* fButton;

@property(nonatomic, readonly) PrefsController* fPrefsController;

@end

@implementation BlocklistDownloaderViewController

BlocklistDownloaderViewController* fBLViewController = nil;
+ (void)downloadWithPrefsController:(PrefsController*)prefsController
{
    if (!fBLViewController)
    {
        fBLViewController = [[BlocklistDownloaderViewController alloc] initWithPrefsController:prefsController];
        [fBLViewController startDownload];
    }
}

- (void)awakeFromNib
{
    self.fButton.title = NSLocalizedString(@"Cancel", "Blocklist -> cancel button");

    CGFloat const oldWidth = NSWidth(self.fButton.frame);
    [self.fButton sizeToFit];
    NSRect buttonFrame = self.fButton.frame;
    buttonFrame.size.width += 12.0; //sizeToFit sizes a bit too small
    buttonFrame.origin.x -= NSWidth(buttonFrame) - oldWidth;
    self.fButton.frame = buttonFrame;

    self.fProgressBar.usesThreadedAnimation = YES;
    [self.fProgressBar startAnimation:self];
}

- (void)cancelDownload:(id)sender
{
    [[BlocklistDownloader downloader] cancelDownload];
}

- (void)setStatusStarting
{
    self.fTextField.stringValue = [NSLocalizedString(@"Connecting to site", "Blocklist -> message") stringByAppendingEllipsis];
    self.fProgressBar.indeterminate = YES;
}

- (void)setStatusProgressForCurrentSize:(NSUInteger)currentSize expectedSize:(long long)expectedSize
{
    NSString* string = NSLocalizedString(@"Downloading blocklist", "Blocklist -> message");
    if (expectedSize != NSURLResponseUnknownLength)
    {
        self.fProgressBar.indeterminate = NO;

        NSString* substring = [NSString stringForFilePartialSize:currentSize fullSize:expectedSize];
        string = [string stringByAppendingFormat:@" (%@)", substring];
        self.fProgressBar.doubleValue = (double)currentSize / expectedSize;
    }
    else
    {
        string = [string stringByAppendingFormat:@" (%@)", [NSString stringForFileSize:currentSize]];
    }

    self.fTextField.stringValue = string;
}

- (void)setStatusProcessing
{
    //change to indeterminate while processing
    self.fProgressBar.indeterminate = YES;
    [self.fProgressBar startAnimation:self];

    self.fTextField.stringValue = [NSLocalizedString(@"Processing blocklist", "Blocklist -> message") stringByAppendingEllipsis];
    self.fButton.enabled = NO;
}

- (void)setFinished
{
    [self.fPrefsController.window endSheet:self.fStatusWindow];

    fBLViewController = nil;
}

- (void)setFailed:(NSString*)error
{
    [self.fPrefsController.window endSheet:self.fStatusWindow];

    NSAlert* alert = [[NSAlert alloc] init];
    [alert addButtonWithTitle:NSLocalizedString(@"OK", "Blocklist -> button")];
    alert.messageText = NSLocalizedString(@"Download of the blocklist failed.", "Blocklist -> message");
    alert.alertStyle = NSAlertStyleWarning;

    alert.informativeText = error;

    [alert beginSheetModalForWindow:self.fPrefsController.window completionHandler:^(NSModalResponse /*returnCode*/) {
        fBLViewController = nil;
    }];
}

#pragma mark - Private

- (instancetype)initWithPrefsController:(PrefsController*)prefsController
{
    if ((self = [super init]))
    {
        _fPrefsController = prefsController;
    }

    return self;
}

- (void)startDownload
{
    //load window and show as sheet
    [NSBundle.mainBundle loadNibNamed:@"BlocklistStatusWindow" owner:self topLevelObjects:NULL];

    BlocklistDownloader* downloader = [BlocklistDownloader downloader];
    downloader.viewController = self; //do before showing the sheet to ensure it doesn't slide out with placeholder text

    [self.fPrefsController.window beginSheet:self.fStatusWindow completionHandler:nil];
}

@end
