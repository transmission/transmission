#import "DisableTextField.h"

@implementation DisableTextField

- (void) setEnabled: (BOOL) enabled
{
    [super setEnabled: enabled];
    
    if (!enabled)
        [self setTextColor: [NSColor secondarySelectedControlColor]];
    else
        [self setTextColor: [NSColor controlTextColor]];
}

@end
