/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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
#import "transmission.h"

@implementation PortChecker

- (id) initWithDelegate: (id) delegate
{
    if ((self = [super init]))
    {
        fDelegate = delegate;
    }
    
    return self;
}

- (port_status_t) status
{
    return fStatus;
}

- (void) probePort: (int) portNumber
{
    NSURLRequest * portProbeRequest = [NSURLRequest requestWithURL: [NSURL URLWithString:
                                [NSString stringWithFormat: @"https://www.grc.com/x/portprobe=%d", portNumber]]
                                cachePolicy: NSURLRequestReloadIgnoringCacheData timeoutInterval: 15.0];
    
    NSURLConnection * portProbeConnection;
    if ((portProbeConnection = [NSURLConnection connectionWithRequest: portProbeRequest delegate: self]))
        fPortProbeData = [[NSMutableData data] retain];
    else
    {
        NSLog(@"Unable to get port status: failed to initiate connection");
        [self callBackWithStatus: PORT_STATUS_ERROR];
    }
}

- (void) callBackWithStatus: (port_status_t) status
{
    fStatus = status;
    
    if (fDelegate && [fDelegate respondsToSelector: @selector(portCheckerDidFinishProbing:)])
        [fDelegate portCheckerDidFinishProbing: self];
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
    [fPortProbeData release];
}

- (void) connectionDidFinishLoading: (NSURLConnection *) connection
{
    NSXMLDocument * shieldsUpProbe = [[NSXMLDocument alloc] initWithData: fPortProbeData
                                        options: NSXMLDocumentTidyHTML error: nil];
    
    if (shieldsUpProbe)
    {
        NSArray * nodes = [shieldsUpProbe nodesForXPath: @"/html/body/center/table[3]/tr/td[2]" error: nil];
        if ([nodes count] != 1)
        {
            NSArray * title = [shieldsUpProbe nodesForXPath: @"/html/head/title" error: nil];
            // This may happen when we probe twice too quickly
            if ([title count] != 1 || ![[[title objectAtIndex: 0] stringValue] isEqualToString:
                                                                    @"NanoProbe System Already In Use"])
            {
                NSLog(@"Unable to get port status: invalid (outdated) XPath expression");
                [[shieldsUpProbe XMLData] writeToFile: @"/tmp/shieldsUpProbe.html" atomically: YES];
                [self callBackWithStatus: PORT_STATUS_ERROR];
            }
        }
        else
        {
            NSString * portStatus = [[[[nodes objectAtIndex: 0] stringValue] stringByTrimmingCharactersInSet:
                                                [[NSCharacterSet letterCharacterSet] invertedSet]] lowercaseString];
            
            if ([portStatus isEqualToString: @"open"])
                [self callBackWithStatus: PORT_STATUS_OPEN];
            else if ([portStatus isEqualToString: @"stealth"])
                [self callBackWithStatus: PORT_STATUS_STEALTH];
            else if ([portStatus isEqualToString: @"closed"])
                [self callBackWithStatus: PORT_STATUS_CLOSED];
            else
            {
                NSLog(@"Unable to get port status: unknown port state");
                [self callBackWithStatus: PORT_STATUS_ERROR];
            }
        }
    }
    else
    {
        NSLog(@"Unable to get port status: failed to create xml document");
        [self callBackWithStatus: PORT_STATUS_ERROR];
    }

    [fPortProbeData release];
}

@end
