/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

$.widget('tr.transMenu', $.ui.menu, {
  _create(...args) {
    this.selectImpl = this.options.select;
    this.options.select = this._select.bind(this);
    this.element.hide();
    this._superApply(args);
  },

  _select(event, ui) {
    if (ui.item.is("[aria-haspopup='true']")) {
      return;
    }
    ui.id = ui.item.attr('id');
    ui.group = ui.item.attr('radio-group');
    ui.radio_id = ui.item.attr('radio-id');
    ui.target = $(event.currentTarget);
    if (this.selectImpl(event, ui) !== false) {
      this.close();
    }
  },

  close(event) {
    const e = $(document);
    e.unbind(`keydown${this.eventNamespace}`);
    e.unbind(`mousedown${this.eventNamespace}`);
    e.unbind(`touchstart${this.eventNamespace}`);

    this._close(this.element);
    this.element.hide();

    this._trigger('close', event);
  },

  open(event) {
    this.element.show();
    this.element.css({
      left: 4,
      position: 'absolute',
      top: -this.element.height() - 4,
    });

    $(document).bind(`keydown${this.eventNamespace}`, (ev) => {
      if (ev.which === $.ui.keyCode.ESCAPE) {
        this.close();
      }
    });
    $(document).bind(`mousedown${this.eventNamespace} touchstart${this.eventNamespace}`, (ev) => {
      if (!ev.target.closest('.ui-menu-item')) {
        this.close();
      }
    });

    this._trigger('open', event);
  },

  options: {
    close: null,
    open: null,
  },
});

(function ($) {
  const BlankIcon = 'ui-icon-blank';
  const SelectIcon = 'ui-icon-check';
  const RadioIcon = 'ui-icon-bullet';

  $.fn.selectMenuItem = function () {
    const item = this.get(0);
    const radio_group = item.getAttribute('radio-group');
    const selection_icon = radio_group ? RadioIcon : SelectIcon;

    // if it's a radio group, deselect the others
    if (radio_group) {
      const selector = `[radio-group="${radio_group}"] .${RadioIcon}`;
      for (const e of item.parentNode.querySelectorAll(selector)) {
        e.classList.replace(RadioIcon, BlankIcon);
      }
    }

    // select this one
    item.querySelector('.ui-icon').classList.add(selection_icon);
    return this;
  };

  $.fn.deselectMenuItem = function () {
    const item = this.get(0);
    const radio_group = item.getAttribute('radio-group');
    const selection_icon = radio_group ? RadioIcon : SelectIcon;

    for (const e of item.getElementsByClassName(selection_icon)) {
      e.classList.replace(selection_icon, BlankIcon);
    }

    return this;
  };

  $.fn.menuItemIsSelected = function () {
    return !this.get(0).querySelector(BlankIcon);
  };
})(jQuery);
