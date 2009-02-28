/**
 * A C wrapper around James Wynn's FileWatcher library.
 *
 * Released under a free dont-bother-me license. I don't claim this
 * software won't destroy everything that you hold dear, but I really
 * doubt it will. And please try not to take credit for others' work.
 */

#include <iostream>
#include "file-watcher.h"
#include "FileWatcher.h"

using namespace FW;

struct CFW_Impl: public FileWatchListener
{
    private:
        FileWatcher myWatcher;
        WatchID myID;
        CFW_ActionCallback * myCallback;
        void * myCallbackData;

    public:
        CFW_Impl( const char         * dir,
                  CFW_ActionCallback * callback,
                  void               * callbackData ):
            myID( myWatcher.addWatch( dir, this ) ),
            myCallback( callback ),
            myCallbackData( callbackData )
        {
        }
        virtual ~CFW_Impl( )
        {
            myWatcher.removeWatch( myID );
        }

    public:
        virtual void handleFileAction( WatchID                watchid,
                                       const String         & dir,
                                       const String         & filename,
                                       FileWatcher::Action    action )
        {
            (*myCallback)( this,
                           dir.c_str(),
                           filename.c_str(),
                           (CFW_Action)action,
                           myCallbackData );
        }
        void update( )
        {
            myWatcher.update( );
        }
};

extern "C"
{
    CFW_Watch*
    cfw_addWatch( const char * directory, CFW_ActionCallback * callback, void * callbackData )
    {
        return new CFW_Impl( directory, callback, callbackData );
    }

    void
    cfw_removeWatch( CFW_Watch * watch )
    {
        if( watch != 0 )
            delete watch;
    }

    void
    cfw_update( CFW_Watch * watch )
    {
        if( watch != 0 )
            watch->update( );
    }
}
