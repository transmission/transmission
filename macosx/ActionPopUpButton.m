#import "ActionPopUpButton.h"
#import "ActionPopUpButtonCell.h"

@implementation ActionPopUpButton

+ (Class) cellClass
{
	return [ActionPopUpButtonCell class];
}

- (id) initWithCoder: (NSCoder *) coder
{
	if (self = [super initWithCoder: coder])
    {
        [self setCell: [[ActionPopUpButtonCell alloc] initTextCell: @"" pullsDown: YES]];
	}
	return self;
}

@end
