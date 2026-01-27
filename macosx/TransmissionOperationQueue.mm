// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TransmissionOperationQueue.h"

@interface TransmissionOperationQueue ()

@property(nonatomic, readonly) dispatch_queue_t queue;

@end

@implementation TransmissionOperationQueue

+ (instancetype)sharedQueue
{
    static TransmissionOperationQueue* sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[TransmissionOperationQueue alloc] init];
    });
    return sharedInstance;
}

- (instancetype)init
{
    if ((self = [super init]))
    {
        _queue = dispatch_queue_create("com.transmissionbt.Transmission.OperationQueue", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

- (void)async:(void (^)(void))operation
{
    NSParameterAssert(operation != nil);
    dispatch_async(self.queue, operation);
}

- (void)async:(void (^)(void))operation completion:(void (^)(void))completion
{
    NSParameterAssert(operation != nil);

    dispatch_async(self.queue, ^{
        operation();

        if (completion != nil)
        {
            dispatch_async(dispatch_get_main_queue(), completion);
        }
    });
}

- (void)asyncWithResult:(id _Nullable (^)(void))operation completion:(void (^)(id _Nullable result))completion
{
    NSParameterAssert(operation != nil);
    NSParameterAssert(completion != nil);

    dispatch_async(self.queue, ^{
        id result = operation();
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(result);
        });
    });
}

@end
