// $Id$

#ifndef TR_INFO_WIND
#define TR_INFO_WIND

#include <Box.h>
#include <Window.h>
#include <StringView.h>

#include <libtransmission/transmission.h>

class TRInfoWindow : public BWindow {
public:
	TRInfoWindow(const tr_stat_t *status, const tr_info_t *info, const char *folder);
	~TRInfoWindow();
	
	virtual void FrameResized(float width, float height);
private:
	void StringForFileSize(uint64_t size, BString *str);
	
	BBox *fBox;
};

#endif
