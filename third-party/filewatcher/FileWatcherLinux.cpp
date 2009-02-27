/**
	Released under a free dont-bother-me license. I don't claim this
	software won't destroy everything that you hold dear, but I really
	doubt it will. And please try not to take credit for others' work.

	@author James Wynn
	@date 4/15/2009
*/

#ifdef __linux__

#include "FileWatcherLinux.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/inotify.h>

#define BUFF_SIZE ((sizeof(struct inotify_event)+FILENAME_MAX)*1024)

namespace FW
{

	struct WatchStruct
	{
		WatchID mWatchID;
		String mDirName;
		FileWatchListener* mListener;		
	};

	//--------
	FileWatcherLinux::FileWatcherLinux()
	{
		mFD = inotify_init();
		if (mFD < 0)
			fprintf (stderr, "Error: %s\n", strerror(errno));
		
		mTimeOut.tv_sec = 0;
		mTimeOut.tv_usec = 0;
	   		
		FD_ZERO(&mDescriptorSet);
	}

	//--------
	FileWatcherLinux::~FileWatcherLinux()
	{
		WatchMap::iterator iter = mWatches.begin();
		WatchMap::iterator end = mWatches.end();
		for(; iter != end; ++iter)
		{
			delete iter->second;
		}
		mWatches.clear();
	}

	//--------
	WatchID FileWatcherLinux::addWatch(const String& directory, FileWatchListener* watcher)
	{
		int wd = inotify_add_watch (mFD, directory.c_str(), 
			IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MOVED_FROM | IN_DELETE);
		if (wd < 0)
		{
			fprintf (stderr, "Error: %s\n", strerror(errno));
			return -1;
		}
		
		WatchStruct* pWatch = new WatchStruct();
		pWatch->mListener = watcher;
		pWatch->mWatchID = wd;
		pWatch->mDirName = directory;
		
		mWatches.insert(std::make_pair(wd, pWatch));
	
		return wd;
	}

	//--------
	void FileWatcherLinux::removeWatch(const String& directory)
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
	void FileWatcherLinux::removeWatch(WatchID watchid)
	{
		WatchMap::iterator iter = mWatches.find(watchid);

		if(iter == mWatches.end())
			return;

		WatchStruct* watch = iter->second;
		mWatches.erase(iter);
	
		inotify_rm_watch(mFD, watchid);
		
		delete watch;
		watch = 0;
	}

	//--------
	void FileWatcherLinux::update()
	{
		FD_SET(mFD, &mDescriptorSet);

		int ret = select(mFD + 1, &mDescriptorSet, NULL, NULL, &mTimeOut);
		if(ret < 0)
		{
			perror("select");
		}
		else if(FD_ISSET(mFD, &mDescriptorSet))
		{
			ssize_t len, i = 0;
			char action[81+FILENAME_MAX] = {0};
			char buff[BUFF_SIZE] = {0};

			len = read (mFD, buff, BUFF_SIZE);
		   
			while (i < len)
			{
				struct inotify_event *pevent = (struct inotify_event *)&buff[i];

				WatchStruct* watch = mWatches[pevent->wd];
				handleAction(watch, pevent->name, pevent->mask);
				i += sizeof(struct inotify_event) + pevent->len;
			}
		}
	}

	//--------
	void FileWatcherLinux::handleAction(WatchStruct* watch, const String& filename, unsigned long action)
	{
		if(!watch->mListener)
			return;

		if(IN_CLOSE_WRITE & action)
		{
			watch->mListener->handleFileAction(watch->mWatchID, watch->mDirName, filename,
								FileWatcher::ACTION_MODIFIED);
		}
		if(IN_MOVED_TO & action || IN_CREATE & action)
		{
			watch->mListener->handleFileAction(watch->mWatchID, watch->mDirName, filename,
								FileWatcher::ACTION_ADD);
		}
		if(IN_MOVED_FROM & action || IN_DELETE & action)
		{
			watch->mListener->handleFileAction(watch->mWatchID, watch->mDirName, filename,
								FileWatcher::ACTION_DELETE);
		}
	}

};//namespace FW


#endif//__linux__
