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

#ifndef TR_WIND
#define TR_WIND

#include <Entry.h>
#include <FilePanel.h>
#include <ListView.h>
#include <ListItem.h>
#include <Window.h>

#include <libtransmission/transmission.h>

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
	BString GetFolder(void);
	void StopTorrent(tr_torrent_t *torrent);
	void StartTorrent(tr_torrent_t *torrent);
	
	static int32 AsynchStopTorrent(void *data);
	static int32 AsynchStartTorrent(void *data);
	
	void RescanTorrents();

private:
	static BListView *transfers;
	BFilePanel *openPanel;
	
	tr_handle_t *engine;
	
	TRPrefsWindow *fSettings;
	
	static bool RemovePath(BListItem *item, void *data);
	static bool CheckQuitStatus(BListItem *item, void *data);
	static bool UpdateStats(BListItem *item, void *data);
	
	bool stopping;
	BInvoker *quitter;
};

/**
 * Used to pass info off to the worker thread that runs AsynchStopTorrent
 */
struct worker_info {
	TRWindow     *window;
	tr_torrent_t *torrent;
};

struct remove_info {
	TRWindow *window;
	char     path[B_FILE_NAME_LENGTH];
};

struct quit_info {
	TRWindow *window;
	int       running;
};

struct update_info {
	TRWindow *window;
	bool     running;
	int      selected;
	int      invalid;
};

#endif /* TR_WIND */
