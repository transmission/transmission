// This file Copyright © 2010-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoOptionsViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

#define OPTION_POPUP_GLOBAL 0
#define OPTION_POPUP_NO_LIMIT 1
#define OPTION_POPUP_LIMIT 2

#define OPTION_POPUP_PRIORITY_HIGH 0
#define OPTION_POPUP_PRIORITY_NORMAL 1
#define OPTION_POPUP_PRIORITY_LOW 2

#define INVALID -99

@interface InfoOptionsViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) IBOutlet NSPopUpButton* fPriorityPopUp;
@property(nonatomic) IBOutlet NSPopUpButton* fRatioPopUp;
@property(nonatomic) IBOutlet NSPopUpButton* fIdlePopUp;
@property(nonatomic) IBOutlet NSButton* fUploadLimitCheck;
@property(nonatomic) IBOutlet NSButton* fDownloadLimitCheck;
@property(nonatomic) IBOutlet NSButton* fGlobalLimitCheck;
@property(nonatomic) IBOutlet NSButton* fRemoveSeedingCompleteCheck;
@property(nonatomic) IBOutlet NSTextField* fUploadLimitField;
@property(nonatomic) IBOutlet NSTextField* fDownloadLimitField;
@property(nonatomic) IBOutlet NSTextField* fRatioLimitField;
@property(nonatomic) IBOutlet NSTextField* fIdleLimitField;
@property(nonatomic) IBOutlet NSTextField* fUploadLimitLabel;
@property(nonatomic) IBOutlet NSTextField* fDownloadLimitLabel;
@property(nonatomic) IBOutlet NSTextField* fIdleLimitLabel;
@property(nonatomic) IBOutlet NSTextField* fRatioLimitGlobalLabel;
@property(nonatomic) IBOutlet NSTextField* fIdleLimitGlobalLabel;
@property(nonatomic) IBOutlet NSTextField* fPeersConnectLabel;
@property(nonatomic) IBOutlet NSTextField* fPeersConnectField;

//remove when we switch to auto layout
@property(nonatomic) IBOutlet NSTextField* fTransferBandwidthSectionLabel;
@property(nonatomic) IBOutlet NSTextField* fPrioritySectionLabel;
@property(nonatomic) IBOutlet NSTextField* fPriorityLabel;
@property(nonatomic) IBOutlet NSTextField* fSeedingLimitsSectionLabel;
@property(nonatomic) IBOutlet NSTextField* fRatioLabel;
@property(nonatomic) IBOutlet NSTextField* fInactivityLabel;
@property(nonatomic) IBOutlet NSTextField* fAdvancedSectionLabel;
@property(nonatomic) IBOutlet NSTextField* fMaxConnectionsLabel;

@property(nonatomic, copy) NSString* fInitialString;

- (void)setupInfo;
- (void)setGlobalLabels;
- (void)updateOptionsNotification:(NSNotification*)notification;

@end

@implementation InfoOptionsViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:@"InfoOptionsView" bundle:nil]))
    {
        self.title = NSLocalizedString(@"Options", "Inspector view -> title");
    }

    return self;
}

- (void)awakeFromNib
{
    [self setGlobalLabels];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(setGlobalLabels) name:@"UpdateGlobalOptions" object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateOptionsNotification:)
                                               name:@"UpdateOptionsNotification"
                                             object:nil];
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    //don't check if it's the same in case the metadata changed
    self.fTorrents = torrents;

    self.fSet = NO;
}

- (void)updateInfo
{
    if (!self.fSet)
    {
        [self setupInfo];
    }

    self.fSet = YES;
}

