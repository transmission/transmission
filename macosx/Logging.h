#import <os/log.h>

@interface Logging : NSObject
+ (os_log_t)bonjour;
+ (os_log_t)groups;
+ (os_log_t)keychain;
+ (os_log_t)portChecking;
+ (os_log_t)power;
+ (os_log_t)torrents;
+ (os_log_t)workspace;
@end
