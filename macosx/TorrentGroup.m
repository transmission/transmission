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

#import "TorrentGroup.h"
#import "Torrent.h"
#include "utils.h" //tr_getRatio()

@implementation TorrentGroup

- (id) initWithGroup: (NSInteger) group
{
    if ((self = [super init]))
    {
        fGroup = group;
        fTorrents = [[NSMutableArray alloc] init];
    }
    return self;
}

- (void) dealloc
{
    [fTorrents release];
    [super dealloc];
}

- (NSInteger) groupIndex
{
    return fGroup;
}

- (NSMutableArray *) torrents
{
    return fTorrents;
}

- (CGFloat) ratio
{
    uint64_t uploaded = 0, downloaded = 0;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
    {
        uploaded += [torrent uploadedTotal];
        downloaded += [torrent downloadedTotal];
    }
    
    return tr_getRatio(uploaded, downloaded);
}

- (CGFloat) uploadRate
{
    CGFloat rate = 0.0f;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        rate += [torrent uploadRate];
    
    return rate;
}

- (CGFloat) downloadRate
{
    CGFloat rate = 0.0f;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        rate += [torrent downloadRate];
    
    return rate;
}

@end
