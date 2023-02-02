// Get "query string"
String.prototype.getQueryString = function(name,split)
{
	if (split==undefined) split="&";
	var reg = new RegExp("(^|"+split+"|\\?)"+ name +"=([^"+split+"]*)("+split+"|$)"), r;
	if (r=this.match(reg)) return unescape(r[2]); return null;
};


String.prototype.right = function(len)
{
	return (this.substr(-len));
}

String.prototype.getHostName = function() {
	var url = this;
	var result = url;
	var regex = /^\w+\:\/\/([^\/]*).*/;
	var match = url.match(regex);
	if (typeof match != "undefined" && null != match) {
		result = match[1];
		if (result.indexOf(":") != -1) {
			result = result.split(":")[0];
		}
	}
	return result;
}


//公用函数定义
//格式化数字
Number.prototype.formatNumber = function(f)
{
	this.fStr = function(n, f, p)
	{
		if (n==""||n==undefined)
		{
			if (f==""||f==undefined)
			{
				return "";
			}
			else
			{
				return f;
			}
		}
		var fc = s = r = "", pos;
		if (!p) 
		{
			n = n.split("").reverse().join("");
			f = f.split("").reverse().join("");
		}
		
		for (var i = j = 0; i < f.length; i++, j++) 
		{
			s = n.charAt(j);
			if (s == undefined) continue;
			fc = f.charAt(i);
			switch (fc)
			{
				case "#":
					r += s;
					pos = i;
					break;
				case "0":
					r = s || s == fc ? (r + s) : r + 0;
					pos = i;
					break;//原方法,这里对小数点后的处理有点问题.
				case ".":
					r += s == fc ? s : (j--, fc);
					break;
				case ",":
					r += s == fc ? s : (j--, fc);
					break;
				default:
					r += fc;
					j--;
			}
		}
		if (j != n.length && f.charAt(f.length - 1) != "0" && pos != f.length && f.charAt(pos) != "0") 
			r = r.substr(0, pos + 1) + n.substr(j) + r.substr(pos + 1);
	
		r = (p ? r : r.split("").reverse().join("")).replace(/(^,)|(,$)|(,,+)/g, "");
		if (r.substr(0,1)==",")
		{
			r = r.substr(1);
		}
		if (r.substr(0,2)=="-,")
		{
			r = "-"+r.substr(2);
		}
		return r;
	}
	var n = this.toString();
	if (n.length == 0) 
		return "";
	if (f == undefined) 
		return this;
	f = f.split("."), n = n.split(".");
	return f.length > 1 ? this.fStr(n[0], f[0]) + "." + this.fStr(n[1], f[1], 1) : this.fStr(n[0], f[0]);
};

/**
 * 根据指定的十六进制颜色值，返回RGB颜色数值
 */
String.prototype.getRGB = function(){
	var reg = /^#([0-9a-f]{3}|[0-9a-f]{6})$/;
	var color = this.toLowerCase();
	if (color && reg.test(color)) {
		if (color.length === 4) {
			var sColorNew = "#";
			for (var i = 1; i < 4; i += 1) {
				sColorNew += color.slice(i, i + 1).concat(color.slice(i, i + 1));
			}
			color = sColorNew;
		}
		//处理六位的颜色值
		var result = [];
		for (var i = 1; i < 7; i += 2) {
			result.push(parseInt("0x" + color.slice(i, i + 2)));
		}

		return {
			R: result[0],
			G: result[1],
			B: result[2]
		}

	} else {
		return this;
	}
}

/**
 * 获取一个颜色的人眼感知亮度，并以 0~1 之间的小数表示。
 * @param {*} color 
 */
function getGrayLevel(color)
{
	if (!color) {
		color = {
			R:0,G:0,B:0
		};
	}
	if (typeof(color)==="string") {
		color=color.getRGB()
	}
   return (0.299 * color.R + 0.587 * color.G + 0.114 * color.B) / 255;
}

function getLocalTime(time)
{
    return new Date(parseInt(time) * 1000).toLocaleString().replace(/年|月/g, "-").replace(/日/g, " ");
}

function formatLongTime(time,formatString){
	var now = new Date(parseInt(time)*1000);
	return formatDate(now);
}

