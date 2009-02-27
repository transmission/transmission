/**
	Released under a free dont-bother-me license. I don't claim this
	software won't destroy everything that you hold dear, but I really
	doubt it will. And please try not to take credit for others' work.

	@author James Wynn
	@date 4/15/2009
*/

#include "FileWatcher.h"

#if defined(_WIN32)
#	include "FileWatcherWin32.h"
#elif defined(__APPLE_CC__)
#	include "FileWatcherOSX.h"
#elif defined(__linux__)
#	include "FileWatcherLinux.h"
#else
#       error FIXME
#endif

namespace FW
{

	//--------
	FileWatcher::FileWatcher()
	{
		mImpl = new FileWatcherImpl();
	}

	//--------
	FileWatcher::~FileWatcher()
	{
		delete mImpl;
		mImpl = 0;
	}

	//--------
	WatchID FileWatcher::addWatch(const String& directory, FileWatchListener* watcher)
	{
		return mImpl->addWatch(directory, watcher);
	}

	//--------
	void FileWatcher::removeWatch(const String& directory)
	{
		mImpl->removeWatch(directory);
	}

	//--------
	void FileWatcher::removeWatch(WatchID watchid)
	{
		mImpl->removeWatch(watchid);
	}

	//--------
	void FileWatcher::update()
	{
		mImpl->update();
	}

};//namespace FW
