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