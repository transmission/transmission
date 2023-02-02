if ($.fn.pagination) {
    $.fn.pagination.defaults.beforePageText = 'Sida';
    $.fn.pagination.defaults.afterPageText = 'av {pages}';
    $.fn.pagination.defaults.displayMsg = 'Visar {from} till {to} av {total} poster';
}
if ($.fn.datagrid) {
    $.fn.datagrid.defaults.loadMsg = 'Bearbetar, vänligen vänta ...';
}
if ($.fn.treegrid && $.fn.datagrid) {
    $.fn.treegrid.defaults.loadMsg = $.fn.datagrid.defaults.loadMsg;
}
if ($.messager) {
    $.messager.defaults.ok = 'Ok';
    $.messager.defaults.cancel = 'Avbryt';
}
$.map(['validatebox','textbox','passwordbox','filebox','searchbox',
        'combo','combobox','combogrid','combotree',
        'datebox','datetimebox','numberbox',
        'spinner','numberspinner','timespinner','datetimespinner'], function(plugin){
    if ($.fn[plugin]){
        $.fn[plugin].defaults.missingMessage = 'Detta fält är obligatoriskt.';
    }
});
if ($.fn.validatebox) {
    $.fn.validatebox.defaults.rules.email.message = 'Vänligen ange en korrekt e-post adress.';
    $.fn.validatebox.defaults.rules.url.message = 'Vänligen ange en korrekt URL.';
    $.fn.validatebox.defaults.rules.length.message = 'Vänligen ange ett nummer mellan {0} och {1}.';
    $.fn.validatebox.defaults.rules.remote.message = 'Vänligen åtgärda detta fält.';
}
if ($.fn.calendar) {
    $.fn.calendar.defaults.weeks = ['Sön', 'Mån', 'Tis', 'Ons', 'Tors', 'Fre', 'Lör'];
    $.fn.calendar.defaults.months = ['Jan', 'Feb', 'Mar', 'Apr', 'Maj', 'Jun', 'Jul', 'Aug', 'Sep', 'Okt', 'Nov', 'Dec'];
}
if ($.fn.datebox) {
    $.fn.datebox.defaults.currentText = 'I dag';
    $.fn.datebox.defaults.closeText = 'Stäng';
    $.fn.datebox.defaults.okText = 'Ok';
}
if ($.fn.datetimebox && $.fn.datebox) {
    $.extend($.fn.datetimebox.defaults, {
        currentText: $.fn.datebox.defaults.currentText,
        closeText: $.fn.datebox.defaults.closeText,
        okText: $.fn.datebox.defaults.okText
    });
}
