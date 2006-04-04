#include "TRApplication.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <AppFileInfo.h>
#include <Alert.h>
#include <Bitmap.h>
#include <File.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <Mime.h>
#include <Path.h>
#include <String.h>
#include <Resources.h>
#include <Roster.h>

int main(int argc, char** argv) {
	TRApplication *app = new TRApplication();
	if (app->InitCheck() == B_OK) {
		app->Run();
	} else {
		BString errMsg("");
		errMsg << "The following error occurred loading Transmission:\n"
		       << strerror(app->InitCheck());
		BAlert *ohNo = new BAlert("Transmission Error", 
			errMsg.String(),
			"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		ohNo->Go();
	}
	delete app;
	
	return (0);
}


TRApplication::TRApplication() : BApplication(APP_SIG) {
	status_t err = B_OK;
	torrentDir = NULL;
	
	// Install Mime types if necessary.
	BMimeType torrentType("application/x-bittorrent");
	if (torrentType.InitCheck() == B_OK) {
		if (!torrentType.IsInstalled()) {
			fprintf(stderr, "Installing mime type...\n");
			// Icon
			app_info runningInfo;
			GetAppInfo(&runningInfo);
			BFile appFile(&(runningInfo.ref), B_READ_ONLY);
			BResources res(&appFile, false);
			
			size_t len = 0;
			BBitmap *icon = NULL;
			void *iconBits = NULL;
			
			iconBits = res.FindResource('ICON', "BEOS:L:application/x-bittorrent", &len);
			if (iconBits) {
				icon = new BBitmap(BRect(0, 0, 31, 31), B_CMAP8);
				icon->SetBits(iconBits, len, 0, B_CMAP8);
				torrentType.SetIcon(icon, B_LARGE_ICON);
				delete icon;
				icon = NULL;
				len = 0;
			}
			
			iconBits = res.FindResource('ICON', "BEOS:M:application/x-bittorrent", &len);
			if (iconBits) {
				icon = new BBitmap(BRect(0, 0, 15, 15), B_CMAP8);
				icon->SetBits(iconBits, len, 0, B_CMAP8);
				torrentType.SetIcon(icon, B_MINI_ICON);
				delete icon;
			}
			
			// Extensions
			BMessage extensions;
			extensions.AddString("extensions", "torrent");
			torrentType.SetFileExtensions(&extensions);
			
			torrentType.SetShortDescription("BitTorrent Meta File");
			torrentType.SetLongDescription("BitTorrent Protocol Meta File (http://www.bittorrent.com)");
			
			// Set Preferred Application
			torrentType.SetPreferredApp(APP_SIG, B_OPEN);
			
			torrentType.Install();
		}
	}
	
	// Create Settings folder and structure
	BPath settingsPath;
	err = find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath, false);
	if (err == B_OK) {
		BDirectory settings(settingsPath.Path());
		BDirectory *trSettings = new BDirectory();
		
		err = settings.CreateDirectory("Transmission", trSettings);
		if (err == B_FILE_EXISTS) {
			err = trSettings->SetTo(&settings, "Transmission");
		}
		
		if (err == B_OK) {
			torrentDir = new BDirectory();
			err = trSettings->CreateDirectory("Torrents", torrentDir);
			if (err != B_OK) {
				torrentDir->SetTo(trSettings, "Torrents");
			}
		}
		delete trSettings;
	}
	
	// Create window
	window = new TRWindow();
	window->RescanTorrents();
	settings = new TRPrefsWindow();
	
	openPanel = new BFilePanel();
	openPanel->SetRefFilter(new TRFilter());
}


TRApplication::~TRApplication() {
	if (window->Lock()) {
		window->Quit();
	}
	if (settings->Lock()) {
		settings->Quit();
	}
	
	if (openPanel != NULL) {
		delete openPanel;
	}
	
	if (torrentDir != NULL) {
		delete torrentDir;
	}
}


/**
 * Checks to make sure our Settings directory structure is in place.
 * If this fails, then we need to display an alert and vomit.
 */
status_t TRApplication::InitCheck() {
	if (torrentDir != NULL) {
		return torrentDir->InitCheck();
	}
	return B_ERROR;
}


void TRApplication::AboutRequested() {
	// Read the application version info
	app_info runningInfo;
	GetAppInfo(&runningInfo);
	BFile appFile(&(runningInfo.ref), B_READ_ONLY);
	BAppFileInfo appInfo(&appFile);
	version_info vInfo;
	appInfo.GetVersionInfo(&vInfo, B_APP_VERSION_KIND);
	
	BString aboutMsg("");
	aboutMsg << "Transmission\n"
	         << "Version " << vInfo.major << "." << vInfo.middle << "." << vInfo.minor << "\n"
	         << "Eric Petit & Bryan Varner\n";
	BAlert *aboutBox = new BAlert("About Transmission", aboutMsg.String(), "Close");
	aboutBox->Go();
}


