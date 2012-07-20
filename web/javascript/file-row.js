/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function FileRow(torrent, i)
{
	var fields = {
		have: 0,
		index: 0,
		isDirty: false,
		isWanted: true,
		priority: 0,
		me: this,
		size: 0,
		torrent: null
	},

	elements = {
		priority_low_button: null,
		priority_normal_button: null,
		priority_high_button: null,
		progress: null,
		root: null
	},

	initialize = function(torrent, i) {
		fields.torrent = torrent;
		fields.index = i;
		createRow(torrent, i);
	},

	readAttributes = function(file) {
		if (fields.have !== file.bytesCompleted) {
			fields.have = file.bytesCompleted;
			fields.isDirty = true;
		}
		if (fields.size !== file.length) {
			fields.size = file.length;
			fields.isDirty = true;
		}
		if (fields.priority !== file.priority) {
			fields.priority = file.priority;
			fields.isDirty = true;
		}
		if (fields.isWanted !== file.wanted) {
			fields.isWanted = file.wanted;
			fields.isDirty = true;
		}
	},

	refreshWantedHTML = function()
	{
		var e = $(elements.root);
		e.toggleClass('skip', !fields.isWanted);
		e.toggleClass('complete', isDone());
		$(e[0].checkbox).prop('checked', fields.isWanted);
	},
	refreshPriorityHTML = function()
	{
		$(elements.priority_high_button  ).toggleClass('selected', fields.priority ===  1 );
		$(elements.priority_normal_button).toggleClass('selected', fields.priority ===  0 );
		$(elements.priority_low_button   ).toggleClass('selected', fields.priority === -1 );
	},
	refreshProgressHTML = function()
	{
		var pct = 100 * (fields.size ? (fields.have / fields.size) : 1.0),
		    c = [ Transmission.fmt.size(fields.have),
			  ' of ',
			  Transmission.fmt.size(fields.size),
			  ' (',
			  Transmission.fmt.percentString(pct),
			  '%)' ].join('');
		setTextContent(elements.progress, c);
	},
	refreshHTML = function() {
		if (fields.isDirty) {
			fields.isDirty = false;
			refreshProgressHTML();
			refreshWantedHTML();
			refreshPriorityHTML();
		}
	},
	refresh = function() {
		readAttributes(fields.torrent.getFile(fields.index));
		refreshHTML();
	},

	isDone = function () {
		return fields.have >= fields.size;
	},

	createRow = function(torrent, i) {
		var file = torrent.getFile(i), e, name, root, box;

		root = document.createElement('li');
		root.id = 't' + fields.torrent.getId() + 'f' + fields.index;
		root.className = 'inspector_torrent_file_list_entry ' + ((i%2)?'odd':'even');
		elements.root = root;

		e = document.createElement('input');
		e.type = 'checkbox';
		e.className = "file_wanted_control";
		e.title = 'Download file';
		$(e).change(function(ev){ fireWantedChanged( $(ev.currentTarget).prop('checked')); });
		root.checkbox = e;
		root.appendChild(e);

		e = document.createElement('div');
		e.className = 'file-priority-radiobox';
		box = e;

			e = document.createElement('div');
			e.className = 'low';
			e.title = 'Low Priority';
			$(e).click(function(){ firePriorityChanged(-1); });
			elements.priority_low_button = e;
			box.appendChild(e);

			e = document.createElement('div');
			e.className = 'normal';
			e.title = 'Normal Priority';
			$(e).click(function(){ firePriorityChanged(0); });
			elements.priority_normal_button = e;
			box.appendChild(e);

			e = document.createElement('div');
			e.title = 'High Priority';
			e.className = 'high';
			$(e).click(function(){ firePriorityChanged(1); });
			elements.priority_high_button = e;
			box.appendChild(e);

		root.appendChild(box);

		name = file.name || 'Unknown';
		name = name.substring(name.lastIndexOf('/')+1);
		name = name.replace(/([\/_\.])/g, "$1&#8203;");
		e = document.createElement('div');
		e.className = "inspector_torrent_file_list_entry_name";
		setTextContent(e, name);
		root.appendChild(e);

		e = document.createElement('div');
		e.className = "inspector_torrent_file_list_entry_progress";
		root.appendChild(e);
		elements.progress = e;

		refresh();
		return root;
	},

	fireWantedChanged = function(do_want) {
		$(fields.me).trigger('wantedToggled',[ fields.me, do_want ]);
	},
	firePriorityChanged = function(priority) {
		$(fields.me).trigger('priorityToggled',[ fields.me, priority ]);
	};

	/***
	****  PUBLIC
	***/

	this.getElement = function() {
		return elements.root;
	};
	this.getIndex = function() {
		return fields.index;
	};
	this.isEditable = function () {
		return (fields.torrent.getFileCount()>1) && !isDone();
	};

	initialize(torrent, i);
};
