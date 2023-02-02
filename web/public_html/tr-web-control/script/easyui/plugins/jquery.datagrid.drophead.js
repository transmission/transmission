// 为 easyui.datagrid 增加表头拖放事件
// 仅对单行表头的表格有效
(function($) {
	$.extend($.fn.datagrid.defaults.view, {
		onBeforeRender: function(target, rows) {
			var datagrid = $.data(target, 'datagrid');
			var parent = datagrid.dc.view2;
			if (datagrid.options["drophead"]) {
				parent.find('.datagrid-header-inner .datagrid-header-row td .datagrid-cell').draggable({
					revert: true,
					proxy: function(source) {
						return ($(source).clone().addClass("drag-begin").appendTo($(source).parent()));
					},
					cursor: "",
					handle: ":first-child",
					axis: "h"
				}).droppable({
					accept: '.datagrid-header-inner .datagrid-header-row td .datagrid-cell',
					onDragEnter: function(e, source) {
						// 目标
						var _target = $(e.currentTarget).parent();
						// 源
						var _source = $(source).parent();

						if (_target[0].cellIndex < _source[0].cellIndex) {
							$(this).addClass('drop-over-before');
						} else
							$(this).addClass('drop-over-after');
					},
					onDragLeave: function(e, source) {
						$(this).removeClass('drop-over-after drop-over-before');
					},

					onDrop: function(e, source) {
						$(this).removeClass('drop-over-after drop-over-before');
						// 目标
						var _target = $(e.currentTarget).parent();
						// 源
						var _source = $(source).parent();
						var _targetField = _target.attr("field");
						var _sourceField = _source.attr("field");

						var _targetCells = parent.find("table.datagrid-btable td[field='" + _targetField + "']");
						var _sourceCells = parent.find("table.datagrid-btable td[field='" + _sourceField + "']");

						var _targetIndex = _target[0].cellIndex;
						var _sourceIndex = _source[0].cellIndex;

						if (_targetIndex < _sourceIndex) {
							_target.before(_source);
							$.each(_targetCells, function(index, item) {
								$(item).before(_sourceCells[index]);
							});

						} else {
							_target.after(_source);
							$.each(_targetCells, function(index, item) {
								$(item).after(_sourceCells[index]);
							});
						}

						// 调整字段位置						
						moveArrayIndex(datagrid.options.columns[0], _sourceIndex, _targetIndex);

						// 执行拖放事件
						if (datagrid.options.onHeadDrop)
						{
							datagrid.options.onHeadDrop(_sourceField,_targetField);
						}
					}
				});
			}
		}
	});

	function moveArrayIndex(source, n, m) {
		n = n < 0 ? 0 : (n > source.length - 1 ? source.length - 1 : n);
		m = m < 0 ? 0 : (m > source.length - 1 ? source.length - 1 : m);

		if (n === m) {
			return source;
		} else {
			if (n > m) //向前移动>对两个索引位置及其中间的元素重新赋值[顺推]   
			{

				var temp = [source[m], source[m] = source[n]][0]; //交换n和m的值并将m上的值赋给temp   
				for (var i = m + 1; i <= n; i++) {
					temp = [source[i], source[i] = temp][0];
				}

			} else { //向后移动>对两个索引位置及其中间的元素重新赋值[倒推]   

				var temp = [source[m], source[m] = source[n]][0]; //交换n和m的值并将m上的值赋给temp   
				for (var i = m - 1; i >= n; i--) {
					temp = [source[i], source[i] = temp][0];
				}

			}
			return source;
		}
	};
})(jQuery);