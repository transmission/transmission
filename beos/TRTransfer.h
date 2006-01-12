#ifndef TR_TRANSFER
#define TR_TRANSFER

#include <Entry.h>
#include <ListItem.h>
#include <Locker.h>
#include <String.h>
#include <View.h>

#include "transmission.h"

class TRTransfer : public BListItem {
public: // Construction and Controll methods.
	TRTransfer(const char *fullpath, node_ref node);
	~TRTransfer();

	inline node_ref GetCachedNodeRef() { return cachedNodeRef; };
	inline const char* GetCachedPath() { return cachedPath->String(); };
	
	bool SetStatus(tr_stat_t *stat, bool shade);
	
public: // BListItem
	virtual void Update(BView *owner, const BFont *font);
	virtual void DrawItem(BView *owner, BRect frame, bool complete = false);

private: 
	/* 
	 * Cached data. The items stored here are _NOT_ necessairly
	 * the torrent we'll be rendering. It's likely they will be,
	 * but NOT guaranteed. They are not used for anything relating
	 * to rendering.
	 * 
	 * Specifically we needed a way to cache the node_ref and
	 * reverse-lookup the node from the string path in the 
	 * transmission structs. This seemed the logical place to store
	 * that information, since it ends up in a BList(View).
	 */
	node_ref cachedNodeRef;
	BString *cachedPath;

private: // Private members used for rendering.
	float fBaselineOffset;
	float fLineSpacing;
	
	BLocker *fStatusLock;
	tr_stat_t *fStatus;
	
	rgb_color fBarColor;
	
	char* fTimeStr;
	char* fTransStr;
	
	bool fShade;
};

#endif /* TR_TRANSFER */
