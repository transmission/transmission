// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PortChecker.h"

#define CHECKER_URL(port) [NSString stringWithFormat:@"https://portcheck.transmissionbt.com/%ld", port]
#define CHECK_FIRE 3.0

@interface PortChecker ()

@property(nonatomic, weak) NSObject<PortCheckerDelegate>* fDelegate;
@property(nonatomic) port_status_t fStatus;

@property(nonatomic) NSURLConnection* fConnection;
@property(nonatomic) NSMutableData* fPortProbeData;

@property(nonatomic) NSTimer* fTimer;

- (void)startProbe:(NSTimer*)timer;

- (void)callBackWithStatus:(port_status_t)status;

@end

@implementation PortChecker

- (instancetype)initForPort:(NSInteger)portNumber delay:(BOOL)delay withDelegate:(NSObject<PortCheckerDelegate>*)delegate
{
    if ((self = [super init]))
    {
        _fDelegate = delegate;

        _fStatus = PORT_STATUS_CHECKING;

        _fTimer = [NSTimer scheduledTimerWithTimeInterval:CHECK_FIRE target:self selector:@selector(startProbe:)
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
    [_fTimer invalidate];
}

- (port_status_t)status
{
    return self.fStatus;
}

- (void)cancelProbe
{
    [self.fTimer invalidate];
    self.fTimer = nil;

    [self.fConnection cancel];
}

- (void)connection:(NSURLConnection*)connection didReceiveResponse:(NSURLResponse*)response
{
    self.fPortProbeData.length = 0;
}

- (void)connection:(NSURLConnection*)connection didReceiveData:(NSData*)data
{
    [self.fPortProbeData appendData:data];
}

- (void)connection:(NSURLConnection*)connection didFailWithError:(NSError*)error
{
    NSLog(@"Unable to get port status: connection failed (%@)", error.localizedDescription);
    [self callBackWithStatus:PORT_STATUS_ERROR];
}

- (void)connectionDidFinishLoading:(NSURLConnection*)connection
{
    NSString* probeString = [[NSString alloc] initWithData:self.fPortProbeData encoding:NSUTF8StringEncoding];
    self.fPortProbeData = nil;

    if (probeString)
    {
        if ([probeString isEqualToString:@"1"])
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
    }
    else
    {
        NSLog(@"Unable to get port status: invalid data received");
        [self callBackWithStatus:PORT_STATUS_ERROR];
    }
}

#pragma mark - Private

- (void)startProbe:(NSTimer*)timer
{
    self.fTimer = nil;

    NSURLRequest* portProbeRequest = [NSURLRequest requestWithURL:[NSURL URLWithString:CHECKER_URL([[timer userInfo] integerValue])]
                                                      cachePolicy:NSURLRequestReloadIgnoringLocalAndRemoteCacheData
                                                  timeoutInterval:15.0];

    if ((self.fConnection = [[NSURLConnection alloc] initWithRequest:portProbeRequest delegate:self]))
    {
        self.fPortProbeData = [[NSMutableData alloc] init];
    }
    else
    {
        NSLog(@"Unable to get port status: failed to initiate connection");
        [self callBackWithStatus:PORT_STATUS_ERROR];
    }
}

- (void)callBackWithStatus:(port_status_t)status
{
    self.fStatus = status;

    if (self.fDelegate && [self.fDelegate respondsToSelector:@selector(portCheckerDidFinishProbing:)])
    {
        [self.fDelegate performSelectorOnMainThread:@selector(portCheckerDidFinishProbing:) withObject:self waitUntilDone:NO];
    }
}

@end
