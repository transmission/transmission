// $Id$

#include "TRTransfer.h"

#include <Font.h>

#include <malloc.h>
#include <stdio.h>

/**
 * BListItem that renders Transfer status.
 */
TRTransfer::TRTransfer(const char *fullpath, node_ref node, tr_torrent_t *torrentRef) : BListItem(0, false), cachedNodeRef(node) {
	fBaselineOffset = 0.0f;
	fLineSpacing = 0.0f;
	torrent = torrentRef;
	
	cachedPath = new BString(fullpath);
	fStatusLock = new BLocker("Status Locker", true);
	
	fStatus = (tr_stat_t*)calloc(1, sizeof(tr_stat_t));
	tr_info_t *info = tr_torrentInfo(torrent);
	fName = new BString("<unknown name>");
	fName->SetTo(info->name);
	
	fBarColor.red = 50;
	fBarColor.green = 150;
	fBarColor.blue = 255;
	fBarColor.alpha = 255;
	
	fTimeStr = (char*)calloc(78, sizeof(char));
	fTransStr = (char*)calloc(78, sizeof(char));
}


TRTransfer::~TRTransfer() {
	if (fStatusLock->Lock()) {
		free(fStatus);
		fStatusLock->Unlock();
		delete fStatusLock;
	}
	delete cachedPath;
	delete fName;
}


void TRTransfer::Update(BView *owner, const BFont *font) {
	BListItem::Update(owner, font);
	SetHeight(BListItem::Height() * 4);
	
	font_height height;
	font->GetHeight(&height);
	
	fBaselineOffset = height.leading + height.ascent;
	fLineSpacing = height.descent;
}


/**
 * Sets the transfer information to render.
 * Returns a bool signaling the view is dirty after the update.
 * 
 * The view is determined to be dirty if the transfer
 * status, progress, eta or the "shade" (even or odd) 
 * position in the list changes from the previous state.
 * If the tr_stat_t is in fact different then the new, full
 * status is memcpy'd overtop the existing code.
 * 
 * This is a thread-safe function, as all writing to the 
 * local fStatus requires a successful Lock on fStatusLock.
 */
bool TRTransfer::UpdateStatus(tr_stat_t *stat, bool shade) {
	bool dirty = false;
	if (fStatusLock->Lock()) {
		if (fStatus->status != stat->status ||
		    fStatus->progress != stat->progress ||
		    fStatus->eta != stat->eta ||
		    fShade != shade)
		{
			memcpy(fStatus, stat, sizeof(tr_stat_t));
			dirty = true;
		}
		fStatusLock->Unlock();
		fShade = shade;
	}
	return dirty;
}

bool TRTransfer::IsRunning() {
	return (fStatus != NULL && 
			(fStatus->status &
			          (TR_STATUS_CHECK | TR_STATUS_DOWNLOAD | TR_STATUS_SEED)));
}


/**
 * Renders (Draws) the current status into the BListView.
 */
