// This file Copyright © 2008-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BonjourController.h"

#define BONJOUR_SERVICE_NAME_MAX_LENGTH 63

@interface BonjourController ()

@property(nonatomic) NSNetService* fService;

@end

@implementation BonjourController

BonjourController* fDefaultController = nil;

+ (BonjourController*)defaultController
{
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        fDefaultController = [[BonjourController alloc] init];
    });

    return fDefaultController;
}

+ (BOOL)defaultControllerExists
{
    return fDefaultController != nil;
}

- (void)startWithPort:(int)port
{
    [self stop];

    NSMutableString* serviceName = [NSMutableString
        stringWithFormat:@"Transmission (%@ - %@)", NSUserName(), [NSHost currentHost].localizedName];
    if (serviceName.length > BONJOUR_SERVICE_NAME_MAX_LENGTH)
    {
        [serviceName deleteCharactersInRange:NSMakeRange(BONJOUR_SERVICE_NAME_MAX_LENGTH, serviceName.length - BONJOUR_SERVICE_NAME_MAX_LENGTH)];
    }

    self.fService = [[NSNetService alloc] initWithDomain:@"" type:@"_http._tcp." name:serviceName port:port];
    self.fService.delegate = self;

    [self.fService publish];
}

- (void)stop
{
    [self.fService stop];
    self.fService = nil;
}

- (void)netService:(NSNetService*)sender didNotPublish:(NSDictionary*)errorDict
{
    NSLog(@"Failed to publish the web interface service on port %ld, with error: %@", sender.port, errorDict);
}

- (void)netService:(NSNetService*)sender didNotResolve:(NSDictionary*)errorDict
{
    NSLog(@"Failed to resolve the web interface service on port %ld, with error: %@", sender.port, errorDict);
}

@end
