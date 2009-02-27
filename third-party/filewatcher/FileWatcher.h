/**
	Released under a free dont-bother-me license. I don't claim this
	software won't destroy everything that you hold dear, but I really
	doubt it will. And please try not to take credit for others' work.

	@author James Wynn
	@date 4/15/2009
*/

/**
	Main header for the FileWatcher class. Declares all implementation
	classes to reduce compilation overhead.
*/

#ifndef _FW_FILEWATCHER_H_
#define _FW_FILEWATCHER_H_
#pragma once

#include <string>

namespace FW
{
	/// Type for a string
	typedef std::string String;
	/// Type for a watch id
	typedef unsigned long WatchID;

	// forward decl
	class FileWatchListener;
	class FileWatcher32;
	class FileWatcherLinux;
	class FileWatcherOSX;
	

#if defined(_WIN32)
	typedef class FileWatcherWin32 FileWatcherImpl;
#elif defined(__APPLE_CC__)
	typedef FileWatcherOSX FileWatcherImpl;
#elif defined(__linux__)
	typedef class FileWatcherLinux FileWatcherImpl;
#endif

	/// Listens to files and directories and dispatches events
	/// to notify the parent program of the changes.
	/// @class FileWatcher
	class FileWatcher
	{
	public:

	public:
		/// Actions to listen for. Rename will send two events, one for
		/// the deletion of the old file, and one for the creation of the
		/// new file.
		enum Action
		{
			/// Sent when a file is created or renamed
			ACTION_ADD = 1,
			/// Sent when a file is deleted or renamed
			ACTION_DELETE = 2,
			/// Sent when a file is modified
			ACTION_MODIFIED = 4
		};

	public:
		///
		///
		FileWatcher();

		///
		///
		virtual ~FileWatcher();

		/// Add a directory watch
		WatchID addWatch(const String& directory, FileWatchListener* watcher);

		/// Remove a directory watch. This is a brute force search O(nlogn).
		void removeWatch(const String& directory);

		/// Remove a directory watch. This is a map lookup O(logn).
		void removeWatch(WatchID watchid);

		/// Updates the watcher. Must be called often.
		void update();

	private:
		/// The implementation
		FileWatcherImpl* mImpl;

	};//end FileWatcher


	/// Basic interface for listening for file events.
	/// @class FileWatchListener
	class FileWatchListener
	{
	public:
		FileWatchListener() {}
		virtual ~FileWatchListener() {}

		/// Handles the action file action
		/// @param watchid The watch id for the directory
		/// @param dir The directory
		/// @param filename The filename that was accessed (not full path)
		/// @param action Action that was performed
		virtual void handleFileAction(WatchID watchid, const String& dir, const String& filename, FileWatcher::Action action) = 0;

	};//class FileWatchListener

};//namespace FW

#endif//_FW_FILEWATCHER_H_
