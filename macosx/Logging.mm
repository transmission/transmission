#import "Logging.h"
#import <os/log.h>

static auto const subsystem = "org.transmission";

@implementation Logging : NSObject
+ (os_log_t)bonjour
{
    return os_log_create(subsystem, "bonjour");
}
+ (os_log_t)groups
{
    return os_log_create(subsystem, "groups");
}
+ (os_log_t)keychain
{
    return os_log_create(subsystem, "keychain");
}
+ (os_log_t)portChecking
{
    return os_log_create(subsystem, "port_checking");
}
+ (os_log_t)power
{
    return os_log_create(subsystem, "power");
}
+ (os_log_t)torrents
{
    return os_log_create(subsystem, "torrents");
}
+ (os_log_t)workspace
{
    return os_log_create(subsystem, "workspace");
}
@end
