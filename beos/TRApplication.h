/**
 * Copyright (C) 2007 Bryan Varner
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * $Id:$
 */

// $Id$

#ifndef TR_APP
#define TR_APP

#include <Application.h>
#include <Directory.h>
#include <FilePanel.h>
#include <Message.h>

#include "TRWindow.h"

#define APP_SIG "application/x-vnd.titer-Transmission"
#define TRANSMISSION_SETTINGS "Transmission/settings"


#define TR_ADD 'tAdd'
#define TR_OPEN 'tOpn'
#define TR_RELOAD_SETTINGS 'tRSt'

class TRApplication : public BApplication {
	public:
		TRApplication();
		~TRApplication();
		
		virtual void AboutRequested();
		virtual void Pulse();
		virtual void ReadyToRun();
		virtual void RefsReceived(BMessage *message);
		virtual void ArgvReceived(int32 _argc, char** _argv);
		virtual bool QuitRequested();

		virtual void MessageReceived(BMessage *message);
		
		static int32 Copy(void *data);
		
		status_t InitCheck();
		inline BDirectory* TorrentDir() { return torrentDir; };
	private:
		TRWindow *window;
		TRPrefsWindow *settings;
		BFilePanel *openPanel;
		BDirectory *torrentDir;
};

/** Torrent File-Type Filter */
class TRFilter : public BRefFilter {
public:
	virtual bool Filter(const entry_ref *ref, BNode *node, 
	                    struct stat *st, const char *mimetype);
};

#endif /* TR_APP */