function formatDate(formatDate, formatString) {
	if (!formatString)
	{
		formatString = "yyyy-mm-dd hh:nn:ss";
	}
    if(formatDate instanceof Date) {  
        var months = new Array("Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec");  
        var yyyy = formatDate.getFullYear();  
        var yy = yyyy.toString().substring(2);  
        var m = formatDate.getMonth()+1;  
        var mm = m < 10 ? "0" + m : m;  
        var mmm = months[m];  
        var d = formatDate.getDate();  
        var dd = d < 10 ? "0" + d : d;  
   
        var h = formatDate.getHours();  
        var hh = h < 10 ? "0" + h : h;  
        var n = formatDate.getMinutes();  
        var nn = n < 10 ? "0" + n : n;  
        var s = formatDate.getSeconds();  
        var ss = s < 10 ? "0" + s : s;  
   
        formatString = formatString.replace(/yyyy/i, yyyy);  
        formatString = formatString.replace(/yy/i, yy);  
        formatString = formatString.replace(/mmm/i, mmm);  
        formatString = formatString.replace(/mm/i, mm);  
        formatString = formatString.replace(/m/i, m);  
        formatString = formatString.replace(/dd/i, dd);  
        formatString = formatString.replace(/d/i, d);  
        formatString = formatString.replace(/hh/i, hh);  
        formatString = formatString.replace(/h/i, h);  
        formatString = formatString.replace(/nn/i, nn);  
        formatString = formatString.replace(/n/i, n);  
        formatString = formatString.replace(/ss/i, ss);  
        formatString = formatString.replace(/s/i, s);  
   
        return formatString;  
    } else {  
        return "";  
    } 
} 

function formatSize(bytes,zeroToEmpty,type)
{
	if (bytes==0)
	{
		if (zeroToEmpty==true)
		{
			return "";
		}
		else
		{
			if (type=="speed")
			{
				return "0.00 KB/s";
			}
			else
				return "0.00";
		}
	}
	var r = "";
	var u = "KB";
	if (bytes < 1000 * 1024)
	{
		r = (bytes / 1024);
		u = "KB";
	}		
	else if (bytes < 1000 * 1048576)
	{
		r = (bytes / 1048576);
		u = "MB";
	}
	else if (bytes < 1000 * 1073741824)
	{
		r = (bytes / 1073741824);
		u = "GB";
	}
	else if (bytes < 1000 * 1099511627776)
	{
		r = (bytes / 1099511627776);
		u = "TB";
	}
	else
	{
		r = (bytes / 1125899906842624);
		u = "PB";
	}
	
	if (type=="speed")
	{
		u+="/s";
	}
	
	return (r.formatNumber("###,###,###,###.00 ")+u);
}

// 根据分钟获取小时
function getHoursFromMinutes(minutes)
{
	return (("00"+parseInt(minutes/60,10)).right(2)+":"+("00"+(minutes % 60)).right(2));
}

// 根据小时获取分钟
function getMinutesFromHours(hours)
{
	return (parseInt(hours.split(":")[0],10)*60+parseInt(hours.split(":")[1],10));
}

// Get the cumulative time
function getTotalTime(time,format)
{
	//time [ms]
	if (!format)
	{
		format = "%dd %hh %mm %ss ";
	}
	//Calculate the difference between the number of days
	var days=Math.floor(time/(24*3600*1000));

	//Calculate the number of hours
	var leave1=time%(24*3600*1000);
	//The number of milliseconds remaining after calculating the number of days
	var hours=Math.floor(leave1/(3600*1000));

	//Calculate the number of minutes
	var leave2=leave1%(3600*1000);
	//The number of milliseconds remaining after the number of hours is counted
	var minutes=Math.floor(leave2/(60*1000));

	//Calculate the number of seconds
	var leave3=leave2%(60*1000);
	//The number of milliseconds remaining after calculating the number of minutes
	var seconds=Math.round(leave3/1000);

	var result = format;
	if (days==0)
	{
		result = result.replace(/(%d+\s)/,"");
	}
	else
		result = result.replace("%d",days);
	
	if (hours==0)
	{
		result = result.replace(/(%h+\s)/,"");
	}
	else
		result = result.replace("%h",hours);

	if (minutes==0)
	{
		result = result.replace(/(%m+\s)/,"");
	}
	else
		result = result.replace("%m",minutes);
	
	if (seconds==0)
	{
		result = result.replace(/(%s+\s)/,"");
	}
	else
		result = result.replace("%s",seconds);
	return result;
}

