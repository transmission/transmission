/*
 *   Copyright © Jordan Lee
 *   This code is licensed under the GPL version 2.
 *   <http://www.gnu.org/licenses/old-licenses/gpl-2.0.html>
 */

function FileRow( controller, torrent, i )
{
        this.initialize( controller, torrent, i );
}

FileRow.prototype =
{
	initialize: function( controller, torrent, i )
	{
		this._torrent = torrent;
		this._index = i;
		this.createRow( torrent, i );
	},

	getTorrent: function( )
	{
		return this._torrent;
	},
	getIndex: function( )
	{
		return this._index;
	},

        readAttributes: function(file)
	{
                if( file.index !== undefined && file.index !== this._index ) {
                        this._index = file.index;
                        this._dirty = true;
                }
                if( file.bytesCompleted !== undefined && file.bytesCompleted !== this._done ) {
                        this._done   = file.bytesCompleted;
                        this._dirty = true;
                }
                if( file.length !== undefined && file.length !== this._size ) {
                        this._size   = file.length;
                        this._dirty = true;
                }
                if( file.priority !== undefined && file.priority !== this._prio ) {
                        this._prio   = file.priority;
                        this._dirty = true;
                }
                if( file.wanted !== undefined && file.wanted !== this._wanted ) {
                        this._wanted = file.wanted;
                        this._dirty = true;
                }
        },

        refreshWantedHTML: function() {
                var e = this.getElement();
                var c = [ e.classNameConst ];
                if(!this._wanted) { c.push( 'skip' ); }
                if(this.isDone()) { c.push( 'complete' ); }
                e.className = c.join(' ');
        },
        refreshPriorityHTML: function() {
                var e = this._priority_control;
                var c = [ e.classNameConst ];
                switch( this._prio ) {
                        case 1  : c.push( 'high'   ); break;
                        case -1 : c.push( 'low'    ); break;
                        default : c.push( 'normal' ); break;
                }
                e.className = c.join(' ');
        },
        refreshProgressHTML: function() {
		var pct = 100 * (this._size ? ( this._done / this._size ) : 1.0);
                var c = [ Transmission.fmt.size(this._done),
                          ' of ',
                          Transmission.fmt.size(this._size),
                          ' (',
                          Transmission.fmt.percentString(pct),
                          '%)' ].join('');
                setInnerHTML(this._progress[0], c);
        },
        refreshHTML: function() {
                if( this._dirty ) {
                        this._dirty = false;
                        this.refreshProgressHTML();
                        this.refreshWantedHTML();
                        this.refreshPriorityHTML();
                }
        },
	refresh: function( )
	{
		var i = this.getIndex( );
		var t = this.getTorrent( );
		this.readAttributes( t._files[i] );
		this.refreshHTML();
	},

        isDone: function () {
                return this._done >= this._size;
        },
        isEditable: function () {
                return (this.getTorrent()._files.length>1) && !this.isDone();
        },

        createRow: function( torrent, i )
	{
		var me = this;
		var file = torrent._files[i];
                var name = file.name.substring (file.name.lastIndexOf('/')+1);

                var root = document.createElement('li');
                root.id = 't' + this._torrent.getId() + 'f' + this._index;
                root.classNameConst = 'inspector_torrent_file_list_entry ' + ((i%2)?'odd':'even');
                root.className = root.classNameConst;

                var wanted_div = document.createElement('div');
                wanted_div.className = "file_wanted_control";
                $(wanted_div).bind('click',function(e){ me.fireWantedChanged( !me._wanted ); });

                var pri_div = document.createElement('div');
                pri_div.classNameConst = "file_priority_control";
                pri_div.className = pri_div.classNameConst;
                $(pri_div).bind('click',function(ev){
			var x = ev.pageX;
			var e = ev.target;
			while (e !== null) {
				x -= e.offsetLeft;
				e = e.offsetParent;
			}
			var prio;
			if(iPhone) {
				if( x < 8 ) prio = -1;
				else if( x < 27 ) prio = 0;
				else prio = 1;
			} else {
				if( x < 12 ) prio = -1;
				else if( x < 23 ) prio = 0;
				else prio = 1;
			}
			me.firePriorityChanged( prio );
		});

                var file_div = document.createElement('div');
                file_div.className = "inspector_torrent_file_list_entry_name";
                file_div.innerHTML = name.replace(/([\/_\.])/g, "$1&#8203;");

                var prog_div = document.createElement('div');
                prog_div.className = "inspector_torrent_file_list_entry_progress";

                root.appendChild(wanted_div);
                root.appendChild(pri_div);
                root.appendChild(file_div);
                root.appendChild(prog_div);

                this._element = root;
                this._priority_control = pri_div;
                this._progress = $(prog_div);

		this.refresh();
		return root;
        },

	getElement: function( )
	{
		return this._element;
	},

        fireWantedChanged: function( do_want )
        {
                $(this).trigger('wantedToggled',[ this, do_want ]);
        },
        firePriorityChanged: function( priority )
        {
                $(this).trigger('priorityToggled',[ this, priority ]);
        }
};
