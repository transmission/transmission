// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// A serial dispatch queue for executing libtransmission operations off the main thread.
/// All completions are dispatched back to the main queue.
@interface TransmissionOperationQueue : NSObject

/// Returns the shared singleton instance.
+ (instancetype)sharedQueue;

/// Executes an operation asynchronously on the operation queue.
/// Fire-and-forget: no completion callback.
/// @param operation The block to execute on the background queue.
- (void)async:(void (^)(void))operation;

/// Executes an operation asynchronously on the operation queue,
/// then calls the completion block on the main queue.
/// @param operation The block to execute on the background queue.
/// @param completion The block to call on the main queue after operation completes. May be nil.
- (void)async:(void (^)(void))operation completion:(nullable void (^)(void))completion;

/// Executes an operation that returns a result asynchronously on the operation queue,
/// then calls the completion block with the result on the main queue.
/// @param operation The block to execute on the background queue. Returns a result object.
/// @param completion The block to call on the main queue with the result. Must not be nil.
- (void)asyncWithResult:(id _Nullable (^)(void))operation completion:(void (^)(id _Nullable result))completion;

@end

NS_ASSUME_NONNULL_END
