/**
	Released under a free dont-bother-me license. I don't claim this
	software won't destroy everything that you hold dear, but I really
	doubt it will. And please try not to take credit for others' work.

	@author James Wynn
	@date 4/15/2009
*/

#if defined(WITH_KQUEUE) || defined(__APPLE_CC__)

#include "FileWatcherOSX.h"

#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>


namespace FW
{
	
	struct kevent change;
	struct kevent event;

	struct WatchStruct
	{
		WatchID mWatchID;
		String mDirName;
		FileWatchListener* mListener;		
	};

	//--------
	FileWatcherOSX::FileWatcherOSX()
	{
		mDescriptor = kqueue();
		mTimeOut.tv_sec = 0;
		mTimeOut.tv_nsec = 0;
	}

	//--------
	FileWatcherOSX::~FileWatcherOSX()
	{
		WatchMap::iterator iter = mWatches.begin();
		WatchMap::iterator end = mWatches.end();
		for(; iter != end; ++iter)
		{
			delete iter->second;
		}
		mWatches.clear();
		
		close(mDescriptor);
	}

	//--------
	WatchID FileWatcherOSX::addWatch(const String& directory, FileWatchListener* watcher)
	{
		int fd = open(directory.c_str(), O_RDONLY);
		if(fd == -1)
			perror("open");
				
		EV_SET(&change, fd, EVFILT_VNODE,
			   EV_ADD | EV_ENABLE | EV_ONESHOT,
			   NOTE_DELETE | NOTE_EXTEND | NOTE_WRITE | NOTE_ATTRIB,
			   0, (void*)"testing");
		
		return 0;
	}

	//--------
	void FileWatcherOSX::removeWatch(const String& directory)
	{
		WatchMap::iterator iter = mWatches.begin();
		WatchMap::iterator end = mWatches.end();
		for(; iter != end; ++iter)
		{
			if(directory == iter->second->mDirName)
			{
				removeWatch(iter->first);
				return;
			}
		}
	}

	//--------
	void FileWatcherOSX::removeWatch(WatchID watchid)
	{
		WatchMap::iterator iter = mWatches.find(watchid);

		if(iter == mWatches.end())
			return;

		WatchStruct* watch = iter->second;
		mWatches.erase(iter);
	
		//inotify_rm_watch(mFD, watchid);
		
		delete watch;
		watch = 0;
	}

	//--------
	void FileWatcherOSX::update()
	{
		int nev = kevent(mDescriptor, &change, 1, &event, 1, &mTimeOut);
		
		if(nev == -1)
			perror("kevent");
		else if (nev > 0)
		{
			printf("File: %s -- ", (char*)event.udata);
			if(event.fflags & NOTE_DELETE)
			{
				printf("File deleted\n");
			}
			if(event.fflags & NOTE_EXTEND ||
			   event.fflags & NOTE_WRITE)
				printf("File modified\n");
			if(event.fflags & NOTE_ATTRIB)
				printf("File attributes modified\n");
		}
	}

	//--------
	void FileWatcherOSX::handleAction(WatchStruct* watch, const String& filename, unsigned long action)
	{
	}

};//namespace FW

#endif // WITH_KQUEUE || __APPLE_CC__
