if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'Página';
	$.fn.pagination.defaults.afterPageText = 'de {pages}';
	$.fn.pagination.defaults.displayMsg = 'Mostrando {from} a {to} de {total} itens';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Processando, aguarde ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Ok';
	$.messager.defaults.cancel = 'Cancelar';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Esse campo é requerido.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Insira um endereço de email válido.';
	$.fn.validatebox.defaults.rules.url.message = 'Insira uma URL válida.';
	$.fn.validatebox.defaults.rules.length.message = 'Insira uma valor entre {0} e {1}.';
	$.fn.validatebox.defaults.rules.remote.message = 'Corrija esse campo.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['D','S','T','Q','Q','S','S'];
	$.fn.calendar.defaults.months = ['Jan', 'Fev', 'Mar', 'Abr', 'Mai', 'Jun', 'Jul', 'Ago', 'Set', 'Out', 'Nov', 'Dez'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Hoje';
	$.fn.datebox.defaults.closeText = 'Fechar';
	$.fn.datebox.defaults.okText = 'Ok';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
