/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2010-2011 Transmission authors and contributors
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

#import "InfoPeersViewController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "PeerProgressIndicatorCell.h"
#import "Torrent.h"

#import "transmission.h" // required by utils.h
#import "utils.h"

@interface InfoPeersViewController (Private)

- (void) setupInfo;

- (void) setWebSeedTableHidden: (BOOL) hide animate: (BOOL) animate;
- (NSArray *) peerSortDescriptors;

@end

@implementation InfoPeersViewController

- (id) init
{
    if ((self = [super initWithNibName: @"InfoPeersView" bundle: nil]))
    {
        [self setTitle: NSLocalizedString(@"Peers", "Inspector view -> title")];
    }
    
    return self;
}

- (void) awakeFromNib
{
    const CGFloat height = [[NSUserDefaults standardUserDefaults] floatForKey: @"InspectorContentHeightPeers"];
    if (height != 0.0)
    {
        NSRect viewRect = [[self view] frame];
        viewRect.size.height = height;
        [[self view] setFrame: viewRect];
    }
    
    //set table header text
    [[[fPeerTable tableColumnWithIdentifier: @"IP"] headerCell] setStringValue: NSLocalizedString(@"IP Address",
                                                                        "inspector -> peer table -> header")];
    [[[fPeerTable tableColumnWithIdentifier: @"Client"] headerCell] setStringValue: NSLocalizedString(@"Client",
                                                                        "inspector -> peer table -> header")];
    [[[fPeerTable tableColumnWithIdentifier: @"DL From"] headerCell] setStringValue: NSLocalizedString(@"DL",
                                                                        "inspector -> peer table -> header")];
    [[[fPeerTable tableColumnWithIdentifier: @"UL To"] headerCell] setStringValue: NSLocalizedString(@"UL",
                                                                        "inspector -> peer table -> header")];
    
    [[[fWebSeedTable tableColumnWithIdentifier: @"Address"] headerCell] setStringValue: NSLocalizedString(@"Web Seeds",
                                                                        "inspector -> web seed table -> header")];
    [[[fWebSeedTable tableColumnWithIdentifier: @"DL From"] headerCell] setStringValue: NSLocalizedString(@"DL",
                                                                        "inspector -> web seed table -> header")];
    
    //set table header tool tips
    [[fPeerTable tableColumnWithIdentifier: @"Encryption"] setHeaderToolTip: NSLocalizedString(@"Encrypted Connection",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"Progress"] setHeaderToolTip: NSLocalizedString(@"Available",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"DL From"] setHeaderToolTip: NSLocalizedString(@"Downloading From Peer",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"UL To"] setHeaderToolTip: NSLocalizedString(@"Uploading To Peer",
                                                                        "inspector -> peer table -> header tool tip")];
    
    [[fWebSeedTable tableColumnWithIdentifier: @"DL From"] setHeaderToolTip: NSLocalizedString(@"Downloading From Web Seed",
                                                                        "inspector -> web seed table -> header tool tip")];
    
    //prepare for animating peer table and web seed table
    NSRect webSeedTableFrame = [[fWebSeedTable enclosingScrollView] frame];
    fWebSeedTableHeight = webSeedTableFrame.size.height;
    fSpaceBetweenWebSeedAndPeer = webSeedTableFrame.origin.y - NSMaxY([[fPeerTable enclosingScrollView] frame]);
    
    [self setWebSeedTableHidden: YES animate: NO];
}

- (void) dealloc
{
    [fTorrents release];
    
    [fPeers release];
    [fWebSeeds release];
    
    [fWebSeedTableAnimation release];
    
    [super dealloc];
}

#warning subclass?
- (void) setInfoForTorrents: (NSArray *) torrents
{
    //don't check if it's the same in case the metadata changed
    [fTorrents release];
    fTorrents = [torrents retain];
    
    fSet = NO;
}

