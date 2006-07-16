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