// Array object sort extension
function arrayObjectSort(field,sortOrder)
{
	return function(object1, object2)
	{
		var value1 = object1[field];
		var value2 = object2[field];
		if (value1 < value2)
		{
			if (sortOrder=="desc")
			{
				return 1;
			}
			else
				return -1;
			
		}
		else if (value1 > value2)
		{
			if (sortOrder=="desc")
			{
				return -1;
			}
			else
				return 1;
		}
		else
		{
			return 0;
		}
	}
}

// Generic time - sharing processing functions
function timedChunk(items, process, context, delay,callback) 
{
	var todo = items.concat();
	if (delay==undefined) delay = 25;

	setTimeout(function() 
	{
		var start = +new Date();

		do {
			process.call(context, todo.shift());
		} while(todo.length > 0 && (+new Date() - start < 100));

		if(todo.length > 0) {
			setTimeout(arguments.callee, delay);
		} else if(callback) {
			callback(items);
		}
	}, delay);
}

// jQuery Extended
(function($){
	// Fade in and out
	$.fn.fadeInAndOut = function(speed,easing,fn){
		var options = {
			speed:speed
			,easing:easing
			,fn:fn
		};
		$.extend(options, $.fn.fadeInAndOut.defaults);
		this.fadeIn(options.speed).delay(options.speed).fadeOut(options.speed,options.easing,options.fn);
	};
	
	// Plugin defaults    
	$.fn.fadeInAndOut.defaults = {    
		speed:1000
		,easing:"swing"
		,fn:null
	};
})(jQuery);

// returns only unique values
function uniq(a) {
    var seen = {};
    return a.filter(function(item) {
        return seen.hasOwnProperty(item) ? false : (seen[item] = true);
    });
}

/**
* 加载指定的文件内容
*/
function loadFileContent(fileType, callback) {
	$("<input id='file-loadContent' type='file' style='display:none;' multiple='true'/>").on("change", function () {
		var fileSelector = this;
		if (fileSelector.files.length > 0 && fileSelector.files[0].name.length > 0) {
			var files = fileSelector.files;
			var count = files.length;
			var index = 0;
			var r = new FileReader();
			r.onload = function (e) {
				callback && callback.call(system, e.target.result);
				readFile();
			};
			r.onerror = function () {
				alert("文件加载失败");
				console.log("文件加载失败");
				readFile();
			};

			function readFile(file) {

				if (index == count) {
					$(fileSelector).remove();
					fileSelector.value = "";
					return;
				}
				var file = files[index];
				var lastIndex = file.name.lastIndexOf(".");
				var fix = file.name.substr(lastIndex + 1);

				index++;

				if (fileType) {
					if (fix != fileType) {
						alert("文件类型错误");
						return;
					}
				}

				r.readAsText(file);
			}
			readFile();
		}
	}).click();
}
/**
* 将指定的内容保存为文件
* @param fileName 文件名
* @param fileData 文件内容
*/
function saveFileAs(fileName, fileData) {
	try {
		var Blob = window.Blob || window.WebKitBlob;

		// Detect availability of the Blob constructor.
		var constructor_supported = false;
		if (Blob) {
			try {
				new Blob([], {
					"type": "text/plain"
				});
				constructor_supported = true;
			} catch (_) {}
		}

		var b = null;
		if (constructor_supported) {
			b = new Blob([fileData], {
				"type": "text/plain"
			});
		} else {
			// Deprecated BlobBuilder API
			var BlobBuilder = window.BlobBuilder || window.WebKitBlobBuilder;
			var bb = new BlobBuilder();
			bb.append(fileData);
			b = bb.getBlob("text/plain");
		}

		saveAs(b, fileName);
	} catch (e) {
		console.log(e.toString());
	}
}