- (void)updateOptions
{
    if (self.fTorrents.count == 0)
    {
        return;
    }

    //get bandwidth info
    NSEnumerator* enumerator = [self.fTorrents objectEnumerator];
    Torrent* torrent = [enumerator nextObject]; //first torrent

    NSInteger uploadUseSpeedLimit = [torrent usesSpeedLimit:YES] ? NSControlStateValueOn : NSControlStateValueOff;
    NSInteger uploadSpeedLimit = [torrent speedLimit:YES];
    NSInteger downloadUseSpeedLimit = [torrent usesSpeedLimit:NO] ? NSControlStateValueOn : NSControlStateValueOff;
    NSInteger downloadSpeedLimit = [torrent speedLimit:NO];
    NSInteger globalUseSpeedLimit = torrent.usesGlobalSpeedLimit ? NSControlStateValueOn : NSControlStateValueOff;

    while ((torrent = [enumerator nextObject]) &&
           (uploadUseSpeedLimit != NSControlStateValueMixed || uploadSpeedLimit != INVALID || downloadUseSpeedLimit != NSControlStateValueMixed ||
            downloadSpeedLimit != INVALID || globalUseSpeedLimit != NSControlStateValueMixed))
    {
        if (uploadUseSpeedLimit != NSControlStateValueMixed &&
            uploadUseSpeedLimit != ([torrent usesSpeedLimit:YES] ? NSControlStateValueOn : NSControlStateValueOff))
        {
            uploadUseSpeedLimit = NSControlStateValueMixed;
        }

        if (uploadSpeedLimit != INVALID && uploadSpeedLimit != [torrent speedLimit:YES])
        {
            uploadSpeedLimit = INVALID;
        }

        if (downloadUseSpeedLimit != NSControlStateValueMixed &&
            downloadUseSpeedLimit != ([torrent usesSpeedLimit:NO] ? NSControlStateValueOn : NSControlStateValueOff))
        {
            downloadUseSpeedLimit = NSControlStateValueMixed;
        }

        if (downloadSpeedLimit != INVALID && downloadSpeedLimit != [torrent speedLimit:NO])
        {
            downloadSpeedLimit = INVALID;
        }

        if (globalUseSpeedLimit != NSControlStateValueMixed &&
            globalUseSpeedLimit != (torrent.usesGlobalSpeedLimit ? NSControlStateValueOn : NSControlStateValueOff))
        {
            globalUseSpeedLimit = NSControlStateValueMixed;
        }
    }

    //set upload view
    self.fUploadLimitCheck.state = uploadUseSpeedLimit;
    self.fUploadLimitCheck.enabled = YES;

    self.fUploadLimitLabel.enabled = uploadUseSpeedLimit == NSControlStateValueOn;
    self.fUploadLimitField.enabled = uploadUseSpeedLimit == NSControlStateValueOn;
    if (uploadSpeedLimit != INVALID)
    {
        self.fUploadLimitField.intValue = uploadSpeedLimit;
    }
    else
    {
        self.fUploadLimitField.stringValue = @"";
    }

    //set download view
    self.fDownloadLimitCheck.state = downloadUseSpeedLimit;
    self.fDownloadLimitCheck.enabled = YES;

    self.fDownloadLimitLabel.enabled = downloadUseSpeedLimit == NSControlStateValueOn;
    self.fDownloadLimitField.enabled = downloadUseSpeedLimit == NSControlStateValueOn;
    if (downloadSpeedLimit != INVALID)
    {
        self.fDownloadLimitField.intValue = downloadSpeedLimit;
    }
    else
    {
        self.fDownloadLimitField.stringValue = @"";
    }

    //set global check
    self.fGlobalLimitCheck.state = globalUseSpeedLimit;
    self.fGlobalLimitCheck.enabled = YES;

    //get ratio and idle info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger checkRatio = torrent.ratioSetting;
    NSInteger checkIdle = torrent.idleSetting;
    NSInteger removeWhenFinishSeeding = torrent.removeWhenFinishSeeding ? NSControlStateValueOn : NSControlStateValueOff;
    CGFloat ratioLimit = torrent.ratioLimit;
    NSUInteger idleLimit = torrent.idleLimitMinutes;

    while ((torrent = [enumerator nextObject]) &&
           (checkRatio != INVALID || ratioLimit != INVALID || checkIdle != INVALID || idleLimit != INVALID))
    {
        if (checkRatio != INVALID && checkRatio != torrent.ratioSetting)
        {
            checkRatio = INVALID;
        }

        if (ratioLimit != INVALID && ratioLimit != torrent.ratioLimit)
        {
            ratioLimit = INVALID;
        }

        if (checkIdle != INVALID && checkIdle != torrent.idleSetting)
        {
            checkIdle = INVALID;
        }

        if (idleLimit != INVALID && idleLimit != torrent.idleLimitMinutes)
        {
            idleLimit = INVALID;
        }

        if (removeWhenFinishSeeding != NSControlStateValueMixed &&
            removeWhenFinishSeeding != (torrent.removeWhenFinishSeeding ? NSControlStateValueOn : NSControlStateValueOff))
        {
            removeWhenFinishSeeding = NSControlStateValueMixed;
        }
    }

    //set ratio view
    NSInteger index;
    if (checkRatio == TR_RATIOLIMIT_SINGLE)
    {
        index = OPTION_POPUP_LIMIT;
    }
    else if (checkRatio == TR_RATIOLIMIT_UNLIMITED)
    {
        index = OPTION_POPUP_NO_LIMIT;
    }
    else if (checkRatio == TR_RATIOLIMIT_GLOBAL)
    {
        index = OPTION_POPUP_GLOBAL;
    }
    else
    {
        index = -1;
    }
    [self.fRatioPopUp selectItemAtIndex:index];
    self.fRatioPopUp.enabled = YES;

    self.fRatioLimitField.hidden = checkRatio != TR_RATIOLIMIT_SINGLE;
    if (ratioLimit != INVALID)
    {
        self.fRatioLimitField.floatValue = ratioLimit;
    }
    else
    {
        self.fRatioLimitField.stringValue = @"";
    }

    self.fRatioLimitGlobalLabel.hidden = checkRatio != TR_RATIOLIMIT_GLOBAL;

    //set idle view
    if (checkIdle == TR_IDLELIMIT_SINGLE)
    {
        index = OPTION_POPUP_LIMIT;
    }
    else if (checkIdle == TR_IDLELIMIT_UNLIMITED)
    {
        index = OPTION_POPUP_NO_LIMIT;
    }
    else if (checkIdle == TR_IDLELIMIT_GLOBAL)
    {
        index = OPTION_POPUP_GLOBAL;
    }
    else
    {
        index = -1;
    }
    [self.fIdlePopUp selectItemAtIndex:index];
    self.fIdlePopUp.enabled = YES;

    self.fIdleLimitField.hidden = checkIdle != TR_IDLELIMIT_SINGLE;
    if (idleLimit != INVALID)
    {
        self.fIdleLimitField.integerValue = idleLimit;
    }
    else
    {
        self.fIdleLimitField.stringValue = @"";
    }
    self.fIdleLimitLabel.hidden = checkIdle != TR_IDLELIMIT_SINGLE;

    self.fIdleLimitGlobalLabel.hidden = checkIdle != TR_IDLELIMIT_GLOBAL;

    //set remove transfer when seeding finishes
    self.fRemoveSeedingCompleteCheck.state = removeWhenFinishSeeding;
    self.fRemoveSeedingCompleteCheck.enabled = YES;

    //get priority info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger priority = torrent.priority;

    while ((torrent = [enumerator nextObject]) && priority != INVALID)
    {
        if (priority != torrent.priority)
        {
            priority = INVALID;
        }
    }

    //set priority view
    if (priority == TR_PRI_HIGH)
    {
        index = OPTION_POPUP_PRIORITY_HIGH;
    }
    else if (priority == TR_PRI_NORMAL)
    {
        index = OPTION_POPUP_PRIORITY_NORMAL;
    }
    else if (priority == TR_PRI_LOW)
    {
        index = OPTION_POPUP_PRIORITY_LOW;
    }
    else
    {
        index = -1;
    }
    [self.fPriorityPopUp selectItemAtIndex:index];
    self.fPriorityPopUp.enabled = YES;

    //get peer info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger maxPeers = torrent.maxPeerConnect;

    while ((torrent = [enumerator nextObject]))
    {
        if (maxPeers != torrent.maxPeerConnect)
        {
            maxPeers = INVALID;
            break;
        }
    }

    //set peer view
    self.fPeersConnectField.enabled = YES;
    self.fPeersConnectLabel.enabled = YES;
    if (maxPeers != INVALID)
    {
        self.fPeersConnectField.intValue = maxPeers;
    }
    else
    {
        self.fPeersConnectField.stringValue = @"";
    }
}

