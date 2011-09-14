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
		priority_control: null,
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
		var e = elements.root,
		    c = [ e.classNameConst ];

		if (!fields.isWanted) { c.push('skip'); }
		if (isDone()) { c.push('complete'); }
		e.className = c.join(' ');
	},
	refreshPriorityHTML = function()
	{
		var e = elements.priority_control,
		    c = [ e.classNameConst ];

		switch(fields.priority) {
			case -1 : c.push('low'); break;
			case 1  : c.push('high'); break;
			default : c.push('normal'); break;
		}
		e.className = c.join(' ');
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
		setInnerHTML(elements.progress, c);
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
		var file = torrent.getFile(i),
		    name, root, wanted_div, pri_div, file_div, prog_div;

		root = document.createElement('li');
		root.id = 't' + fields.torrent.getId() + 'f' + fields.index;
		root.classNameConst = 'inspector_torrent_file_list_entry ' + ((i%2)?'odd':'even');
		root.className = root.classNameConst;

		wanted_div = document.createElement('div');
		wanted_div.className = "file_wanted_control";
		$(wanted_div).click(function(){ fireWantedChanged(!fields.isWanted); });

		pri_div = document.createElement('div');
		pri_div.classNameConst = "file_priority_control";
		pri_div.className = pri_div.classNameConst;
		$(pri_div).bind('click',function(ev){
			var prio,
			    x = ev.pageX,
			    e = ev.target;
			while (e) {
				x -= e.offsetLeft;
				e = e.offsetParent;
			}
			// ugh.
			if (isMobileDevice) {
				if (x < 8) prio = -1;
				else if (x < 27) prio = 0;
				else prio = 1;
			} else {
				if (x < 12) prio = -1;
				else if (x < 23) prio = 0;
				else prio = 1;
			}
			firePriorityChanged(prio);
		});

		name = file.name || 'Unknown';
		name = name.substring(name.lastIndexOf('/')+1);
		name = name.replace(/([\/_\.])/g, "$1&#8203;");
		file_div = document.createElement('div');
		file_div.className = "inspector_torrent_file_list_entry_name";
		file_div.innerHTML = name;

		prog_div = document.createElement('div');
		prog_div.className = "inspector_torrent_file_list_entry_progress";

		root.appendChild(wanted_div);
		root.appendChild(pri_div);
		root.appendChild(file_div);
		root.appendChild(prog_div);

		elements.root = root;
		elements.priority_control = pri_div;
		elements.progress = prog_div;

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
