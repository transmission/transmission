//
//  Badger.h
//  Transmission
//
//  Created by Mitchell Livingston on 12/21/05.
//

#ifndef BADGER_H
#define BADGER_H

#import <Cocoa/Cocoa.h>

@interface Badger : NSObject {

    NSImage             * fDockIcon, * fBadgedDockIcon,
                        * fBadge, * fUploadBadge, * fDownloadBadge;
                    
    NSMutableDictionary * fAttributes;
    
    int                 fCompleted;
    BOOL                fSpeedShown;
}

- (void) updateBadgeWithCompleted: (int) completed
                    uploadRate: (NSString *) uploadRate
                    downloadRate: (NSString *) downloadRate;
- (void) clearBadge;

@end

#endif
