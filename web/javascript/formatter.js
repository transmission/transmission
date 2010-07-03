/**
***  This file Copyright (C) 2010 Mnemosyne LLC
***
***  This code is licensed under the GPL version 2.
***  For more details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
**/

Transmission.fmt = (function()
{
	var speed_B_str = 'B';
	var speed_K_str = 'kB/s';
	var speed_M_str = 'MB/s';
	var speed_G_str = 'GB/s';

	var size_B_str = 'B';
	var size_K_str = 'KiB';
	var size_M_str = 'MiB';
	var size_G_str = 'GiB';

	return {
		speed_K: 1000,

		size_K: 1024,

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

		/**
		 * Formats the bytes into a string value with B, KiB, MiB, or GiB units.
		 *
		 * @param {Number} bytes the filesize in bytes
		 * @return {String} formatted string with B, KiB, MiB or GiB units.
		 */
		size: function( bytes )
		{
			var size_K = this.size_K;
			var size_M = size_K * size_K;
			var size_G = size_K * size_K * size_K;

			if( !bytes )
				return 'None';
			if( bytes < size_K )
				return bytes.toTruncFixed(0) + size_B_str;

			if( bytes < ( size_K * 100 ) )
				return (bytes/size_K).toTruncFixed(2) + ' ' + size_K_str;
			if( bytes < size_M )
				return (bytes/size_K).toTruncFixed(1) + ' ' + size_K_str;

			if( bytes < ( size_M * 100 ) )
				return (bytes/size_M).toTruncFixed(2) + ' ' + size_M_str;
			if( bytes < size_G )
				return (bytes/size_M).toTruncFixed(1) + ' ' + size_M_str;

			if( bytes < ( size_G * 100 ) )
				return (bytes/size_G).toTruncFixed(2) + ' ' + size_G_str;
			else
				return (bytes/size_G).toTruncFixed(1) + ' ' + size_G_str;
		},

		speed: function( bytes )
		{
			var speed_K = this.speed_K;
			var speed_M = speed_K * speed_K;
			var speed_G = speed_K * speed_K * speed_K;

			if( bytes==undefined || bytes==0 )
				return 'None';

			if( bytes < speed_K )
				return bytes.toTruncFixed(0) + ' ' + speed_B_str;

			if( bytes < ( speed_K * 100 ) )
				return (bytes/speed_K).toTruncFixed(2) + ' ' + speed_K_str;
			if( bytes < speed_M )
				return (bytes/speed_K).toTruncFixed(1) + ' ' + speed_K_str;

			if( bytes < ( speed_M * 100 ) )
				return (bytes/speed_M).toTruncFixed(2) + ' ' + speed_M_str;
			if( bytes < speed_G )
				return (bytes/speed_M).toTruncFixed(1) + ' ' + speed_M_str;

			if( bytes < ( speed_G * 100 ) )
				return (bytes/speed_G).toTruncFixed(2) + ' ' + speed_G_str;
			else
				return (bytes/speed_G).toTruncFixed(1) + ' ' + speed_G_str;
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
