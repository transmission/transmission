if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = '第';
	$.fn.pagination.defaults.afterPageText = '共{pages}頁';
	$.fn.pagination.defaults.displayMsg = '顯示{from}到{to},共{total}記錄';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = '正在處理，請稍待。。。';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = '確定';
	$.messager.defaults.cancel = '取消';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = '該輸入項為必輸項';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = '請輸入有效的電子郵件地址';
	$.fn.validatebox.defaults.rules.url.message = '請輸入有效的URL地址';
	$.fn.validatebox.defaults.rules.length.message = '輸入內容長度必須介於{0}和{1}之間';
	$.fn.validatebox.defaults.rules.remote.message = '請修正此欄位';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['日','一','二','三','四','五','六'];
	$.fn.calendar.defaults.months = ['一月','二月','三月','四月','五月','六月','七月','八月','九月','十月','十一月','十二月'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = '今天';
	$.fn.datebox.defaults.closeText = '關閉';
	$.fn.datebox.defaults.okText = '確定';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
if ($.fn.datetimespinner){
	$.fn.datetimespinner.defaults.selections = [[0,4],[5,7],[8,10],[11,13],[14,16],[17,19]]
}