- (void) updateInfo
{
    if (!fSet)
        [self setupInfo];
    
    if ([fTorrents count] == 0)
        return;
    
    if (!fPeers)
        fPeers = [[NSMutableArray alloc] init];
    else
        [fPeers removeAllObjects];
    
    if (!fWebSeeds)
        fWebSeeds = [[NSMutableArray alloc] init];
    else
        [fWebSeeds removeAllObjects];
    
    NSUInteger known = 0, connected = 0, tracker = 0, incoming = 0, cache = 0, lpd = 0, pex = 0, dht = 0, ltep = 0,
                toUs = 0, fromUs = 0;
    BOOL anyActive = false;
    for (Torrent * torrent in fTorrents)
    {
        if ([torrent webSeedCount] > 0)
            [fWebSeeds addObjectsFromArray: [torrent webSeeds]];
        
        known += [torrent totalPeersKnown];
        
        if ([torrent isActive])
        {
            anyActive = YES;
            [fPeers addObjectsFromArray: [torrent peers]];
            
            const NSUInteger connectedThis = [torrent totalPeersConnected];
            if (connectedThis > 0)
            {
                connected += [torrent totalPeersConnected];
                tracker += [torrent totalPeersTracker];
                incoming += [torrent totalPeersIncoming];
                cache += [torrent totalPeersCache];
                lpd += [torrent totalPeersLocal];
                pex += [torrent totalPeersPex];
                dht += [torrent totalPeersDHT];
                ltep += [torrent totalPeersLTEP];
                
                toUs += [torrent peersSendingToUs];
                fromUs += [torrent peersGettingFromUs];
            }
        }
    }
    
    [fPeers sortUsingDescriptors: [self peerSortDescriptors]];
    [fPeerTable reloadData];
    
    [fWebSeeds sortUsingDescriptors: [fWebSeedTable sortDescriptors]];
    [fWebSeedTable reloadData];
    
    NSString * knownString = [NSString stringWithFormat: NSLocalizedString(@"%d known", "Inspector -> Peers tab -> peers"), known];
    if (anyActive)
    {
        NSString * connectedText = [NSString stringWithFormat: NSLocalizedString(@"%d Connected", "Inspector -> Peers tab -> peers"),
                                    connected];
        
        if (connected > 0)
        {
            NSMutableArray * fromComponents = [NSMutableArray arrayWithCapacity: 7];
            if (tracker > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d tracker", "Inspector -> Peers tab -> peers"), tracker]];
            if (incoming > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d incoming", "Inspector -> Peers tab -> peers"), incoming]];
            if (cache > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d cache", "Inspector -> Peers tab -> peers"), cache]];
            if (lpd > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d local discovery", "Inspector -> Peers tab -> peers"), lpd]];
            if (pex > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d PEX", "Inspector -> Peers tab -> peers"), pex]];
            if (dht > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d DHT", "Inspector -> Peers tab -> peers"), dht]];
            if (ltep > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d LTEP", "Inspector -> Peers tab -> peers"), ltep]];
            
            NSMutableArray * upDownComponents = [NSMutableArray arrayWithCapacity: 3];
            if (toUs > 0)
                [upDownComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"DL from %d", "Inspector -> Peers tab -> peers"), toUs]];
            if (fromUs > 0)
                [upDownComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"UL to %d", "Inspector -> Peers tab -> peers"), fromUs]];
            [upDownComponents addObject: knownString];
            
            connectedText = [connectedText stringByAppendingFormat: @": %@\n%@", [fromComponents componentsJoinedByString: @", "],
                                [upDownComponents componentsJoinedByString: @", "]];
        }
        else
            connectedText = [connectedText stringByAppendingFormat: @"\n%@", knownString];
        
        [fConnectedPeersField setStringValue: connectedText];
    }
    else
    {
        NSString * activeString;
        if ([fTorrents count] == 1)
            activeString = NSLocalizedString(@"Transfer Not Active", "Inspector -> Peers tab -> peers");
        else
            activeString = NSLocalizedString(@"Transfers Not Active", "Inspector -> Peers tab -> peers");
        
        NSString * connectedText = [activeString stringByAppendingFormat: @"\n%@", knownString];
        [fConnectedPeersField setStringValue: connectedText];
    }
}

- (void) saveViewSize
{
    [[NSUserDefaults standardUserDefaults] setFloat: NSHeight([[self view] frame]) forKey: @"InspectorContentHeightPeers"];
}

- (void) clearView
{
    //if in the middle of animating, just stop and resize immediately
    if (fWebSeedTableAnimation)
        [self setWebSeedTableHidden: !fWebSeeds animate: NO];
    
    [fPeers release];
    fPeers = nil;
    [fWebSeeds release];
    fWebSeeds = nil;
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    if (tableView == fWebSeedTable)
        return fWebSeeds ? [fWebSeeds count] : 0;
    else
        return fPeers ? [fPeers count] : 0;
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (NSInteger) row
{
    if (tableView == fWebSeedTable)
    {
        NSString * ident = [column identifier];
        NSDictionary * webSeed = [fWebSeeds objectAtIndex: row];
        
        if ([ident isEqualToString: @"DL From"])
        {
            NSNumber * rate;
            return (rate = [webSeed objectForKey: @"DL From Rate"]) ? [NSString stringForSpeedAbbrev: [rate doubleValue]] : @"";
        }
        else
            return [webSeed objectForKey: @"Address"];
    }
    else
    {
        NSString * ident = [column identifier];
        NSDictionary * peer = [fPeers objectAtIndex: row];
        
        if ([ident isEqualToString: @"Encryption"])
            return [[peer objectForKey: @"Encryption"] boolValue] ? [NSImage imageNamed: @"Lock.png"] : nil;
        else if ([ident isEqualToString: @"Client"])
            return [peer objectForKey: @"Client"];
        else if  ([ident isEqualToString: @"Progress"])
            return [peer objectForKey: @"Progress"];
        else if ([ident isEqualToString: @"UL To"])
        {
            NSNumber * rate;
            return (rate = [peer objectForKey: @"UL To Rate"]) ? [NSString stringForSpeedAbbrev: [rate doubleValue]] : @"";
        }
        else if ([ident isEqualToString: @"DL From"])
        {
            NSNumber * rate;
            return (rate = [peer objectForKey: @"DL From Rate"]) ? [NSString stringForSpeedAbbrev: [rate doubleValue]] : @"";
        }
        else
            return [peer objectForKey: @"IP"];
    }
}

- (void) tableView: (NSTableView *) tableView willDisplayCell: (id) cell forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    if (tableView == fPeerTable)
    {
        NSString * ident = [tableColumn identifier];
        
        if  ([ident isEqualToString: @"Progress"])
        {
            NSDictionary * peer = [fPeers objectAtIndex: row];
            [(PeerProgressIndicatorCell *)cell setSeed: [[peer objectForKey: @"Seed"] boolValue]];
        }
    }
}

- (void) tableView: (NSTableView *) tableView didClickTableColumn: (NSTableColumn *) tableColumn
{
    if (tableView == fWebSeedTable)
    {
        if (fWebSeeds)
        {
            [fWebSeeds sortUsingDescriptors: [fWebSeedTable sortDescriptors]];
            [tableView reloadData];
        }
    }
    else
    {
        if (fPeers)
        {
            [fPeers sortUsingDescriptors: [self peerSortDescriptors]];
            [tableView reloadData];
        }
    }
}

- (BOOL) tableView: (NSTableView *) tableView shouldSelectRow: (NSInteger) row
{
    return NO;
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (NSInteger) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fPeerTable)
    {
        const BOOL multiple = [fTorrents count] > 1;
        
        NSDictionary * peer = [fPeers objectAtIndex: row];
        NSMutableArray * components = [NSMutableArray arrayWithCapacity: multiple ? 6 : 5];
        
        if (multiple)
            [components addObject: [peer objectForKey: @"Name"]];
        
        const CGFloat progress = [[peer objectForKey: @"Progress"] floatValue];
        NSString * progressString = [NSString stringWithFormat: NSLocalizedString(@"Progress: %@",
                                        "Inspector -> Peers tab -> table row tooltip"),
                                        [NSString percentString: progress longDecimals: NO]];
        if (progress < 1.0 && [[peer objectForKey: @"Seed"] boolValue])
            progressString = [progressString stringByAppendingFormat: @" (%@)", NSLocalizedString(@"Partial Seed",
                                "Inspector -> Peers tab -> table row tooltip")];
        [components addObject: progressString];
        
        if ([[peer objectForKey: @"Encryption"] boolValue])
            [components addObject: NSLocalizedString(@"Encrypted Connection", "Inspector -> Peers tab -> table row tooltip")];
        
        NSString * portString;
        NSInteger port;
        if ((port = [[peer objectForKey: @"Port"] intValue]) > 0)
            portString = [NSString stringWithFormat: @"%d", port];
        else
            portString = NSLocalizedString(@"N/A", "Inspector -> Peers tab -> table row tooltip");
        [components addObject: [NSString stringWithFormat: @"%@: %@", NSLocalizedString(@"Port",
            "Inspector -> Peers tab -> table row tooltip"), portString]];
        
        const NSInteger peerFrom = [[peer objectForKey: @"From"] integerValue];
        switch (peerFrom)
        {
            case TR_PEER_FROM_TRACKER:
                [components addObject: NSLocalizedString(@"From: tracker", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_INCOMING:
                [components addObject: NSLocalizedString(@"From: incoming connection", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_RESUME:
                [components addObject: NSLocalizedString(@"From: cache", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_LPD:
                [components addObject: NSLocalizedString(@"From: local peer discovery", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_PEX:
                [components addObject: NSLocalizedString(@"From: peer exchange", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_DHT:
                [components addObject: NSLocalizedString(@"From: distributed hash table", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_LTEP:
                [components addObject: NSLocalizedString(@"From: libtorrent extension protocol handshake",
                                        "Inspector -> Peers tab -> table row tooltip")];
                break;
            default:
                NSAssert1(NO, @"Peer from unknown source: %d", peerFrom);
        }
        
        //determing status strings from flags
        NSMutableArray * statusArray = [NSMutableArray arrayWithCapacity: 6];
        NSString * flags = [peer objectForKey: @"Flags"];
        
        if ([flags rangeOfString: @"D"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently downloading (interested and not choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"d"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You want to download, but peer does not want to send (interested and choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"U"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently uploading (interested and not choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"u"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer wants you to upload, but you do not want to (interested and choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"K"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer is unchoking you, but you are not interested",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"?"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You unchoked the peer, but the peer is not interested",
                "Inspector -> peer -> status")];
        
        if ([statusArray count] > 0)
        {
            NSString * statusStrings = [statusArray componentsJoinedByString: @"\n\n"];
            [components addObject: [@"\n" stringByAppendingString: statusStrings]];
        }
        
        return [components componentsJoinedByString: @"\n"];
    }
    else
    {
        if ([fTorrents count] > 1)
            return [[fWebSeeds objectAtIndex: row] objectForKey: @"Name"];
    }
    
    return nil;
}

- (void) animationDidEnd: (NSAnimation *) animation
{
    if (animation == fWebSeedTableAnimation)
    {
        [fWebSeedTableAnimation release];
        fWebSeedTableAnimation = nil;
    }
}

- (void) stopWebSeedAnimation
{
    if (fWebSeedTableAnimation)
    {
        [fWebSeedTableAnimation stopAnimation]; // jumps to end frame
        [fWebSeedTableAnimation release];
        fWebSeedTableAnimation = nil;
    }
}

@end

@implementation InfoPeersViewController (Private)

- (void) setupInfo
{
    BOOL hasWebSeeds = NO;
    
    if ([fTorrents count] == 0)
    {
        [fPeers release];
        fPeers = nil;
        [fPeerTable reloadData];
        
        [fConnectedPeersField setStringValue: @""];
    }
    else
    {
        for (Torrent * torrent in fTorrents)
            if ([torrent webSeedCount] > 0)
            {
                hasWebSeeds = YES;
                break;
            }
    }
    
    if (!hasWebSeeds)
    {
        [fWebSeeds release];
        fWebSeeds = nil;
        [fWebSeedTable reloadData];
    }
    [self setWebSeedTableHidden: !hasWebSeeds animate: YES];
    
    fSet = YES;
}

- (void) setWebSeedTableHidden: (BOOL) hide animate: (BOOL) animate
{
    if (animate && (![[self view] window] || ![[[self view] window] isVisible]))
        animate = NO;
    
    if (fWebSeedTableAnimation)
    {
        [fWebSeedTableAnimation stopAnimation];
        [fWebSeedTableAnimation release];
        fWebSeedTableAnimation = nil;
    }
    
    NSRect webSeedFrame = [[fWebSeedTable enclosingScrollView] frame];
    NSRect peerFrame = [[fPeerTable enclosingScrollView] frame];
    
    if (hide)
    {
        CGFloat webSeedFrameMaxY = NSMaxY(webSeedFrame);
        webSeedFrame.size.height = 0;
        webSeedFrame.origin.y = webSeedFrameMaxY;
        
        peerFrame.size.height = webSeedFrameMaxY - peerFrame.origin.y;
    }
    else
    {
        webSeedFrame.origin.y -= fWebSeedTableHeight - webSeedFrame.size.height;
        webSeedFrame.size.height = fWebSeedTableHeight;
        
        peerFrame.size.height = (webSeedFrame.origin.y - fSpaceBetweenWebSeedAndPeer) - peerFrame.origin.y;
    }
    
    [[fWebSeedTable enclosingScrollView] setHidden: NO]; //this is needed for some reason
    
    //actually resize tables
    if (animate)
    {
        NSDictionary * webSeedDict = [NSDictionary dictionaryWithObjectsAndKeys:
                                    [fWebSeedTable enclosingScrollView], NSViewAnimationTargetKey,
                                    [NSValue valueWithRect: [[fWebSeedTable enclosingScrollView] frame]], NSViewAnimationStartFrameKey,
                                    [NSValue valueWithRect: webSeedFrame], NSViewAnimationEndFrameKey, nil],
                    * peerDict = [NSDictionary dictionaryWithObjectsAndKeys:
                                    [fPeerTable enclosingScrollView], NSViewAnimationTargetKey,
                                    [NSValue valueWithRect: [[fPeerTable enclosingScrollView] frame]], NSViewAnimationStartFrameKey,
                                    [NSValue valueWithRect: peerFrame], NSViewAnimationEndFrameKey, nil];
        
        fWebSeedTableAnimation = [[NSViewAnimation alloc] initWithViewAnimations:
                                        [NSArray arrayWithObjects: webSeedDict, peerDict, nil]];
        [fWebSeedTableAnimation setDuration: 0.125];
        [fWebSeedTableAnimation setAnimationBlockingMode: NSAnimationNonblocking];
        [fWebSeedTableAnimation setDelegate: self];
        
        [fWebSeedTableAnimation startAnimation];
    }
    else
    {
        [[fWebSeedTable enclosingScrollView] setFrame: webSeedFrame];
        [[fPeerTable enclosingScrollView] setFrame: peerFrame];
    }
}

- (NSArray *) peerSortDescriptors
{
    NSMutableArray * descriptors = [NSMutableArray arrayWithCapacity: 2];
    
    NSArray * oldDescriptors = [fPeerTable sortDescriptors];
    BOOL useSecond = YES, asc = YES;
    if ([oldDescriptors count] > 0)
    {
        NSSortDescriptor * descriptor = [oldDescriptors objectAtIndex: 0];
        [descriptors addObject: descriptor];
        
        if ((useSecond = ![[descriptor key] isEqualToString: @"IP"]))
            asc = [descriptor ascending];
    }
    
    //sort by IP after primary sort
    if (useSecond)
    {
        #warning when 10.6-only, replace with sortDescriptorWithKey:ascending:selector:
        NSSortDescriptor * secondDescriptor = [[NSSortDescriptor alloc] initWithKey: @"IP" ascending: asc
                                                                        selector: @selector(compareNumeric:)];
        [descriptors addObject: secondDescriptor];
        [secondDescriptor release];
    }
    
    return descriptors;
}

@end
