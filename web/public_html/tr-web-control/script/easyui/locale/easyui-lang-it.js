if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Pagina';
	$.fn.pagination.defaults.afterPageText = 'di {pages}';
	$.fn.pagination.defaults.displayMsg = 'Visualizzazione {from} a {to} di {total} elementi';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'In lavorazione, attendere ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Ok';
	$.messager.defaults.cancel = 'Annulla';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Questo campo è richiesto.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Inserisci un indirizzo email valido.';
	$.fn.validatebox.defaults.rules.url.message = 'Inserisci un URL valido.';
	$.fn.validatebox.defaults.rules.length.message = 'Inserisci un valore tra {0} e {1}.';
	$.fn.validatebox.defaults.rules.remote.message = 'Correggere questo campo.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.firstDay = 1;
	$.fn.calendar.defaults.weeks = ['D','L','M','M','G','V','S'];
	$.fn.calendar.defaults.months = ['Gen', 'Feb', 'Mar', 'Apr', 'Mag', 'Giu', 'Lug', 'Ago', 'Set', 'Ott', 'Nov', 'Dic'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Oggi';
	$.fn.datebox.defaults.closeText = 'Chiudi';
	$.fn.datebox.defaults.okText = 'Ok';
	$.fn.datebox.defaults.formatter = function(date){
		var y = date.getFullYear();
		var m = date.getMonth()+1;
		var d = date.getDate();
		return (d<10?('0'+d):d)+'/'+(m<10?('0'+m):m)+'/'+y;
	};
	$.fn.datebox.defaults.parser = function(s){
		if (!s) return new Date();
		var ss = s.split('/');
		var d = parseInt(ss[0],10);
		var m = parseInt(ss[1],10);
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
