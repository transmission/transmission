if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Էջ';
	$.fn.pagination.defaults.afterPageText = 'ից {pages}';
	$.fn.pagination.defaults.displayMsg = 'Դիտել {from}-ից {to}-ը {total} գրառումից';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Մշակվում է, խնդրում ենք սպասել ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Այո';
	$.messager.defaults.cancel = 'Փակել';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Այս դաշտը պարտադիր է.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Խնդրում ենք մուտքագրել գործող e-mail հասցե.';
	$.fn.validatebox.defaults.rules.url.message = 'Խնդրում ենք մուտքագրել գործող URL.';
	$.fn.validatebox.defaults.rules.length.message = 'Խնդրում ենք մուտքագրել արժեք {0}  {1}.';
	$.fn.validatebox.defaults.rules.remote.message = 'Խնդրում ենք ուղղել այս դաշտը.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.firstDay = 1;
	$.fn.calendar.defaults.weeks  = ['Կ.','Ե.','Ե.','Չ.','Հ.','Ու.','Շ.'];
	$.fn.calendar.defaults.months = ['Հունվար', 'Փետրվար', 'Մարտ', 'Ապրիլ', 'Մայիս', 'Հունիս', 'Հուլիս', 'Օգոստոս', 'Սեպտեմբեր', 'Հոկտեմբեր', 'Նոյեմբեր', 'Դեկտեմբեր'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Այսօր';
	$.fn.datebox.defaults.closeText = 'Փակել';
	$.fn.datebox.defaults.okText = 'Այո';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
