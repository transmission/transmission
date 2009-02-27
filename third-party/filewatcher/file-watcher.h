/**
 * A C wrapper around James Wynn's FileWatcher library.
 *
 * Released under a free dont-bother-me license. I don't claim this
 * software won't destroy everything that you hold dear, but I really
 * doubt it will. And please try not to take credit for others' work.
 */

#ifndef _CFW_FILEWATCHER_H_
#define _CFW_FILEWATCHER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Actions to listen for. Rename will send two events, one for
 * the deletion of the old file, and one for the creation of the
 * new file.
 */
typedef enum
{
    CFW_ACTION_ADD       = (1<<0), /* Sent when a file is created or renamed */
    CFW_ACTION_DELETE    = (1<<1), /* Sent when a file is deleted or renamed */
    CFW_ACTION_MODIFIED  = (1<<2)  /* Sent when a file is modified */
}
CFW_Action;

typedef struct CFW_Impl CFW_Watch;

typedef void ( CFW_ActionCallback )( CFW_Watch*, const char * dir, const char * filename, CFW_Action, void * callbackData );

CFW_Watch*  cfw_addWatch    ( const char * directory, CFW_ActionCallback * callback, void * callbackData );

void        cfw_removeWatch ( CFW_Watch * );

void        cfw_update      ( CFW_Watch * );


#ifdef __cplusplus
}
#endif

#endif
