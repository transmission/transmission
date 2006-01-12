#ifndef TR_WIND
#define TR_WIND

#include <Entry.h>
#include <FilePanel.h>
#include <ListView.h>
#include <Window.h>

#include "transmission.h"

#include "TRPrefsWindow.h"

#define TR_INFO 'tNfo'

#define TR_RESUME 'tRes'
#define TR_PAUSE 'tPse'
#define TR_REMOVE 'tRmv'
#define TR_SELECT 'tSel'
#define TR_SETTINGS 'tSet'



/**
 * Transmission Window.
 */
class TRWindow : public BWindow {
public: // BWindow
	TRWindow();
	~TRWindow();
	
	virtual void MessageReceived(BMessage *msg);
	virtual bool QuitRequested();
	virtual void FrameResized(float width, float height);

public:	// TRWindow
	void AddEntry(BEntry *torrent);
	
	void UpdateList(int32 selection, bool menus);
	
	void LoadSettings();
	
	void StopTorrent(int index);
	void StartTorrent(int index);
	
	static int32 AsynchStopTorrent(void *data);
	static int32 AsynchStartTorrent(void *data);
	
	void RescanTorrents();

private:
	BListView *transfers;
	BFilePanel *openPanel;
	
	tr_handle_t *engine;
	
	TRPrefsWindow *fSettings;
};

/**
 * Used to pass info off to the worker thread that runs AsynchStopTorrent
 */
struct worker_info {
	TRWindow *window;
	int      index;
};

#endif /* TR_WIND */
