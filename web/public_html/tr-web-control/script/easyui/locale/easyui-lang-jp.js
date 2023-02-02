if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'ページ';
	$.fn.pagination.defaults.afterPageText = '{pages} 中';
	$.fn.pagination.defaults.displayMsg = '全 {total} アイテム中 {from} から {to} を表示中';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = '処理中です。少々お待ちください...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'OK';
	$.messager.defaults.cancel = 'キャンセル';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = '入力は必須です。';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = '正しいメールアドレスを入力してください。';
	$.fn.validatebox.defaults.rules.url.message = '正しいURLを入力してください。';
	$.fn.validatebox.defaults.rules.length.message = '{0} から {1} の範囲の正しい値を入力してください。';
	$.fn.validatebox.defaults.rules.remote.message = 'このフィールドを修正してください。';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['日','月','火','水','木','金','土'];
	$.fn.calendar.defaults.months = ['1月', '2月', '3月', '4月', '5月', '6月', '7月', '8月', '9月', '10月', '11月', '12月'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = '今日';
	$.fn.datebox.defaults.closeText = '閉じる';
	$.fn.datebox.defaults.okText = 'OK';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