void TRTransfer::DrawItem(BView *owner, BRect frame, bool) {
	rgb_color col;
	col.red = 255;
	col.green = 255;
	col.blue = 255;
	
	owner->PushState();
	
	// Draw the background...
	if (IsSelected()) {
		owner->SetLowColor(tint_color(col, B_DARKEN_2_TINT));
	} else if (fShade) {
		owner->SetLowColor(tint_color(tint_color(col, B_DARKEN_1_TINT), B_LIGHTEN_2_TINT));
	} else {
		owner->SetLowColor(col);
	}
	owner->FillRect(frame, B_SOLID_LOW);
	
	// Draw the informational text...
	owner->SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
	BPoint textLoc = frame.LeftTop();
	textLoc.y += (fBaselineOffset / 2);
	textLoc += BPoint(2, fBaselineOffset);
	
	if (fStatus != NULL && fStatusLock->Lock()) {
		owner->DrawString(fName->String(), textLoc);
		
		if (fStatus->status & TR_STATUS_PAUSE ) {
			sprintf(fTimeStr, "Paused (%.2f %%)", 100 * fStatus->progress);
		} else if (fStatus->status & TR_STATUS_CHECK ) {
			sprintf(fTimeStr, "Checking Existing Files (%.2f %%)",
			        100 * fStatus->progress);
		} else if (fStatus->status & TR_STATUS_DOWNLOAD) {
			if (fStatus->eta < 0 ) {
				sprintf(fTimeStr, "--:--:-- Remaining (%.2f %%Complete)",
				        100 * fStatus->progress);
			} else {
				sprintf(fTimeStr, "%02d:%02d:%02d Remaining (%.2f %%Complete)",
				        fStatus->eta / 3600, (fStatus->eta / 60) % 60,
				        fStatus->eta % 60, 100 * fStatus->progress);
			}
		} else if (fStatus->status & TR_STATUS_SEED) {
			sprintf(fTimeStr, "Seeding");
		} else if (fStatus->status & TR_STATUS_STOPPING) {
			sprintf(fTimeStr, "Stopping...");
		} else {
			fTimeStr[0] = '\0';
		}
		
		textLoc.x = frame.Width() - owner->StringWidth(fTimeStr) - 2;
		owner->DrawString(fTimeStr, textLoc);
		
		if (fStatus->status & (TR_STATUS_DOWNLOAD | TR_STATUS_SEED | TR_STATUS_CHECK )) {
			// Move to the left of the bottom line.
			textLoc.Set(frame.left + 2, 
			            frame.top + fBaselineOffset * 3 + (2 * fLineSpacing) + (fBaselineOffset / 2));
			sprintf(fTransStr, "DL: %.2f KB/s (from %i of %i peer%s)",
			        fStatus->rateDownload, fStatus->peersUploading, fStatus->peersTotal,
			        (fStatus->peersTotal == 1) ? "" : "s");
			owner->DrawString(fTransStr, textLoc);
			
			sprintf(fTransStr, "UL: %.2f KB/s", fStatus->rateUpload);
			textLoc.x = frame.Width() - owner->StringWidth(fTransStr) - 2;
			owner->DrawString(fTransStr, textLoc);
		}
		
		/*
		 * Progress Bar - Mercilessly ripped from the Haiku source code, and
		 * modified to handle selection tinting, and list item position shading.
		 */
		// Custom setup (Transmission Added)
		BRect rect(frame.left + 2, frame.top + fBaselineOffset + fLineSpacing + (fBaselineOffset / 2),
		           frame.Width() - 2, frame.top + fBaselineOffset * 2 + fLineSpacing + (fBaselineOffset / 2));
		
		// First bevel
		owner->SetHighColor(tint_color(ui_color ( B_PANEL_BACKGROUND_COLOR ), B_DARKEN_1_TINT));
		if (IsSelected()) {
			owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
		}
		owner->StrokeLine(BPoint(rect.left, rect.bottom), BPoint(rect.left, rect.top));
		owner->StrokeLine(BPoint(rect.right, rect.top));
	
		owner->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_LIGHTEN_2_TINT));
		if (IsSelected()) {
			owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
		}
		owner->StrokeLine(BPoint(rect.left + 1.0f, rect.bottom), BPoint(rect.right, rect.bottom));
		owner->StrokeLine(BPoint(rect.right, rect.top + 1.0f));
	
		rect.InsetBy(1.0f, 1.0f);
		
		// Second bevel
		owner->SetHighColor(tint_color(ui_color ( B_PANEL_BACKGROUND_COLOR ), B_DARKEN_4_TINT));
		if (IsSelected()) {
			owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
		}
		owner->StrokeLine(BPoint(rect.left, rect.bottom), BPoint(rect.left, rect.top));
		owner->StrokeLine(BPoint(rect.right, rect.top));
	
		owner->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		if (IsSelected()) {
			owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
		}
		owner->StrokeLine(BPoint(rect.left + 1.0f, rect.bottom), BPoint(rect.right, rect.bottom));
		owner->StrokeLine(BPoint(rect.right, rect.top + 1.0f));
	
		rect.InsetBy(1.0f, 1.0f);
	
		// Filling
		owner->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_LIGHTEN_MAX_TINT));
		if (IsSelected()) {
			owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
		}
		owner->FillRect(rect);
	
		if (fStatus->progress != 0.0f) {
			rect.right = rect.left + (float)ceil(fStatus->progress * (rect.Width() - 4)),
	
			// Bevel
			owner->SetHighColor(tint_color(fBarColor, B_LIGHTEN_2_TINT));
			if (IsSelected()) {
				owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
			}
			owner->StrokeLine(BPoint(rect.left, rect.bottom), BPoint(rect.left, rect.top));
			owner->StrokeLine(BPoint(rect.right, rect.top));
	
			owner->SetHighColor(tint_color(fBarColor, B_DARKEN_2_TINT));
			if (IsSelected()) {
				owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
			}
			owner->StrokeLine(BPoint(rect.left, rect.bottom), BPoint(rect.right, rect.bottom));
			owner->StrokeLine(BPoint(rect.right, rect.top));
	
			rect.InsetBy(1.0f, 1.0f);
	
			// Filling
			owner->SetHighColor(fBarColor);
			if (IsSelected()) {
				owner->SetHighColor(tint_color(owner->HighColor(), B_DARKEN_1_TINT));
			}
			owner->FillRect(rect);
		}
		
		fStatusLock->Unlock();
	} else {
		owner->DrawString("loading...", textLoc);
	}
	
	owner->PopState();
}
