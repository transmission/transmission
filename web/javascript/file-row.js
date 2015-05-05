/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function FileRow(torrent, depth, name, indices, even)
{
	var fields = {
		have: 0,
		indices: [],
		isWanted: true,
		priorityLow: false,
		priorityNormal: false,
		priorityHigh: false,
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

	initialize = function(torrent, depth, name, indices, even) {
		fields.torrent = torrent;
		fields.indices = indices;
		createRow(torrent, depth, name, even);
	},

	refreshWantedHTML = function()
	{
		var e = $(elements.root);
		e.toggleClass('skip', !fields.isWanted);
		e.toggleClass('complete', isDone());
		$(e[0].checkbox).prop('disabled', !isEditable());
		$(e[0].checkbox).prop('checked', fields.isWanted);
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
	refreshImpl = function() {
		var i,
		    file,
		    have = 0,
		    size = 0,
		    wanted = false,
		    low = false,
		    normal = false,
		    high = false;

		// loop through the file_indices that affect this row
		for (i=0; i<fields.indices.length; ++i) {
			file = fields.torrent.getFile (fields.indices[i]);
			have += file.bytesCompleted;
			size += file.length;
			wanted |= file.wanted;
			switch (file.priority) {
				case -1: low = true; break;
				case  0: normal = true; break;
				case  1: high = true; break;
			}
		}

		if ((fields.have != have) || (fields.size != size)) {
			fields.have = have;
			fields.size = size;
			refreshProgressHTML();
		}

		if (fields.isWanted !== wanted) {
			fields.isWanted = wanted;
			refreshWantedHTML();
		}

		if (fields.priorityLow !== low) {
			fields.priorityLow = low;
			$(elements.priority_low_button).toggleClass('selected', low);
		}

		if (fields.priorityNormal !== normal) {
			fields.priorityNormal = normal;
			$(elements.priority_normal_button).toggleClass('selected', normal);
		}

		if (fields.priorityHigh !== high) {
			fields.priorityHigh = high;
			$(elements.priority_high_button).toggleClass('selected', high);
		}
	},

	isDone = function () {
		return fields.have >= fields.size;
	},
	isEditable = function () {
		return (fields.torrent.getFileCount()>1) && !isDone();
	},

	createRow = function(torrent, depth, name, even) {
		var e, root, box;

		root = document.createElement('li');
		root.className = 'inspector_torrent_file_list_entry' + (even?'even':'odd');
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

		e = document.createElement('div');
		e.className = "inspector_torrent_file_list_entry_name";
		setTextContent(e, name);
		$(e).click(function(){ fireNameClicked(-1); });
		root.appendChild(e);

		e = document.createElement('div');
		e.className = "inspector_torrent_file_list_entry_progress";
		root.appendChild(e);
		$(e).click(function(){ fireNameClicked(-1); });
		elements.progress = e;

		$(root).css('margin-left', '' + (depth*16) + 'px');

		refreshImpl();
		return root;
	},

	fireWantedChanged = function(do_want) {
		$(fields.me).trigger('wantedToggled',[ fields.indices, do_want ]);
	},
	firePriorityChanged = function(priority) {
		$(fields.me).trigger('priorityToggled',[ fields.indices, priority ]);
	},
	fireNameClicked = function() {
		$(fields.me).trigger('nameClicked',[ fields.me, fields.indices ]);
	};

	/***
	****  PUBLIC
	***/

	this.getElement = function() {
		return elements.root;
	};
	this.refresh = function() {
		refreshImpl();
	};

	initialize(torrent, depth, name, indices, even);
};
