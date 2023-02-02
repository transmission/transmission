if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Страница';
	$.fn.pagination.defaults.afterPageText = 'из {pages}';
	$.fn.pagination.defaults.displayMsg = 'Просмотр {from} до {to} из {total} записей';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Обрабатывается, пожалуйста ждите ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Ок';
	$.messager.defaults.cancel = 'Закрыть';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Это поле необходимо.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Пожалуйста введите корректный e-mail адрес.';
	$.fn.validatebox.defaults.rules.url.message = 'Пожалуйста введите корректный URL.';
	$.fn.validatebox.defaults.rules.length.message = 'Пожалуйста введите зачение между {0} и {1}.';
	$.fn.validatebox.defaults.rules.remote.message = 'Пожалуйста исправте это поле.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.firstDay = 1;
	$.fn.calendar.defaults.weeks  = ['В','П','В','С','Ч','П','С'];
	$.fn.calendar.defaults.months = ['Янв', 'Фев', 'Мар', 'Апр', 'Май', 'Июн', 'Июл', 'Авг', 'Сен', 'Окт', 'Ноя', 'Дек'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Сегодня';
	$.fn.datebox.defaults.closeText = 'Закрыть';
	$.fn.datebox.defaults.okText = 'Ок';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
