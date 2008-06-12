/*Copyright (c) 2007 Extendmac, LLC. <support@extendmac.com>

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

//Last Changed on 2/07/08. Version 0.14

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <Security/Security.h>

#import "EMKeychainItem.h"

@interface EMKeychainProxy : NSObject 
{
	BOOL _logErrors;
}
+ (id)sharedProxy;
- (void)lockKeychain;
- (void)unlockKeychain;
- (void)setLogsErrors:(BOOL)flag;

- (EMGenericKeychainItem *)genericKeychainItemForService:(NSString *)serviceNameString withUsername:(NSString *)usernameString;
- (EMInternetKeychainItem *)internetKeychainItemForServer:(NSString *)serverString withUsername:(NSString *)usernameString path:(NSString *)pathString port:(int)port protocol:(SecProtocolType)protocol;

- (EMGenericKeychainItem *)addGenericKeychainItemForService:(NSString *)serviceNameString withUsername:(NSString *)usernameString password:(NSString *)passwordString;
- (EMInternetKeychainItem *)addInternetKeychainItemForServer:(NSString *)serverString withUsername:(NSString *)usernameString password:(NSString *)passwordString path:(NSString *)pathString port:(int)port protocol:(SecProtocolType)protocol;

@end
