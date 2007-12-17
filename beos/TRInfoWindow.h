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
