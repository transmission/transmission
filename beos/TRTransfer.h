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

#ifndef TR_TRANSFER
#define TR_TRANSFER

#include <Entry.h>
#include <ListItem.h>
#include <Locker.h>
#include <String.h>
#include <View.h>

#include <libtransmission/transmission.h>

class TRTransfer : public BListItem {
public: // Construction and Controll methods.
	TRTransfer(const char *fullpath, node_ref node, tr_torrent_t *torrentRef);
	~TRTransfer();

	inline node_ref GetCachedNodeRef() { return cachedNodeRef; };
	inline const char* GetCachedPath() { return cachedPath->String(); };
	inline tr_torrent_t* GetTorrent()  { return torrent; };
	
	bool UpdateStatus(const tr_stat_t *stat, bool shade);
	bool IsRunning();
	
public: // BListItem
	virtual void Update(BView *owner, const BFont *font);
	virtual void DrawItem(BView *owner, BRect frame, bool complete = false);

private: 
	node_ref cachedNodeRef;
	BString *cachedPath;
	tr_torrent_t *torrent;

private: // Private members used for rendering.
	float fBaselineOffset;
	float fLineSpacing;
	
	BLocker *fStatusLock;
	tr_stat_t *fStatus;
	BString *fName;
	
	rgb_color fBarColor;
	
	char* fTimeStr;
	char* fTransStr;
	
	bool fShade;
};

#endif /* TR_TRANSFER */