- (void)setUseSpeedLimit:(id)sender
{
    BOOL const upload = sender == self.fUploadLimitCheck;

    if (((NSButton*)sender).state == NSControlStateValueMixed)
    {
        [sender setState:NSControlStateValueOn];
    }
    BOOL const limit = ((NSButton*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents)
    {
        [torrent setUseSpeedLimit:limit upload:upload];
    }

    NSTextField* field = upload ? self.fUploadLimitField : self.fDownloadLimitField;
    field.enabled = limit;
    if (limit)
    {
        [field selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    NSTextField* label = upload ? self.fUploadLimitLabel : self.fDownloadLimitLabel;
    label.enabled = limit;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setUseGlobalSpeedLimit:(id)sender
{
    if (((NSButton*)sender).state == NSControlStateValueMixed)
    {
        [sender setState:NSControlStateValueOn];
    }
    BOOL const limit = ((NSButton*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.usesGlobalSpeedLimit = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setSpeedLimit:(id)sender
{
    BOOL const upload = sender == self.fUploadLimitField;
    NSInteger const limit = [sender intValue];

    for (Torrent* torrent in self.fTorrents)
    {
        [torrent setSpeedLimit:limit upload:upload];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setRatioSetting:(id)sender
{
    NSInteger setting;
    BOOL single = NO;
    switch ([sender indexOfSelectedItem])
    {
    case OPTION_POPUP_LIMIT:
        setting = TR_RATIOLIMIT_SINGLE;
        single = YES;
        break;
    case OPTION_POPUP_NO_LIMIT:
        setting = TR_RATIOLIMIT_UNLIMITED;
        break;
    case OPTION_POPUP_GLOBAL:
        setting = TR_RATIOLIMIT_GLOBAL;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in ratio popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.ratioSetting = static_cast<tr_ratiolimit>(setting);
    }

    self.fRatioLimitField.hidden = !single;
    if (single)
    {
        [self.fRatioLimitField selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    self.fRatioLimitGlobalLabel.hidden = setting != TR_RATIOLIMIT_GLOBAL;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setRatioLimit:(id)sender
{
    CGFloat const limit = [sender floatValue];

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.ratioLimit = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setIdleSetting:(id)sender
{
    NSInteger setting;
    BOOL single = NO;
    switch ([sender indexOfSelectedItem])
    {
    case OPTION_POPUP_LIMIT:
        setting = TR_IDLELIMIT_SINGLE;
        single = YES;
        break;
    case OPTION_POPUP_NO_LIMIT:
        setting = TR_IDLELIMIT_UNLIMITED;
        break;
    case OPTION_POPUP_GLOBAL:
        setting = TR_IDLELIMIT_GLOBAL;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in idle popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.idleSetting = static_cast<tr_idlelimit>(setting);
    }

    self.fIdleLimitField.hidden = !single;
    self.fIdleLimitLabel.hidden = !single;
    if (single)
    {
        [self.fIdleLimitField selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    self.fIdleLimitGlobalLabel.hidden = setting != TR_IDLELIMIT_GLOBAL;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setIdleLimit:(id)sender
{
    NSUInteger const limit = [sender integerValue];

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.idleLimitMinutes = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (IBAction)setRemoveWhenSeedingCompletes:(id)sender
{
    if (((NSButton*)sender).state == NSControlStateValueMixed)
    {
        [sender setState:NSControlStateValueOn];
    }
    BOOL const enable = ((NSButton*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.removeWhenFinishSeeding = enable;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setPriority:(id)sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem])
    {
    case OPTION_POPUP_PRIORITY_HIGH:
        priority = TR_PRI_HIGH;
        break;
    case OPTION_POPUP_PRIORITY_NORMAL:
        priority = TR_PRI_NORMAL;
        break;
    case OPTION_POPUP_PRIORITY_LOW:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in priority popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.priority = priority;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setPeersConnectLimit:(id)sender
{
    NSInteger limit = [sender intValue];

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.maxPeerConnect = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor
{
    self.fInitialString = control.stringValue;

    return YES;
}

- (BOOL)control:(NSControl*)control didFailToFormatString:(NSString*)string errorDescription:(NSString*)error
{
    NSBeep();
    if (self.fInitialString)
    {
        control.stringValue = self.fInitialString;
        self.fInitialString = nil;
    }
    return NO;
}

#pragma mark - Private

- (void)setupInfo
{
    if (self.fTorrents.count == 0)
    {
        self.fUploadLimitCheck.enabled = NO;
        self.fUploadLimitCheck.state = NSControlStateValueOff;
        self.fUploadLimitField.enabled = NO;
        self.fUploadLimitLabel.enabled = NO;
        self.fUploadLimitField.stringValue = @"";

        self.fDownloadLimitCheck.enabled = NO;
        self.fDownloadLimitCheck.state = NSControlStateValueOff;
        self.fDownloadLimitField.enabled = NO;
        self.fDownloadLimitLabel.enabled = NO;
        self.fDownloadLimitField.stringValue = @"";

        self.fGlobalLimitCheck.enabled = NO;
        self.fGlobalLimitCheck.state = NSControlStateValueOff;

        self.fPriorityPopUp.enabled = NO;
        [self.fPriorityPopUp selectItemAtIndex:-1];

        self.fRatioPopUp.enabled = NO;
        [self.fRatioPopUp selectItemAtIndex:-1];
        self.fRatioLimitField.hidden = YES;
        self.fRatioLimitField.stringValue = @"";
        self.fRatioLimitGlobalLabel.hidden = YES;

        self.fIdlePopUp.enabled = NO;
        [self.fIdlePopUp selectItemAtIndex:-1];
        self.fIdleLimitField.hidden = YES;
        self.fIdleLimitField.stringValue = @"";
        self.fIdleLimitLabel.hidden = YES;
        self.fIdleLimitGlobalLabel.hidden = YES;

        self.fRemoveSeedingCompleteCheck.enabled = NO;
        self.fRemoveSeedingCompleteCheck.state = NSControlStateValueOff;

        self.fPeersConnectField.enabled = NO;
        self.fPeersConnectField.stringValue = @"";
        self.fPeersConnectLabel.enabled = NO;
    }
    else
    {
        [self updateOptions];
    }
}

- (void)setGlobalLabels
{
    NSString* global = [NSUserDefaults.standardUserDefaults boolForKey:@"RatioCheck"] ?
        [NSString stringForRatio:[NSUserDefaults.standardUserDefaults floatForKey:@"RatioLimit"]] :
        NSLocalizedString(@"disabled", "Info options -> global setting");
    self.fRatioLimitGlobalLabel.stringValue = global;

    //idle field
    NSString* globalIdle;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"IdleLimitCheck"])
    {
        NSInteger const globalMin = [NSUserDefaults.standardUserDefaults integerForKey:@"IdleLimitMinutes"];
        globalIdle = globalMin == 1 ?
            NSLocalizedString(@"1 minute", "Info options -> global setting") :
            [NSString localizedStringWithFormat:NSLocalizedString(@"%ld minutes", "Info options -> global setting"), globalMin];
    }
    else
    {
        globalIdle = NSLocalizedString(@"disabled", "Info options -> global setting");
    }
    self.fIdleLimitGlobalLabel.stringValue = globalIdle;
}

- (void)updateOptionsNotification:(NSNotification*)notification
{
    if (notification.object != self)
    {
        [self updateOptions];
    }
}

@end
