if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Seite';
	$.fn.pagination.defaults.afterPageText = 'von {pages}';
	$.fn.pagination.defaults.displayMsg = '{from} bis {to} von {total} Datensätzen';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Verarbeitung läuft, bitte warten ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'OK';
	$.messager.defaults.cancel = 'Abbruch';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Dieses Feld wird benötigt.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Bitte geben Sie eine gültige E-Mail-Adresse ein.';
	$.fn.validatebox.defaults.rules.url.message = 'Bitte geben Sie eine gültige URL ein.';
	$.fn.validatebox.defaults.rules.length.message = 'Bitte geben Sie einen Wert zwischen {0} und {1} ein.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.firstDay = 1;
	$.fn.calendar.defaults.weeks  = ['S','M','D','M','D','F','S'];
	$.fn.calendar.defaults.months = ['Jan', 'Feb', 'Mär', 'Apr', 'Mai', 'Jun', 'Jul', 'Aug', 'Sep', 'Okt', 'Nov', 'Dez'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Heute';
	$.fn.datebox.defaults.closeText = 'Schließen';
	$.fn.datebox.defaults.okText = 'OK';
	$.fn.datebox.defaults.formatter = function(date){
		var y = date.getFullYear();
		var m = date.getMonth()+1;
		var d = date.getDate();
		return (d<10?('0'+d):d)+'.'+(m<10?('0'+m):m)+'.'+y;
	};
	$.fn.datebox.defaults.parser = function(s){
		if (!s) return new Date();
		var ss = s.split('.');
		var m = parseInt(ss[1],10);
		var d = parseInt(ss[0],10);
		var y = parseInt(ss[2],10);
		if (!isNaN(y) && !isNaN(m) && !isNaN(d)){
			return new Date(y,m-1,d);
		} else {
			return new Date();
		}
	};
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
