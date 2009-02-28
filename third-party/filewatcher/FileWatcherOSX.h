/**
	Released under a free dont-bother-me license. I don't claim this
	software won't destroy everything that you hold dear, but I really
	doubt it will. And please try not to take credit for others' work.

	@author James Wynn
	@date 4/15/2009
*/

/**
	Implementation header file for OSX based on FSEvent.
*/

#ifndef _FW_FILEWATCHEROSX_H_
#define _FW_FILEWATCHEROSX_H_
#pragma once

#include "FileWatcher.h"
#include <map>
#include <sys/types.h>

namespace FW
{

	// forward decl
	struct WatchStruct;

	///
	/// @class FileWatcherOSX
	class FileWatcherOSX
	{
	public:
		/// type for a map from WatchID to WatchStruct pointer
		typedef std::map<WatchID, WatchStruct*> WatchMap;

	public:
		///
		///
		FileWatcherOSX();

		///
		///
		virtual ~FileWatcherOSX();

		/// Add a directory watch
		WatchID addWatch(const String& directory, FileWatchListener* watcher);

		/// Remove a directory watch. This is a brute force lazy search O(nlogn).
		void removeWatch(const String& directory);

		/// Remove a directory watch. This is a map lookup O(logn).
		void removeWatch(WatchID watchid);

		/// Updates the watcher. Must be called often.
		void update();

		/// Handles the action
		void handleAction(WatchStruct* watch, const String& filename, unsigned long action);

	private:
		/// Map of WatchID to WatchStruct pointers
		WatchMap mWatches;
		/// The descriptor for the kqueue
		int mDescriptor;
		/// time out data
		struct timespec mTimeOut;

	};//end FileWatcherOSX

};//namespace FW

#endif//_FW_FILEWATCHEROSX_H_
