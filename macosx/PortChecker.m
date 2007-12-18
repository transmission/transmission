/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "PortChecker.h"
#import "NSApplicationAdditions.h"

@implementation PortChecker

- (id) initForPort: (int) portNumber withDelegate: (id) delegate
{
    if ((self = [super init]))
    {
        fDelegate = delegate;
        
        NSURLRequest * portProbeRequest = [NSURLRequest requestWithURL: [NSURL URLWithString:
                [NSString stringWithFormat: @"http://transmission.m0k.org/PortCheck.php?port=%d", portNumber]] cachePolicy:
                [NSApp isOnLeopardOrBetter] ? NSURLRequestReloadIgnoringLocalCacheData : NSURLRequestReloadIgnoringCacheData
                timeoutInterval: 15.0];
        
        fStatus = PORT_STATUS_CHECKING;
        
        if ((fConnection = [[NSURLConnection alloc] initWithRequest: portProbeRequest delegate: self]))
            fPortProbeData = [[NSMutableData alloc] init];
        else
        {
            NSLog(@"Unable to get port status: failed to initiate connection");
            [self callBackWithStatus: PORT_STATUS_ERROR];
        }
    }
    
    return self;
}

- (void) dealloc
{
    [fConnection release];
    [fPortProbeData release];
    [super dealloc];
}

- (port_status_t) status
{
    return fStatus;
}

- (void) endProbe
{
    [fConnection cancel];
}

- (void) callBackWithStatus: (port_status_t) status
{
    fStatus = status;
    
    if (fDelegate && [fDelegate respondsToSelector: @selector(portCheckerDidFinishProbing:)])
        [fDelegate performSelectorOnMainThread: @selector(portCheckerDidFinishProbing:) withObject: self waitUntilDone: NO];
}

#pragma mark NSURLConnection delegate methods

- (void) connection: (NSURLConnection *) connection didReceiveResponse: (NSURLResponse *) response
{
    [fPortProbeData setLength: 0];
}

- (void) connection: (NSURLConnection *) connection didReceiveData: (NSData *) data
{
    [fPortProbeData appendData: data];
}

- (void) connection: (NSURLConnection *) connection didFailWithError: (NSError *) error
{
    NSLog(@"Unable to get port status: connection failed (%@)", [error localizedDescription]);
    [self callBackWithStatus: PORT_STATUS_ERROR];
}

- (void) connectionDidFinishLoading: (NSURLConnection *) connection
{
    NSString * probeString = [[NSString alloc] initWithData: fPortProbeData encoding: NSASCIIStringEncoding];
    
    port_status_t status;
    if ([probeString isEqualToString: @"1"])
        status = PORT_STATUS_OPEN;
    else if ([probeString isEqualToString: @"0"])
        status = PORT_STATUS_CLOSED;
    else
        status = PORT_STATUS_ERROR;
    
    [self callBackWithStatus: status];
    [probeString release];
}

@end
