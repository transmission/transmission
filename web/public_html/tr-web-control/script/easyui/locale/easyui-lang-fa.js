if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'صفحه';
	$.fn.pagination.defaults.afterPageText = 'از {pages}';
	$.fn.pagination.defaults.displayMsg = 'نمایش {from} تا {to} از {total} مورد';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'درحال پردازش، لطفا صبر کنید...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'قبول';
	$.messager.defaults.cancel = 'انصراف';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'این فیلد اجباری می باشد.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'لطفا آدرس ایمیل را صحیح وارد کنید.';
	$.fn.validatebox.defaults.rules.url.message = 'لطفا آدرس سایت را صحیح وارد کنید.';
	$.fn.validatebox.defaults.rules.length.message = 'لطفا مقداری بین {0} و {1} وارد کنید.';
	$.fn.validatebox.defaults.rules.remote.message = 'لطفا مقدار این فیلد را تصحیح کنید.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['S','M','T','W','T','F','S'];
	$.fn.calendar.defaults.months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'امروز';
	$.fn.datebox.defaults.closeText = 'بستن';
	$.fn.datebox.defaults.okText = 'قبول';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
