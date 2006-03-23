#import <Cocoa/Cocoa.h>
#import <transmission.h>

@class Controller;

@interface TorrentTableView : NSTableView

{
    IBOutlet Controller * fController;
    NSArray             * fTorrents;

    NSPoint               fClickPoint;
    
    IBOutlet NSMenu     * fContextRow;
    IBOutlet NSMenu     * fContextNoRow;
}
- (void) setTorrents: (NSArray *) torrents;

@end
