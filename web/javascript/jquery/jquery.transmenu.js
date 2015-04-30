/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

$.widget('tr.transMenu', $.ui.menu, {
	options: {
		open: null,
		close: null
	},

	_create: function() {
		this.selectImpl = this.options.select;
		this.options.select = $.proxy(this._select, this);
		this.element.hide();
		this._superApply(arguments);
	},

	_select: function(event, ui) {
		if (ui.item.is("[aria-haspopup='true']"))
			return;
		ui.id = ui.item.attr("id");
		ui.group = ui.item.attr("radio-group");
		ui.target = $(event.currentTarget);
		if (this.selectImpl(event, ui) !== false)
			this.close();
	},

	open: function(event) {
		this.element.show();
		this.element.css({ position: "absolute", left: 4, top: -this.element.height() - 4 });

		$(document).bind("keydown" + this.eventNamespace, $.proxy(function(event) {
			if (event.which === $.ui.keyCode.ESCAPE)
				this.close();
		}, this));
		$(document).bind("mousedown" + this.eventNamespace + " touchstart" + this.eventNamespace, $.proxy(function(event) {
			if (!$(event.target).closest(".ui-menu-item").length)
				this.close();
		}, this));

		this._trigger("open", event);
	},

	close: function(event) {
		$(document).unbind("keydown" + this.eventNamespace);
		$(document).unbind("mousedown" + this.eventNamespace);
		$(document).unbind("touchstart" + this.eventNamespace);

		this._close(this.element);
		this.element.hide();

		this._trigger("close", event);
	}
});

(function($)
{
	function indicatorClass(type)
	{
		return ['ui-icon', 'ui-icon-' + type];
	}

	function findIndicator(item, type)
	{
		return $(item).find('span.' + indicatorClass(type).join('.'));
	}

	function createIndicator(item, type)
	{
		$(item).prepend($("<span class='" + indicatorClass(type).join(' ') + "'></span>"));
	}

	function indicatorType(item)
	{
		var group = item.attr('radio-group');
		return { type: group !== undefined ? 'bullet' : 'check', group: group };
	}

	$.fn.selectMenuItem = function() {
		var t = indicatorType(this);
		if (t.type == 'bullet')
			this.parent().find('li[radio-group=' + t.group + '] span.' + indicatorClass(t.type).join('.')).remove();
		if (findIndicator(this, t.type).length == 0)
			createIndicator(this, t.type);
		return this;
	};

	$.fn.deselectMenuItem = function() {
		var t = indicatorType(this);
		return findIndicator(this, t.type).remove();
	};

	$.fn.menuItemIsSelected = function() {
		return findIndicator(this, 'bullet').length > 0 || findIndicator(this, 'check').length > 0;
	};
})(jQuery);
