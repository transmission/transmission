#include "TRWindow.h"

#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <File.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <NodeMonitor.h>
#include <ScrollView.h>
#include <String.h>

#include <malloc.h>

#include "Prefs.h"
#include "TRApplication.h"
#include "TRTransfer.h"
#include "TRInfoWindow.h"

/**
 * The Transmission Window! Yay!
 */
TRWindow::TRWindow() : BWindow(BRect(10, 40, 350, 110), "Transmission", B_TITLED_WINDOW,
                               B_ASYNCHRONOUS_CONTROLS , B_CURRENT_WORKSPACE)
{
	engine = NULL;
	Prefs prefs(TRANSMISSION_SETTINGS);
	
	BRect *rectFrame = new BRect();
	if (prefs.FindRect("window.frame", rectFrame) == B_OK) {
		MoveTo(rectFrame->LeftTop());
		ResizeTo(rectFrame->Width(), rectFrame->Height());
	} else {
		rectFrame->Set(10, 40, 350, 110);
	}
	Lock();
	
	BRect viewRect(0, 0, rectFrame->Width(), rectFrame->Height());
	
	BMenuBar *menubar = new BMenuBar(viewRect, "MenuBar");
	BMenu *menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Open", new BMessage(TR_OPEN), 'O', B_COMMAND_KEY));
	menu->FindItem(TR_OPEN)->SetTarget(be_app_messenger); // send OPEN to the be_app.
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q', B_COMMAND_KEY));
	menubar->AddItem(menu);
	
	menu = new BMenu("Torrent");
	menu->AddItem(new BMenuItem("Get Info", new BMessage(TR_INFO), 'I', B_COMMAND_KEY));
	menu->FindItem(TR_INFO)->SetEnabled(false);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Resume", new BMessage(TR_RESUME)));
	menu->AddItem(new BMenuItem("Pause", new BMessage(TR_PAUSE)));
	menu->AddItem(new BMenuItem("Remove", new BMessage(TR_REMOVE)));
	menubar->AddItem(menu);
	
	menu = new BMenu("Tools");
	menu->AddItem(new BMenuItem("Settings", new BMessage(TR_SETTINGS)));
	menu->FindItem(TR_SETTINGS)->SetTarget(be_app_messenger);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About Transmission", new BMessage(B_ABOUT_REQUESTED)));
	menu->FindItem(B_ABOUT_REQUESTED)->SetTarget(be_app_messenger);
	menubar->AddItem(menu);
	
	AddChild(menubar);
	SetKeyMenuBar(menubar);
	
	// TODO: Tool Bar? (Well after everything is working based on Menus)
	
	// Setup the transfers ListView
	viewRect.Set(2, menubar->Frame().bottom + 3, rectFrame->Width() - 2 - B_V_SCROLL_BAR_WIDTH, rectFrame->Height() - 2);
	transfers = new BListView(viewRect, "TorrentList", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
	transfers->SetSelectionMessage(new BMessage(TR_SELECT));
	AddChild(new BScrollView("TransferScroller", transfers, B_FOLLOW_ALL, 0, false, true));
	
	Unlock();
	delete rectFrame;
	
	// Bring up the Transmission Engine
	engine = tr_init();
	LoadSettings();
	
	UpdateList(-1, true);
	
	// Start the message loop without showing the window.
	Hide();
	Show();
}

TRWindow::~TRWindow() {
	tr_close(engine);
	stop_watching(this);
}


void TRWindow::LoadSettings() {
	if (engine != NULL) {
		Prefs prefs(TRANSMISSION_SETTINGS);
		
		int32 bindPort;
		if (prefs.FindInt32("transmission.bindPort", &bindPort) != B_OK) {
			bindPort = 9000;
			prefs.SetInt32("transmission.bindPort", bindPort);
		}
		tr_setBindPort(engine, (int)bindPort);
		
		int32 uploadLimit;
		if (prefs.FindInt32("transmission.uploadLimit", &uploadLimit) != B_OK) {
			uploadLimit = 20;
			prefs.SetInt32("transmission.uploadLimit", uploadLimit);
		}
		tr_setUploadLimit(engine, (int)uploadLimit);
	}
}


/**
 * Rescans the active Torrents folder, and will add all the torrents there to the
 * engine.
 */
void TRWindow::RescanTorrents() {
	if (Lock()) {
		TRApplication *app = dynamic_cast<TRApplication*>(be_app);
		BEntry *torrentEntry = new BEntry();
		status_t err;
		
		if (app->TorrentDir()->InitCheck() == B_OK) {
			err = app->TorrentDir()->Rewind();
			while (err == B_OK) {
				err = app->TorrentDir()->GetNextEntry(torrentEntry, true);
				if (err != B_ENTRY_NOT_FOUND) {
					AddEntry(torrentEntry);
				}
			}
		}
		delete torrentEntry;
		Unlock();
	}
}


/**
 * Adds the file specified by *torrent to the Transmission engine.
 * Then adds a new TRTransfer item in the transfers list.
 * This item holds cached information about the torrent entry and node.
 * These TRTransmission items are _NOT_ guaranteed to render the entry 
 * they were created from.
 */
void TRWindow::AddEntry(BEntry *torrent) {
	node_ref node;
	if (torrent->GetNodeRef(&node) == B_OK) {
		if (watch_node(&node, B_WATCH_NAME, this) == B_OK) {
			BPath path;
			torrent->GetPath(&path);
			
			// Try adding the torrent to the engine.
			int addStatus = tr_torrentInit(engine, path.Path());
			if (addStatus == 0 && Lock()) { // Success. Add the TRTorrent item.
				transfers->AddItem(new TRTransfer(path.Path(), node));
				
				// Start the newly added torrent.
				worker_info *startData = (worker_info*)calloc(1, sizeof(worker_info));
				startData->window = this;
				startData->index = tr_torrentCount(engine) - 1;
				thread_id start_thread = spawn_thread(TRWindow::AsynchStartTorrent, "BirthCanal",
				                                      B_NORMAL_PRIORITY, (void *)startData);
				if (!((start_thread) < B_OK)) {
					resume_thread(start_thread);
				} else { // Fallback and start the old way.
					StartTorrent(startData->index);
					free(startData);
				}
				Unlock();
			} else {
				bool duplicate = false;
				TRTransfer* tr;
				for (int32 i = 0; i < transfers->CountItems(); i++) {
					tr = (TRTransfer*)transfers->ItemAt(i);
					if (tr->GetCachedNodeRef() == node) {
						duplicate = true;
					}
				}
				if (!duplicate) {
					BString errmsg("An error occurred trying to read ");
					char namebuf[B_FILE_NAME_LENGTH];
					torrent->GetName(namebuf);
					errmsg << namebuf;
					errmsg << ".";
					
					BAlert *error = new BAlert("Error Opening Torrent", 
					       errmsg.String(), 
					       "Ok", NULL, NULL, 
					       B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					error->Go();
					torrent->Remove();
				}
			}
		}
	}
}


void TRWindow::MessageReceived(BMessage *msg) {
	/*
	 * The only messages we receive from the node_monitor are if we need to
	 * stop watching the node. Basically, if it's been moved or removed we stop.
	 */
	if (msg->what == B_NODE_MONITOR) {
		node_ref node;
		ino_t fromDir;
		ino_t toDir;
		int32 opcode;
		
		if ((msg->FindInt32("opcode", &opcode) == B_OK) &&
			(msg->FindInt64("node", &node.node) == B_OK) &&
		    (msg->FindInt32("device", &node.device) == B_OK))
		{
			bool stop = (opcode == B_ENTRY_REMOVED);
			
			if (stop) {
				msg->FindInt64("directory", &toDir);
			} else { // It must have moved.
				stop = ((msg->FindInt64("from directory", &fromDir) == B_OK) &&
				        (msg->FindInt64("to directory", &toDir) == B_OK) &&
				        (toDir != fromDir));
			}
			
			if (stop) {
				watch_node(&node, B_STOP_WATCHING, this);
				
				/* Find the full path from the TRTorrents.
				 * The index of the TRTorrent that is caching the information
				 * IS NOT the index of the torrent in the engine. These are
				 * Totally decoupled, due to the way transmission is written.
				 */
				char path[B_FILE_NAME_LENGTH];
				TRTransfer* item;
				for (int32 i = 0; i < transfers->CountItems(); i++) {
					item = (TRTransfer*)transfers->ItemAt(i);
					if (item->GetCachedNodeRef() == node) {
						strcpy(path, item->GetCachedPath());
					}
				}
				
				// Look for the torrent info in the engine with the matching
				// path name.
				tr_stat_t *stats;
				tr_torrentStat(engine, &stats);
				int index;
				for (index = 0; index < tr_torrentCount(engine); index++) {
					if (strcmp(stats[index].info.torrent, path) == 0) {
						tr_torrentClose(engine, index);
						transfers->RemoveItem(index);
						break;
					}
				}
				free(stats);
			}
		}
	} else if (msg->what == TR_INFO) {
		// Display an Info Window.
		tr_stat_t *s;
		tr_torrentStat(engine, &s);
		
		TRInfoWindow *info = new TRInfoWindow(s[transfers->CurrentSelection()]);
		info->MoveTo(Frame().LeftTop() + BPoint(20, 25));
		info->Show();
	} else if (msg->what == TR_SELECT) {
		// Setup the Torrent Menu enabled / disabled state.
		int32 selection;
		msg->FindInt32("index", &selection);
		UpdateList(selection, true);
	} else if (msg->what == TR_RESUME) {
		worker_info *startData = (worker_info*)calloc(1, sizeof(worker_info));
		startData->window = this;
		startData->index = (int)transfers->CurrentSelection();
		thread_id start_thread = spawn_thread(TRWindow::AsynchStartTorrent, "BirthCanal",
		                                      B_NORMAL_PRIORITY, (void *)startData);
		if (!((start_thread) < B_OK)) {
			resume_thread(start_thread);
		} else { // Fallback and start the old way.
			StartTorrent(startData->index);
			free(startData);
		}
	} else if (msg->what == TR_PAUSE) {
		worker_info *stopData = (worker_info*)calloc(1, sizeof(worker_info));
		stopData->window = this;
		stopData->index = (int)transfers->CurrentSelection();
		thread_id stop_thread = spawn_thread(TRWindow::AsynchStopTorrent, "InUtero",
		                                     B_NORMAL_PRIORITY, (void *)stopData);
		if (!((stop_thread) < B_OK)) {
			resume_thread(stop_thread);
		} else { // Fallback and stop it the old way.
			StopTorrent(stopData->index);
			free(stopData);
		}
	} else if (msg->what == TR_REMOVE) {
		int32 index = transfers->CurrentSelection();
		
		tr_torrentClose(engine, (int)index);
		
		// Remove the file from the filesystem.
		TRTransfer *item = (TRTransfer*)transfers->RemoveItem(index);
		BEntry *entry = new BEntry(item->GetCachedPath(), true);
		entry->Remove();
		delete entry;
		delete item;
		
		UpdateList(transfers->CurrentSelection(), true);
	} else if (msg->what == B_SIMPLE_DATA) {
		be_app->RefsReceived(msg);
	}
		
	BWindow::MessageReceived(msg);
}


bool TRWindow::QuitRequested() {
	bool quit = false;
	
	bool running = false;
	tr_stat_t *s;
	int max = tr_torrentStat(engine, &s);
	for (int i = 0; i < max && !running; i++) {
		running = (s[i].status &
		          (TR_STATUS_CHECK | TR_STATUS_DOWNLOAD | TR_STATUS_SEED));
	}
	free(s);
	
	if (running) {
		BString quitMsg("");
		quitMsg << "There's " << max << " torrent";
		if (max > 1) {
			quitMsg << "s";
		}
		quitMsg << " currently running.\n"
		        << "What would you like to do?";
		
		BAlert *confirmQuit = new BAlert("Confirm Quit", quitMsg.String(),
		                                 "Cancel", "Quit", NULL,
		                                 B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		quit = (confirmQuit->Go() == 1);
	} else {
		quit = true;
	}
	
	if (quit) {
		Prefs *prefs = new Prefs(TRANSMISSION_SETTINGS);
		prefs->SetRect("window.frame", Frame());
		delete prefs;
		
		for (int i = 0; i < tr_torrentCount(engine); i++) {
			tr_torrentStop(engine, i);
		}
		
		be_app->PostMessage(new BMessage(B_QUIT_REQUESTED));
	}
	return quit;
}

void TRWindow::FrameResized(float width, float height) {
	transfers->Invalidate();
}


/**
 * Called from the StopTorrent thread.
 */
void TRWindow::StopTorrent(int index) {
	tr_torrentStop(engine, index);
	
	UpdateList(index, true);
}

/**
 * Called from StartTorrent thread.
 */
void TRWindow::StartTorrent(int index) {
	// Read the settings.
	BString folder("");
	Prefs *prefs = new Prefs(TRANSMISSION_SETTINGS);
	if (prefs->FindString("download.folder", &folder) != B_OK) {
		prefs->SetString("download.folder", "/boot/home/Downloads");
		folder << "/boot/home/Downloads";
	}
	tr_torrentSetFolder(engine, index, folder.String());
	tr_torrentStart(engine, index);
	
	if (transfers->CurrentSelection() >= 0) {
		UpdateList(index, true);
	}
	
	delete prefs;
}

/**
 * Called from the be_app Pulse();
 * This will update the data structures that the TRTorrents use to render,
 * and invalidate the view.
 */
void TRWindow::UpdateList(int32 selection = -1, bool menus = true) {
	bool running = false;
	
	tr_stat_t * s;
	int i = 0;
	int max = tr_torrentStat(engine, &s);
	bool invalid[max];
	
	for (i = 0; i < max; i++) {
		invalid[i] = ((TRTransfer*)transfers->ItemAt(i))->SetStatus(&(s[i]), (i % 2 != 0));
		
		if (menus && i == (int)selection) {
			running = (s[selection].status & 
			          (TR_STATUS_CHECK | TR_STATUS_DOWNLOAD | TR_STATUS_SEED));
		}
	}
	free(s);
	
	if (menus) {
		KeyMenuBar()->FindItem(TR_INFO)->SetEnabled(selection >= 0);
		KeyMenuBar()->FindItem(TR_RESUME)->SetEnabled(selection >= 0 && !running);
		KeyMenuBar()->FindItem(TR_PAUSE)->SetEnabled(selection >= 0 && running);
		KeyMenuBar()->FindItem(TR_REMOVE)->SetEnabled(selection >= 0 && !running);
	}
	
	if (Lock()) {
		for (i = 0; i < max; i++) {
			if (invalid[i]) {
				transfers->InvalidateItem(i);
			}
		}
		Unlock();
	}
}

/**
 * Thread Function to stop Torrents. This can be expensive and causes the event loop to
 * choke.
 */
int32 TRWindow::AsynchStopTorrent(void *data) {
	worker_info* stopData = (worker_info*)data;
	stopData->window->StopTorrent(stopData->index);
	free(stopData);
	return B_OK;
}

/**
 * Thread Function to start Torrents. This can be expensive and causes the event loop to
 * choke.
 */
int32 TRWindow::AsynchStartTorrent(void *data) {
	worker_info* startData = (worker_info*)data;
	startData->window->StartTorrent(startData->index);
	free(startData);
	return B_OK;
}