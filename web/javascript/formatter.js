/**
***  This file Copyright (C) 2010 Mnemosyne LLC
***
***  This code is licensed under the GPL version 2.
***  For more details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
**/

Transmission.fmt = (function()
{
	var KB_val = 1024;
	var MB_val = 1024 * 1024;
	var GB_val = 1024 * 1024 * 1024;
	var KB_str = 'KiB';
	var MB_str = 'MiB';
	var GB_str = 'GiB';

	return {
		MODE_IEC: 1,
		MODE_SI: 2,

		/*
		 *   Format a percentage to a string
		 */
		percentString: function( x ) {
			if( x < 10.0 )
				return x.toTruncFixed( 2 );
			else if( x < 100.0 )
				return x.toTruncFixed( 1 );
			else
				return x.toTruncFixed( 0 );
		},

		/*
		 *   Format a ratio to a string
		 */
		ratioString: function( x ) {
			if( x ==  -1 )
				return "None";
			else if( x == -2 )
				return '&infin;';
			else
				return this.percentString( x );
		},

		setMode: function( mode ) {
			if( mode == MODE_IEC ) {
				this.KB_val = 1024;
				this.MB_val = this.KB_val * 1024;
				this.GB_val = this.MB_val * 1024;
				this.KB_str = 'KiB';
				this.MB_str = 'MiB';
				this.GB_str = 'GiB';
			} else {
				this.KB_val = 1000;
				this.MB_val = this.KB_val * 1000;
				this.GB_val = this.MB_val * 1000;
				this.KB_str = 'kB';
				this.MB_str = 'MB';
				this.GB_str = 'GB';
			}
		},

		/**
		 * Formats the bytes into a string value with B, KiB, MiB, or GiB units.
		 *
		 * @param {Number} bytes the filesize in bytes
		 * @return {String} formatted string with B, KiB, MiB or GiB units.
		 */
		size: function( bytes )
		{
			if( !bytes )
				return 'None';

			if( bytes < KB_val )
				return bytes.toFixed(0) + ' B';

			if( bytes < ( KB_val * 100 ) )
				return Math.roundWithPrecision(bytes/KB_val, 2) + ' ' + KB_str;
			if( bytes < MB_val )
				return Math.roundWithPrecision(bytes/KB_val, 1) + ' ' + KB_str;

			if( bytes < ( MB_val * 100 ) )
				return Math.roundWithPrecision(bytes/MB_val, 2) + ' ' + MB_str;
			if( bytes < GB_val )
				return Math.roundWithPrecision(bytes/MB_val, 1) + ' ' + MB_str;

			if( bytes < ( GB_val * 100 ) )
				return Math.roundWithPrecision(bytes/GB_val, 2) + ' ' + GB_str;
			else
				return Math.roundWithPrecision(bytes/GB_val, 1) + ' ' + GB_str;
		},

		speed: function( bytes )
		{
			if( !bytes )
				return 'None';
			else
				return this.size( bytes ) + '/s';
		},

		timeInterval: function( seconds )
		{
			var result;
			var days = Math.floor(seconds / 86400);
			var hours = Math.floor((seconds % 86400) / 3600);
			var minutes = Math.floor((seconds % 3600) / 60);
			var seconds = Math.floor((seconds % 3600) % 60);

			if (days > 0 && hours == 0)
				result = days + ' days';
			else if (days > 0 && hours > 0)
				result = days + ' days ' + hours + ' hr';
			else if (hours > 0 && minutes == 0)
				result = hours + ' hr';
			else if (hours > 0 && minutes > 0)
				result = hours + ' hr ' + minutes + ' min';
			else if (minutes > 0 && seconds == 0)
				result = minutes + ' min';
			else if (minutes > 0 && seconds > 0)
				result = minutes + ' min ' + seconds + ' seconds';
			else
				result = seconds + ' seconds';

			return result;
		},

		timestamp: function( seconds )
		{
			var myDate = new Date(seconds*1000);
			var now = new Date();

			var date = "";
			var time = "";

			var sameYear = now.getFullYear() == myDate.getFullYear();
			var sameMonth = now.getMonth() == myDate.getMonth();

			var dateDiff = now.getDate() - myDate.getDate();
			if(sameYear && sameMonth && Math.abs(dateDiff) <= 1){
				if(dateDiff == 0){
					date = "Today";
				}
				else if(dateDiff == 1){
					date = "Yesterday";
				}
				else{
					date = "Tomorrow";
				}
			}
			else{
				date = myDate.toDateString();
			}

			var hours = myDate.getHours();
			var period = "AM";
			if(hours > 12){
				hours = hours - 12;
				period = "PM";
			}
			if(hours == 0){
				hours = 12;
			}
			if(hours < 10){
				hours = "0" + hours;
			}
			var minutes = myDate.getMinutes();
			if(minutes < 10){
				minutes = "0" + minutes;
			}
			var seconds = myDate.getSeconds();
				if(seconds < 10){
					seconds = "0" + seconds;
			}

			time = [hours, minutes, seconds].join(':');

			return [date, time, period].join(' ');
		}
	}
})();
