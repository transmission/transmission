// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PortChecker.h"

static NSTimeInterval const kCheckFireInterval = 3.0;

@interface PortChecker ()

@property(nonatomic, weak) NSObject<PortCheckerDelegate>* fDelegate;
@property(nonatomic) port_status_t fStatus;

@property(nonatomic) NSURLSession* fSession;
@property(nonatomic) NSURLSessionDataTask* fTask;

@property(nonatomic) NSTimer* fTimer;

@end

@implementation PortChecker

- (instancetype)initForPort:(NSInteger)portNumber delay:(BOOL)delay withDelegate:(NSObject<PortCheckerDelegate>*)delegate
{
    if ((self = [super init]))
    {
        _fSession = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration delegate:nil
                                             delegateQueue:nil];
        _fDelegate = delegate;

        _fStatus = PORT_STATUS_CHECKING;

        _fTimer = [NSTimer scheduledTimerWithTimeInterval:kCheckFireInterval target:self selector:@selector(startProbe:)
                                                 userInfo:@(portNumber)
                                                  repeats:NO];
        if (!delay)
        {
            [_fTimer fire];
        }
    }

    return self;
}

- (void)dealloc
{
    [self cancelProbe];
}

- (port_status_t)status
{
    return self.fStatus;
}

- (void)cancelProbe
{
    [self.fTimer invalidate];
    self.fTimer = nil;

    [self.fTask cancel];
}

#pragma mark - Private

- (void)startProbe:(NSTimer*)timer
{
    self.fTimer = nil;

    NSString* urlString = [NSString stringWithFormat:@"https://portcheck.transmissionbt.com/%ld", [(NSNumber*)timer.userInfo integerValue]];
    NSURLRequest* portProbeRequest = [NSURLRequest requestWithURL:[NSURL URLWithString:urlString]
                                                      cachePolicy:NSURLRequestReloadIgnoringLocalAndRemoteCacheData
                                                  timeoutInterval:15.0];

    _fTask = [_fSession dataTaskWithRequest:portProbeRequest
                          completionHandler:^(NSData* _Nullable data, NSURLResponse* _Nullable response, NSError* _Nullable error) {
                              if (error)
                              {
                                  NSLog(@"Unable to get port status: connection failed (%@)", error.localizedDescription);
                                  [self callBackWithStatus:PORT_STATUS_ERROR];
                                  return;
                              }
                              NSString* probeString = [[NSString alloc] initWithData:data ?: NSData.data encoding:NSUTF8StringEncoding];
                              if (!probeString)
                              {
                                  NSLog(@"Unable to get port status: invalid data received");
                                  [self callBackWithStatus:PORT_STATUS_ERROR];
                              }
                              else if ([probeString isEqualToString:@"1"])
                              {
                                  [self callBackWithStatus:PORT_STATUS_OPEN];
                              }
                              else if ([probeString isEqualToString:@"0"])
                              {
                                  [self callBackWithStatus:PORT_STATUS_CLOSED];
                              }
                              else
                              {
                                  NSLog(@"Unable to get port status: invalid response (%@)", probeString);
                                  [self callBackWithStatus:PORT_STATUS_ERROR];
                              }
                          }];
    [_fTask resume];
}

- (void)callBackWithStatus:(port_status_t)status
{
    self.fStatus = status;

    NSObject<PortCheckerDelegate>* delegate = self.fDelegate;
    [delegate performSelectorOnMainThread:@selector(portCheckerDidFinishProbing:) withObject:self waitUntilDone:NO];
}

@end