void TRApplication::ReadyToRun() {
	SetPulseRate(500000);
	window->Show();
}


void TRApplication::Pulse() {
	window->UpdateList(-1, false);
}


/**
 * When a ref is received, we copy each file that is to be opened into
 * B_USER_SETTINGS/Transmission/Torrents
 * 
 * Our window node_monitors this folder for added / removed torrent meta files.
 *
 * Each copy is performed in a seperate thread to avoid blocking / locking the app.
 */
void TRApplication::RefsReceived(BMessage *message) {
	int32 count;
	type_code code;
	message->GetInfo("refs", &code, &count);
	for (int i = 0; i < count; i++) {
		entry_ref *ref = (entry_ref*)calloc(sizeof(entry_ref), 1);
		if (message->FindRef("refs", i, ref) == B_OK) {
			thread_id cp_thread = spawn_thread(TRApplication::Copy, "TorrentDuper",
			                                   B_NORMAL_PRIORITY, (void *)ref);
			if (!((cp_thread) < B_OK)) {
				resume_thread(cp_thread);
			}
		}
	}
}

/**
 * Needed for browsers or command line interaction
 */
void TRApplication::ArgvReceived(int32 _argc, char** _argv) 
{
	entry_ref ref;
	BMessage refs(B_REFS_RECEIVED);
	for( int32 i = 0; i < _argc; ++i )
	{
		if( B_OK == get_ref_for_path(_argv[i], &ref) )
		{
			refs.AddRef("refs", &ref);
		}
	}
	
	be_app_messenger.SendMessage(refs);
}

/**
 * BMessage handling. 
 */
void TRApplication::MessageReceived(BMessage *message) {
	BApplication::MessageReceived(message);
	/* 
	 * When the copy of a torrent file is complete, we get a message
	 * signaling that we should add the now copied .torrent file to our Transfer Window.
	 */
 	if (message->what == TR_ADD) {
		// Add the torrent to the window.
		entry_ref torrentRef;
		if (message->FindRef("torrent", &torrentRef) == B_OK) {
			BEntry *entry = new BEntry(&torrentRef, true);
			window->AddEntry(entry);
			delete entry;
		}
	/* Show the Open Dialog */
	} else if (message->what == TR_OPEN) {
		openPanel->Show();
	} else if (message->what == TR_SETTINGS) {
		settings->MoveTo(window->Frame().left + (window->Frame().Width() - settings->Frame().Width()) / 2, 
		                 window->Frame().top + (window->Frame().Height() - settings->Frame().Height()) / 2);
			
		settings->Show();
	} else if (message->what == TR_RELOAD_SETTINGS) {
		window->LoadSettings();
	}
}

bool TRApplication::QuitRequested() {
	return BApplication::QuitRequested();
}

/**
 * Thread Function.
 * The thread copies the original .torrent file into the torrents folder,
 * then signals the window to load the torrent meta info from the copy.
 * The torrent window will then node_monitor the file so that if it's removed
 * the torrent will cease.
 *
 * Keeping the file copy in a separate thread keeps us from blocking up the
 * rest of our program.
 * 
 * This behavior lets us kill the original .torrent download from disc (which
 * speaking as a user I have a tendency to do) but keep the meta around for
 * Transmission to use later.
 *
 * If the user whacks the .torrent file from our settings folder, then we'll
 * remove it from the list (node_monitor!) and if they remove it from our list
 * we'll remove it from the file system.
 */
int32 TRApplication::Copy(void *data) {
	entry_ref *ref = (entry_ref*)data;
	
	BFile source(ref, B_READ_ONLY);
	BFile target(((TRApplication*)be_app)->TorrentDir(), ref->name, 
	             B_WRITE_ONLY | B_CREATE_FILE | B_FAIL_IF_EXISTS);
	
	BEntry targetEntry(((TRApplication*)be_app)->TorrentDir(), ref->name);
	entry_ref targetRef;
	targetEntry.GetRef(&targetRef);
	
	// Only perform the copy if the target is freshly created. Everything is B_OK!
	if (source.InitCheck() == B_OK && target.InitCheck() == B_OK) {
		if (target.Lock() == B_OK) {
			char *buffer = (char*)calloc(1, 4096); // 4k data buffer.
			ssize_t read = 0;
			while ((read = source.Read(buffer, 4096)) > 0) {
				target.Write(buffer, read);
			}
			free(buffer);
			target.Unlock();
		}
	}
	
	BMessage msg(TR_ADD);
	msg.AddRef("torrent", &targetRef);
	BMessenger messenger(be_app);
	messenger.SendMessage(&msg);
	
	free(ref);
	return B_OK;
}

/**
 * Filters the FilePanel for torrent files and directories.
 */
bool TRFilter::Filter(const entry_ref *ref, BNode *node, struct stat *st, const char *mimetype) {
	return (node->IsDirectory() || 
           (strcmp(mimetype, "application/x-bittorrent") == 0) ||
           (strstr(ref->name, ".torrent") != NULL));

}
