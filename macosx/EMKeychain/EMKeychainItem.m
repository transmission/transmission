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

#import "EMKeychainItem.h"

@implementation EMKeychainItem
- (id)initWithCoreKeychainItem:(SecKeychainItemRef)item username:(NSString *)username password:(NSString *)password
{
	if ((self = [super init]))
	{
		coreKeychainItem = item;
		[self setValue:username forKey:@"myUsername"];
		[self setValue:password forKey:@"myPassword"];
		return self;
	}
	return nil;
}
- (NSString *)password
{
	return myPassword;
}
- (NSString *)username
{
	return myUsername;
}
- (NSString *)label
{
	return myLabel;
}

- (BOOL)setPassword:(NSString *)newPasswordString
{
	if (!newPasswordString)
	{
		return NO;
	}
	[self willChangeValueForKey:@"password"];
	[myPassword autorelease];
	myPassword = [newPasswordString copy];
	[self didChangeValueForKey:@"password"];
	
	const char *newPassword = [newPasswordString UTF8String];
	OSStatus returnStatus = SecKeychainItemModifyAttributesAndData(coreKeychainItem, NULL, strlen(newPassword), (void *)newPassword);
	return (returnStatus == noErr);	
}
- (BOOL)setUsername:(NSString *)newUsername
{
	[self willChangeValueForKey:@"username"];
	[myUsername autorelease];
	myUsername = [newUsername copy];
	[self didChangeValueForKey:@"username"];	
	
	return [self modifyAttributeWithTag:kSecAccountItemAttr toBeString:newUsername];
}
- (BOOL)setLabel:(NSString *)newLabel
{
	[self willChangeValueForKey:@"label"];
	[myLabel autorelease];
	myLabel = [newLabel copy];
	[self didChangeValueForKey:@"label"];
	
	return [self modifyAttributeWithTag:kSecLabelItemAttr toBeString:newLabel];
}
@end

@implementation EMKeychainItem (Private)
- (BOOL)modifyAttributeWithTag:(SecItemAttr)attributeTag toBeString:(NSString *)newStringValue
{
	const char *newValue = [newStringValue UTF8String];
	SecKeychainAttribute attributes[1];
	attributes[0].tag = attributeTag;
	attributes[0].length = strlen(newValue);
	attributes[0].data = (void *)newValue;
	
	SecKeychainAttributeList list;
	list.count = 1;
	list.attr = attributes;
	
	OSStatus returnStatus = SecKeychainItemModifyAttributesAndData(coreKeychainItem, &list, 0, NULL);
	return (returnStatus == noErr);
}
@end

@implementation EMGenericKeychainItem
- (id)initWithCoreKeychainItem:(SecKeychainItemRef)item serviceName:(NSString *)serviceName username:(NSString *)username password:(NSString *)password
{
	if ((self = [super initWithCoreKeychainItem:item username:username password:password]))
	{
		[self setValue:serviceName forKey:@"myServiceName"];
		return self;
	}
	return nil;
}
+ (id)genericKeychainItem:(SecKeychainItemRef)item forServiceName:(NSString *)serviceName username:(NSString *)username password:(NSString *)password
{
	return [[[EMGenericKeychainItem alloc] initWithCoreKeychainItem:item serviceName:serviceName username:username password:password] autorelease];
}
- (NSString *)serviceName
{
	return myServiceName;
}

- (BOOL)setServiceName:(NSString *)newServiceName
{
	[self willChangeValueForKey:@"serviceName"];
	[myServiceName autorelease];
	myServiceName = [newServiceName copy];
	[self didChangeValueForKey:@"serviceName"];	
	
	return [self modifyAttributeWithTag:kSecServiceItemAttr toBeString:newServiceName];
}
@end

@implementation EMInternetKeychainItem
- (id)initWithCoreKeychainItem:(SecKeychainItemRef)item server:(NSString *)server username:(NSString *)username password:(NSString *)password path:(NSString *)path port:(int)port protocol:(SecProtocolType)protocol
{
	if ((self = [super initWithCoreKeychainItem:item username:username password:password]))
	{
		[self setValue:server forKey:@"myServer"];
		[self setValue:path forKey:@"myPath"];
		[self setValue:[NSNumber numberWithInt:port] forKey:@"myPort"];
		[self setValue:[NSNumber numberWithInt:protocol] forKey:@"myProtocol"];
		return self;
	}
	return nil;
}
+ (id)internetKeychainItem:(SecKeychainItemRef)item forServer:(NSString *)server username:(NSString *)username password:(NSString *)password path:(NSString *)path port:(int)port protocol:(SecProtocolType)protocol
{
	return [[[EMInternetKeychainItem alloc] initWithCoreKeychainItem:item server:server username:username password:password path:path port:port protocol:protocol] autorelease];
}
- (NSString *)server
{
	return myServer;
}
- (NSString *)path
{
	return myPath;
}
- (int)port
{
	return myPort;
}
- (SecProtocolType)protocol
{
	return myProtocol;
}

- (BOOL)setServer:(NSString *)newServer
{
	[self willChangeValueForKey:@"server"];
	[myServer autorelease];
	myServer = [newServer copy];	
	[self didChangeValueForKey:@"server"];
	
	return [self modifyAttributeWithTag:kSecServerItemAttr toBeString:newServer];
}
- (BOOL)setPath:(NSString *)newPath
{
	[self willChangeValueForKey:@"path"];
	[myPath autorelease];
	myPath = [newPath copy];
	[self didChangeValueForKey:@"path"];
	
	return [self modifyAttributeWithTag:kSecPathItemAttr toBeString:newPath];
}
- (BOOL)setPort:(int)newPort
{
	[self willChangeValueForKey:@"port"];
	myPort = newPort;
	[self didChangeValueForKey:@"port"];
	
	return [self modifyAttributeWithTag:kSecPortItemAttr toBeString:[NSString stringWithFormat:@"%i", newPort]];
}
- (BOOL)setProtocol:(SecProtocolType)newProtocol
{
	[self willChangeValueForKey:@"protocol"];
	myProtocol = newProtocol;
	[self didChangeValueForKey:@"protocol"];
	
	SecKeychainAttribute attributes[1];
	attributes[0].tag = kSecProtocolItemAttr;
	attributes[0].length = sizeof(newProtocol);
	attributes[0].data = (void *)newProtocol;
	
	SecKeychainAttributeList list;
	list.count = 1;
	list.attr = attributes;
	
	OSStatus returnStatus = SecKeychainItemModifyAttributesAndData(coreKeychainItem, &list, 0, NULL);
	return (returnStatus == noErr);
}
@end