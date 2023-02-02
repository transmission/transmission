if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Страница';
	$.fn.pagination.defaults.afterPageText = 'от {pages}';
	$.fn.pagination.defaults.displayMsg = 'Показани {from} за {to} от {total} продукти';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Обработка, моля изчакайте ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Добре';
	$.messager.defaults.cancel = 'Задрасквам';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Това поле е задължително.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Моля, въведете валиден имейл адрес.';
	$.fn.validatebox.defaults.rules.url.message = 'Моля въведете валиден URL.';
	$.fn.validatebox.defaults.rules.length.message = 'Моля, въведете стойност между {0} и {1}.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['S','M','T','W','T','F','S'];
	$.fn.calendar.defaults.months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Днес';
	$.fn.datebox.defaults.closeText = 'Близо';
	$.fn.datebox.defaults.okText = 'Добре';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
