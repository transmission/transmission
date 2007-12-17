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

#include "TRInfoWindow.h"

#include <Box.h>
#include <String.h>
#include <TextView.h>
#include <ScrollView.h>

#include <malloc.h>
#include <stdio.h>


TRInfoWindow::TRInfoWindow(const tr_stat_t *status, const tr_info_t *info, const char *folder) : BWindow(BRect(0, 0, 250, 175), "Info", 
                          B_FLOATING_WINDOW, B_ASYNCHRONOUS_CONTROLS | /*B_NOT_RESIZABLE*/  B_NOT_ZOOMABLE,
                          B_CURRENT_WORKSPACE)
{
	BRect viewRect = Bounds();
	
	// Single header, Font Size 14.
	BFont headerFont(be_bold_font);
	headerFont.SetSize(14.0f);
	font_height fh;
	headerFont.GetHeight(&fh);
	if (headerFont.StringWidth(info->name) > Bounds().Width() - 10) {
		ResizeBy(headerFont.StringWidth(info->name) - Bounds().Width() + 10, 0);
	}
	
	viewRect = Bounds();
	viewRect.bottom = fh.ascent + fh.descent;
	BStringView *strView = new BStringView(viewRect, "header", info->name, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	strView->SetFont(&headerFont);
	strView->SetAlignment(B_ALIGN_CENTER);
	
	viewRect.left = 5;
	viewRect.top = 10;
	viewRect.bottom = Bounds().bottom - 5;
	BTextView *txtView = new BTextView(viewRect, "infoText", viewRect, B_FOLLOW_LEFT | B_FOLLOW_TOP);
	txtView->MakeEditable(false);
	
	BString strTracker(status->tracker->address);
	strTracker << ":" << status->tracker->port;

	BString strPieceSize("");
	StringForFileSize(info->pieceSize, &strPieceSize);

	BString strTotalSize("");
	StringForFileSize(info->totalSize, &strTotalSize);

	BString strDownloaded("");
	StringForFileSize(status->downloaded, &strDownloaded);

	BString strUploaded("");
	StringForFileSize(status->uploaded, &strUploaded);

	BString infoStr("");
	infoStr << "Tracker: " << strTracker << "\n"
	        << "Announce: " << status->tracker->announce << "\n"
	        << "Piece Size: " << strPieceSize << "\n"
	        << "Pieces: " << info->pieceCount << "\n"
	        << "Total Size: " << strTotalSize << "\n"
	        << "\n"
	        << "Folder: " << folder << "\n"
	        << "Downloaded: " << strDownloaded << "\n"
	        << "Uploaded: " << strUploaded << "\n";
	txtView->SetText(infoStr.String());
	
	Lock();
	AddChild(strView);
	AddChild(txtView);
	Unlock();
}

TRInfoWindow::~TRInfoWindow() {
	
}

void TRInfoWindow::FrameResized(float, float) {
}

void TRInfoWindow::StringForFileSize(uint64_t size, BString *str) {
	char *s = (char*)calloc(512, sizeof(char));
	if (size < 1024) {
		sprintf(s, "%lld bytes", size);
	} else if (size < 1048576) {
		sprintf(s, "%lld.%lld KB", size / 1024, (size % 1024 ) / 103);
	} else if (size < 1073741824 ) {
		sprintf(s, "%lld.%lld MB", size / 1048576, (size % 1048576) / 104858);
	} else {
		sprintf(s, "%lld.%lld GB", size / 1073741824, (size % 1073741824) / 107374183);
	}
	
	str->SetTo(s);
	free(s);
}
