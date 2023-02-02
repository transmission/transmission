if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Bladsy';
	$.fn.pagination.defaults.afterPageText = 'Van {pages}';
	$.fn.pagination.defaults.displayMsg = 'Wys (from) tot (to) van (total) items';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Verwerking, wag asseblief ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Ok';
	$.messager.defaults.cancel = 'Die styl';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Die veld is verpligtend.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = "Gee 'n geldige e-pos adres.";
	$.fn.validatebox.defaults.rules.url.message = "Gee 'n geldige URL nie.";
	$.fn.validatebox.defaults.rules.length.message = "Voer 'n waarde tussen {0} en {1}.";
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['S','M','T','W','T','F','S'];
	$.fn.calendar.defaults.months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Vandag';
	$.fn.datebox.defaults.closeText = 'Sluit';
	$.fn.datebox.defaults.okText = 'Ok';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
