/* transMenu - v0.1.5 (2007-07-07)
 * Copyright (c) 2007 Roman Weich
 * http://p.sohei.org
 *
 */

(function($)
{
	var defaults = {
		onClick: function(){
			$(this).find('>a').each(function(){
				if ( this.href )
				{
					window.location = this.href;
				}
			});
		},
		arrow_char: '&#x25BA;',
		selected_char: '&#x2713;',
		subDelay: 300,
		direction: 'down',
		mainDelay: 10
	};
	
	var transMenuSettings;

	$.fn.transMenu = function(options) 
	{
		var shown = false;
		var liOffset = 2;

		transMenuSettings = $.extend({}, defaults, options);

		var hideDIV = function(div, delay) {
			//a timer running to show the div?
			if ( div.timer && !div.isVisible ) {
				clearTimeout(div.timer);
			} else if (div.timer) {
				return; //hide-timer already running
			}
			if ( div.isVisible ) {
				div.timer = setTimeout( function() {
					//remove events
					$(div).find('ul li').unbind('mouseover', liHoverIn).unbind('mouseout', liHoverOut).unbind('click', transMenuSettings.onClick);
					$(div).hide();
					div.isVisible = false;
					div.timer = null;
				}, delay);
			}
		};

		var showDIV = function(div, delay) {
			if ( div.timer ) {
				clearTimeout(div.timer);
			}
			if ( !div.isVisible ) {
				div.timer = setTimeout( function() {
					//check if the mouse is still over the parent item - if not dont show the submenu
					if (! $('div').parent().is('.hover')) {
						return;
					}
					//assign events to all div>ul>li-elements
					$(div).find('ul li').mouseover(liHoverIn).mouseout(liHoverOut).click(transMenuSettings.onClick);
					//positioning
					if (! $(div).parent().is('.main')) {
						$(div).css('left', $(div).parent().parent().width() - liOffset);
					}
					
					if (transMenuSettings.direction == 'up') {
						$(div).css('top', ($(div).height() * -1) + $(div).parent().parent().height());
					}
					
					div.isVisible = true; //we use this over :visible to speed up traversing
					$(div).show();
					div.timer = null;
				}, delay);
			}
		};

		//same as hover.handlehover in jquery - just can't use hover() directly - need the ability to unbind only the one hover event
		var testHandleHover = function(e) {
			// Check if mouse(over|out) are still within the same parent element
			var p = (e.type == "mouseover" ? e.fromElement : e.toElement) || e.relatedTarget;
			// Traverse up the tree
			while ( p && p != this ) {
				try { 
					p = p.parentNode;
				} catch(e) { 
					p = this;
				}
			}
			// If we actually just moused on to a sub-element, ignore it
			if ( p == this ) {
				return false;
			}
			return true;
		};
		
		var mainHoverIn = function(e) {
			$(this).addClass('hover').siblings('li.hover').removeClass('hover');
			if ( shown ) {
				hoverIn(this, transMenuSettings.mainDelay);
			}
		};

		var liHoverIn = function(e) {
			if ( !testHandleHover(e) ) {
				return false;
			}
			if ( e.target != this ) {
				//look whether the target is a direct child of this (maybe an image)
				if ( !isChild(this, e.target) ) {
					return;
				}
			}
			hoverIn(this, transMenuSettings.subDelay);
		};

		var hoverIn = function(li, delay) {
			//stop running timers from the other menus on the same level - a little faster than $('>*>div', li.parentNode)
			var n = li.parentNode.firstChild;
			for ( ; n; n = n.nextSibling ) {
				if ( n.nodeType == 1 && n.nodeName.toUpperCase() == 'LI' ) {
					var div = getOneChild(n, 'DIV');
					//clear show-div timer
					if ( div && div.timer && !div.isVisible ) {
						clearTimeout(div.timer);
						div.timer = null;
					}
				}
			}
			//is there a timer running to hide one of the parent divs? stop it
			var pNode = li.parentNode;
			for ( ; pNode; pNode = pNode.parentNode ) {
				if ( pNode.nodeType == 1 && pNode.nodeName.toUpperCase() == 'DIV' ) {
					if (pNode.timer) {
						clearTimeout(pNode.timer);
						pNode.timer = null;
						$(pNode.parentNode).addClass('hover');
					}
				}
			}
			//highlight the current element
			$(li).addClass('hover');
			var innerDiv = $(li).children('div');
			innerDiv = innerDiv.length ? innerDiv[0] : null;
			//is the submenu already visible?
			if ( innerDiv && innerDiv.isVisible ) {
				//hide-timer running?
				if ( innerDiv.timer ) {
					clearTimeout(innerDiv.timer);
					innerDiv.timer = null;
				} else {
					return;
				}
			}
			//hide all open menus on the same level and below and unhighlight the li item (but not the current submenu!)
			$(li.parentNode.getElementsByTagName('DIV')).each( function() {
				if ( this != innerDiv && this.isVisible ) {
					hideDIV(this, delay);
					$(this.parentNode).removeClass('hover');
				}
			});
			//show the submenu, if there is one
			if ( innerDiv ) {
				showDIV(innerDiv, delay);
			}
		};

		var liHoverOut = function(e) {
			if ( !testHandleHover(e) ) {
				return false;
			}
			if ( e.target != this ) {
				//return only if the target is no direct child of this
				if ( !isChild(this, e.target) )  {
					return;
				}
			}
			// Remove the hover from the submenu item, if the mouse is hovering out of the 
			// menu (this is only for the last open (levelwise) (sub-)menu)
			var div = getOneChild(this, 'DIV');
			if ( !div ) {
				$(this).removeClass('hover');
			} else  {
				if ( !div.isVisible ) {
					$(this).removeClass('hover');
				}
			}
		};

		var mainHoverOut = function(e) {
			//no need to test e.target==this, as no child has the same event bound
			var div = getOneChild(this, 'DIV');
			var relTarget = e.relatedTarget || e.toElement; //this is undefined sometimes (e.g. when the mouse moves out of the window), so dont remove hover then
			var p;
			if ( !shown ) {
				$(this).removeClass('hover');
				
			//menuitem has no submenu, so dont remove the hover if the mouse goes outside the menu	
			} else if ( !div && relTarget ) {
				p = $(e.target).parents('UL.trans_menu');
				if ( p.contains(relTarget)) {
					$(this).removeClass('hover');
				}
			} else if ( relTarget )	{
				//remove hover only when moving to anywhere inside the trans_menu
				p = $(e.target).parents('UL.trans_menu');
				if ( !div.isVisible && (p.contains(relTarget)) ) {
					$(this).removeClass('hover');
				}
			}
		};

		var mainClick = function() {
			var div = getOneChild(this, 'DIV');
			//clicked on an open main-menu-item
			if ( div && div.isVisible ) {
				clean();
				$(this).addClass('hover');
			} else {
				hoverIn(this, transMenuSettings.mainDelay);
				shown = true;
				$('ul.trans_menu li').addClass('active');
				$(document).bind('mousedown', checkMouse);
			}
		};

		var checkMouse = function(e) {
			//is the mouse inside a trans_menu? if yes, is it an open (the current) one?
			var vis = false;
			$(e.target).parents('UL.trans_menu').find('div').each( function(){
				if ( this.isVisible ) {
					vis = true;
				}
			});
			if ( !vis ) {
				clean();
			}
		};

		var clean = function() {
			//remove timeout and hide the divs
			$('ul.trans_menu div.outerbox').each(function(){
				if ( this.timer ) {
					clearTimeout(this.timer);
					this.timer = null;
				}
				if ( this.isVisible ) {
					$(this).hide();
					this.isVisible = false;
				}
			});
			$('ul.trans_menu li').removeClass('hover');
			//remove events
			$('ul.trans_menu>li li').unbind('mouseover', liHoverIn).unbind('mouseout', liHoverOut).unbind('click', transMenuSettings.onClick);
			$(document).unbind('mousedown', checkMouse);
			shown = false;
			$('ul.trans_menu li').removeClass('active');
		};

		var getOneChild = function(elem, name) {
			if ( !elem ) {
				return null;
			}
			var n = elem.firstChild;
			for ( ; n; n = n.nextSibling )  {
				if ( n.nodeType == 1 && n.nodeName.toUpperCase() == name ) {
					return n;
				}
			}
			return null;
		};
		
		var isChild = function(elem, childElem) {
			var n = elem.firstChild;
			for ( ; n; n = n.nextSibling ) {
				if ( n == childElem ) {
					return true;
				}
			}
			return false;
		};

	    return this.each(function() {
			//add .contains() to mozilla - http://www.quirksmode.org/blog/archives/2006/01/contains_for_mo.html
			if (window.Node && Node.prototype && !Node.prototype.contains) {
				Node.prototype.contains = function(arg)  {
					return !!(this.compareDocumentPosition(arg) & 16);
				};
			}
			if (! $(this).is('.trans_menu')) {
				$(this).addClass('trans_menu');
			}
			//add shadows
			$('ul', this).shadowBox();
			
			//assign events
			$(this).bind('closemenu', function(){clean();}); //assign closemenu-event, through wich the menu can be closed from outside the plugin
			//add click event handling, if there are any elements inside the main menu
			var liElems = $(this).children('li');
			for ( var j = 0; j < liElems.length; j++ ) {
				if ( getOneChild(getOneChild(getOneChild(liElems[j], 'DIV'), 'UL'), 'LI') ) {
					$(liElems[j]).click(mainClick);
				}
			}
			//add hover event handling and assign classes
			$(liElems).hover(mainHoverIn, mainHoverOut).addClass('main').find('>div').addClass('inner');
			//add the little arrow before each submenu
			if ( transMenuSettings.arrow_char ) {
				var arrow_markup = $("<span class='arrow'>" + transMenuSettings.arrow_char + '</span>');
				// Mozilla float/position hack
				if ($.browser.mozilla) {
					arrow_markup.css('margin-top', '-13px');
				}
				$('div.inner div.outerbox', this).before(arrow_markup);
			}

			//the floating list elements are destroying the layout..so make it nice again..
			$(this).wrap('<div class="main_container"></div>').after('<div style="clear: both; visibility: hidden;"></div>');
	    });
	};
	
	$.fn.transMenu.setDefaults = function(o) {
		$.extend(defaults, o);
	};

	$.fn.shadowBox = function() {
	    return this.each(function() {
			var outer = $('<div class="outerbox"></div>').get(0);
			if ( $(this).css('position') == 'absolute' ) {
				//if the child(this) is positioned abolute, we have to use relative positioning and shrink the outerbox accordingly to the innerbox
				$(outer).css({position:'relative', width:this.offsetWidth, height:this.offsetHeight});
			} else {
				//shrink the outerbox
				$(outer).css('position', 'absolute');
			}
			//add the boxes
			$(this).addClass('innerBox').wrap(outer).
					before('<div class="shadowbox1"></div><div class="shadowbox2"></div><div class="shadowbox3"></div>');
	    });
	};
	
	$.fn.selectMenuItem = function() {
		if (this.find('span.selected').length == 0) {
			this.prepend($("<span class='selected'>" + transMenuSettings.selected_char + "</span>"));
		}
	    return this;
	};
	
	$.fn.deselectMenuItem = function() {
		return this.find('span.selected').remove();
	};
	
	$.fn.menuItemIsSelected = function() {
		return (this.find('span.selected').length > 0);
	};
	
	$.fn.deselectMenuSiblings = function() {
		this.parent().find('span.selected').remove();
	    this.selectMenuItem();
		return this; 
	};
})(jQuery);
