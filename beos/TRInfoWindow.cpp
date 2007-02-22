// $Id$

#include "TRInfoWindow.h"

#include <Box.h>
#include <String.h>
#include <TextView.h>
#include <ScrollView.h>

#include <malloc.h>
#include <stdio.h>


TRInfoWindow::TRInfoWindow(tr_stat_t *status, tr_info_t *info, char *folder) : BWindow(BRect(0, 0, 250, 175), "Info", 
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
	
	BString strTracker(status->trackerAddress);
	strTracker << ":" << status->trackerPort;

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
	        << "Announce: " << status->trackerAnnounce << "\n"
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

void TRInfoWindow::FrameResized(float width, float height) {
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
