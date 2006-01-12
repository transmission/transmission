#include <Cocoa/Cocoa.h>
#include <transmission.h>

@class Controller;

@interface TorrentTableView : NSTableView

{
    IBOutlet Controller  * fController;

    tr_stat_t            * fStat;
    NSPoint                fClickPoint;
}

- (void) updateUI: (tr_stat_t *) stat;

@end
