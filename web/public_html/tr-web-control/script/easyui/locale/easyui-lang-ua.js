if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Сторінка';
	$.fn.pagination.defaults.afterPageText = 'з {pages}';
	$.fn.pagination.defaults.displayMsg = 'Перегляд {from} до {to} з {total} записів';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Обробляється, зачекайте будь даска ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Ок';
	$.messager.defaults.cancel = 'Закрити';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Це поле необхідно.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Будь ласка, введіть коректну e-mail адресу.';
	$.fn.validatebox.defaults.rules.url.message = 'Будь ласка, введіть коректний URL.';
	$.fn.validatebox.defaults.rules.length.message = 'Будь ласка введіть значення між {0} і {1}.';
	$.fn.validatebox.defaults.rules.remote.message = 'Будь ласка виправте це поле.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.firstDay = 1;
	$.fn.calendar.defaults.weeks  = ['В','П','В','С','Ч','П','С'];
	$.fn.calendar.defaults.months = ['Січ', 'Лют', 'Бер', 'Квіт', 'Трав', 'Черв', 'Лип', 'Серп', 'Вер', 'Жовт', 'Лист', 'Груд'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Сьогодні';
	$.fn.datebox.defaults.closeText = 'Закрити';
	$.fn.datebox.defaults.okText = 'Ок';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
