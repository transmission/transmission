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

#ifndef TR_PREF_WIND
#define TR_PREF_WIND

#include <Button.h>
#include <TextControl.h>
#include <Slider.h>
#include <Window.h>

#define TR_PREF_SAVE 'tSve'
#define TR_PREF_CANCEL 'tCan'
#define TR_PREF_DEFAULTS 'tDef'

class TRPrefsWindow : public BWindow {
public:
	TRPrefsWindow();
	~TRPrefsWindow();
	
	virtual void MessageReceived(BMessage *msg);
	
	virtual void Show();
private:
	void ReadPrefs();
	bool WritePrefs();
	
	BTextControl *txtFolder;
	BTextControl *txtPort;
	BTextControl *txtUpload;
	
	BButton *btnSave;
	BButton *btnCancel;
	BButton *btnDefaults;
};

#endif /* TR_PREF_WIND */
