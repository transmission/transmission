#import <Cocoa/Cocoa.h>
#import <transmission.h>

@class Controller;

@interface TorrentTableView : NSTableView

{
    IBOutlet Controller  * fController;

    tr_stat_t            * fStat;
    NSPoint                fClickPoint;
    
    IBOutlet NSMenu     * fContextRow;
    IBOutlet NSMenu     * fContextNoRow;
}

- (void) updateUI: (tr_stat_t *) stat;

@end
