if ($.fn.pagination){
	$.fn.pagination.defaults.beforePageText = 'P&aacute;gina';
	$.fn.pagination.defaults.afterPageText = 'de {pages}';
	$.fn.pagination.defaults.displayMsg = 'Mostrando {from} a {to} de {total} elementos';
}
if ($.fn.datagrid){
	$.fn.datagrid.defaults.loadMsg = 'Procesando, por favor espere ...';
}
if ($.fn.treegrid && $.fn.datagrid){
	$.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager){
	$.messager.defaults.ok = 'Aceptar';
	$.messager.defaults.cancel = 'Cancelar';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
		'combo','combobox','combogrid','combotree',
		'datebox','datetimebox','numberbox',
		'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
	if ($.fn[plugin]){
		$.fn[plugin].defaults.missingMessage = 'Este campo es obligatorio.';
	}
});
if ($.fn.validatebox){
	$.fn.validatebox.defaults.rules.email.message = 'Por favor ingrese una direcci&oacute;n de correo v&aacute;lida.';
	$.fn.validatebox.defaults.rules.url.message = 'Por favor ingrese una URL v&aacute;lida.';
	$.fn.validatebox.defaults.rules.length.message = 'Por favor ingrese un valor entre {0} y {1}.';
	$.fn.validatebox.defaults.rules.remote.message = 'Por favor corrija este campo.';
}
if ($.fn.calendar){
	$.fn.calendar.defaults.weeks = ['Do','Lu','Ma','Mi','Ju','Vi','S&aacute;'];
	$.fn.calendar.defaults.months = ['Ene', 'Feb', 'Mar', 'Abr', 'May', 'Jun', 'Jul', 'Ago', 'Sep', 'Oct', 'Nov', 'Dic'];
}
if ($.fn.datebox){
	$.fn.datebox.defaults.currentText = 'Hoy';
	$.fn.datebox.defaults.closeText = 'Cerrar';
	$.fn.datebox.defaults.okText = 'Aceptar';
}
if ($.fn.datetimebox && $.fn.datebox){
	$.extend($.fn.datetimebox.defaults,{
		currentText: $.fn.datebox.defaults.currentText,
		closeText: $.fn.datebox.defaults.closeText,
		okText: $.fn.datebox.defaults.okText
	});
}
