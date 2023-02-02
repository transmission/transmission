/**
 * EasyUI for jQuery 1.5.4.5
 * 
 * Copyright (c) 2009-2018 www.jeasyui.com. All rights reserved.
 *
 * Licensed under the freeware license: http://www.jeasyui.com/license_freeware.php
 * To use it on other terms please contact us: info@jeasyui.com
 *
 */
(function($){
$.fn.navpanel=function(_1,_2){
if(typeof _1=="string"){
var _3=$.fn.navpanel.methods[_1];
return _3?_3(this,_2):this.panel(_1,_2);
}else{
_1=_1||{};
return this.each(function(){
var _4=$.data(this,"navpanel");
if(_4){
$.extend(_4.options,_1);
}else{
_4=$.data(this,"navpanel",{options:$.extend({},$.fn.navpanel.defaults,$.fn.navpanel.parseOptions(this),_1)});
}
$(this).panel(_4.options);
});
}
};
$.fn.navpanel.methods={options:function(jq){
return $.data(jq[0],"navpanel").options;
}};
$.fn.navpanel.parseOptions=function(_5){
return $.extend({},$.fn.panel.parseOptions(_5),$.parser.parseOptions(_5,[]));
};
$.fn.navpanel.defaults=$.extend({},$.fn.panel.defaults,{fit:true,border:false,cls:"navpanel"});
$.parser.plugins.push("navpanel");
})(jQuery);
(function($){
$(function(){
$.mobile.init();
});
$.mobile={defaults:{animation:"slide",direction:"left",reverseDirections:{up:"down",down:"up",left:"right",right:"left"}},panels:[],init:function(_6){
$.mobile.panels=[];
var _7=$(_6||"body").children(".navpanel:visible");
if(_7.length){
_7.not(":first").children(".panel-body").navpanel("close");
var p=_7.eq(0).children(".panel-body");
$.mobile.panels.push({panel:p,animation:$.mobile.defaults.animation,direction:$.mobile.defaults.direction});
}
$(document).unbind(".mobile").bind("click.mobile",function(e){
var a=$(e.target).closest("a");
if(a.length){
var _8=$.parser.parseOptions(a[0],["animation","direction",{back:"boolean"}]);
if(_8.back){
$.mobile.back();
e.preventDefault();
}else{
var _9=$.trim(a.attr("href"));
if(/^#/.test(_9)){
var to=$(_9);
if(to.length&&to.hasClass("panel-body")){
$.mobile.go(to,_8.animation,_8.direction);
e.preventDefault();
}
}
}
}
});
$(window).unbind(".mobile").bind("hashchange.mobile",function(){
var _a=$.mobile.panels.length;
if(_a>1){
var _b=location.hash;
var p=$.mobile.panels[_a-2];
if(!_b||_b=="#&"+p.panel.attr("id")){
$.mobile._back();
}
}
});
},nav:function(_c,to,_d,_e){
if(window.WebKitAnimationEvent){
_d=_d!=undefined?_d:$.mobile.defaults.animation;
_e=_e!=undefined?_e:$.mobile.defaults.direction;
var _f="m-"+_d+(_e?"-"+_e:"");
var p1=$(_c).panel("open").panel("resize").panel("panel");
var p2=$(to).panel("open").panel("resize").panel("panel");
p1.add(p2).bind("webkitAnimationEnd",function(){
$(this).unbind("webkitAnimationEnd");
var p=$(this).children(".panel-body");
if($(this).hasClass("m-in")){
p.panel("open").panel("resize");
}else{
p.panel("close");
}
$(this).removeClass(_f+" m-in m-out");
});
p2.addClass(_f+" m-in");
p1.addClass(_f+" m-out");
}else{
$(to).panel("open").panel("resize");
$(_c).panel("close");
}
},_go:function(_10,_11,_12){
_11=_11!=undefined?_11:$.mobile.defaults.animation;
_12=_12!=undefined?_12:$.mobile.defaults.direction;
var _13=$.mobile.panels[$.mobile.panels.length-1].panel;
var to=$(_10);
if(_13[0]!=to[0]){
$.mobile.nav(_13,to,_11,_12);
$.mobile.panels.push({panel:to,animation:_11,direction:_12});
}
},_back:function(){
if($.mobile.panels.length<2){
return;
}
var p1=$.mobile.panels.pop();
var p2=$.mobile.panels[$.mobile.panels.length-1];
var _14=p1.animation;
var _15=$.mobile.defaults.reverseDirections[p1.direction]||"";
$.mobile.nav(p1.panel,p2.panel,_14,_15);
},go:function(_16,_17,_18){
_17=_17!=undefined?_17:$.mobile.defaults.animation;
_18=_18!=undefined?_18:$.mobile.defaults.direction;
location.hash="#&"+$(_16).attr("id");
$.mobile._go(_16,_17,_18);
},back:function(){
history.go(-1);
}};
$.map(["validatebox","textbox","passwordbox","filebox","searchbox","combo","combobox","combogrid","combotree","combotreegrid","datebox","datetimebox","numberbox","spinner","numberspinner","timespinner","datetimespinner"],function(_19){
if($.fn[_19]){
$.extend($.fn[_19].defaults,{iconWidth:28,tipPosition:"bottom"});
}
});
$.map(["spinner","numberspinner","timespinner","datetimespinner"],function(_1a){
if($.fn[_1a]){
$.extend($.fn[_1a].defaults,{iconWidth:56,spinAlign:"horizontal"});
}
});
if($.fn.menu){
$.extend($.fn.menu.defaults,{itemHeight:30,noline:true});
}
})(jQuery);

