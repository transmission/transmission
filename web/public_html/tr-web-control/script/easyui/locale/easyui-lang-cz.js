if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Strana';
	$.fn.pagination.defaults.afterPageText = 'z {pages}';
	$.fn.pagination.defaults.displayMsg = 'Zobrazuji záznam {from} až {to} z {total}.';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Pracuji, čekejte prosím…';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Ok';
	$.messager.defaults.cancel = 'Zrušit';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Toto pole je vyžadováno.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Zadejte, prosím, platnou e-mailovou adresu.';
	$.fn.validatebox.defaults.rules.url.message = 'Zadejte, prosím, platnou adresu URL.';
	$.fn.validatebox.defaults.rules.length.message = 'Zadejte, prosím, hodnotu mezi {0} a {1}.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['N','P','Ú','S','Č','P','S']; //neděle pondělí úterý středa čtvrtek pátek sobota
	$.fn.calendar.defaults.months = ['led', 'únr', 'bře', 'dub', 'kvě', 'čvn', 'čvc', 'srp', 'zář', 'říj', 'lis', 'pro']; //leden únor březen duben květen červen červenec srpen září říjen listopad prosinec
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Dnes';
	$.fn.datebox.defaults.closeText = 'Zavřít';
	$.fn.datebox.defaults.okText = 'Ok';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
