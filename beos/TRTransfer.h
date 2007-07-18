// $Id$

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
	
	bool UpdateStatus(tr_stat_t *stat, bool shade);
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
