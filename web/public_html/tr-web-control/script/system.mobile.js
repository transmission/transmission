/*
	移动版
*/
var system = {
	version: "1.6.0 beta",
	rootPath: "tr-web-control/",
	codeupdate: "20180906",
	configHead: "transmission-web-control",
	config: {
		autoReload: true,
		reloadStep: 5000,
		pageSize: 30,
		defaultSelectNode: null
	},
	lang: null,
	reloading: false,
	autoReloadTimer: null,
	downloadDir: "",
	islocal: false,
	B64: new Base64(),
	// 当前选中的种子编号		
	currentTorrentId: 0,
	currentContentPage: "home",
	currentContentConfig: null,
	control: {
		tree: null,
		torrentlist: null
	},
	serverConfig: null,
	serverSessionStats: null,
	// 种子列表已选中
	torrentListChecked: false,
	debug: function (label, text) {
		if (window.console) {
			if (window.console.log) {
				window.console.log(label, text);
			}
		}
	},
	setlang: function (lang, callback) {
		// 如果未指定语言，则获取当前浏览器默认语言
		if (!lang) {
			if (this.config.defaultLang)
				lang = this.config.defaultLang;
			else
				lang = navigator.language || navigator.browserLanguage;
			//this.debug("lang",lang);
		}
		if (!lang) lang = "zh-CN";

		// 如果语言代码中包含-，则需要将后半部份转为大写
		if (lang.indexOf("-") != -1) {
			// 因linux对文件有大小写限制，故重新赋值
			lang = lang.split("-")[0].toLocaleLowerCase() + "-" + lang.split("-")[1].toLocaleUpperCase();
		}

		// 如果该语言包没有定义，则使用英文
		if (!this.languages[lang]) {
			lang = "en";
		}

		// 统一使用 _ 替代 -
		lang = lang.replace("-", "_");

		$.getJSON(system.rootPath + "i18n/" + lang + ".json", function (result) {
			if (result) {
				system.lang = $.extend(true, system.defaultLang, result);
			}
			
			system.resetLangText();
			if (callback)
				callback();
		});
	},
	// 设置语言信息
	resetLangText: function () {
		var items = $("*[system-lang]");

		$.each(items, function (key, item) {
			var name = $(item).attr("system-lang");
			$(item).html(eval("system.lang." + name));
		});
	},
	init: function (lang, islocal) {
		this.readConfig();
		transmission.options.getFolders = false;
		if (this.lang == null) {
			this.setlang(lang, function () {
				system.initdata()
			});
		} else {
			this.initdata();
		}
	},
	initdata: function () {
		$(document).attr("title", this.lang.system.title + " " + this.version);
		//this.control.torrentlist = $("#content-torrent-list ul");
		this.control.torrentlist = $("#torrent-list");
		this.connect();
	},
	// 从 cookies 里加载配置		
	readConfig: function () {
		// 将原来的cookies的方式改为本地存储的方式
		var config = this.getStorageData(this.configHead + '.system');
		if (config) {
			this.config = $.extend(this.config, JSON.parse(config));
		}
	},
	// 在 cookies 里保存参数		
	saveConfig: function () {
		this.setStorageData(this.configHead + '.system', JSON.stringify(this.config));
	},
	getStorageData: function (key, defaultValue) {
		return (window.localStorage[key] == null ? defaultValue : window.localStorage[key]);
	},
	setStorageData: function (key, value) {
		window.localStorage[key] = value;
	},
	// 连接服务器		
	connect: function () {
		// 当种子总数发生变化时，重新获取种子信息
		transmission.on.torrentCountChange = function () {
			system.reloadTorrentBaseInfos();
		};
		// 提交错误时
		transmission.on.postError = function () {
			//system.reloadTorrentBaseInfos();
		};
		// 初始化连接
		transmission.init({
			islocal: true
		}, function () {
			system.reloadSession(true);
			system.getServerStatus();
		});
	},
	// 重新加载服务器信息		
	reloadSession: function (isinit) {
		transmission.getSession(function (result) {
			system.serverConfig = result;
			if (result["alt-speed-enabled"] == true) {
				$("#status_alt_speed").show();
			} else {
				$("#status_alt_speed").hide();
			}

			system.downloadDir = result["download-dir"];

			// rpc-version 版本为 15 起，不再提供 download-dir-free-space 参数，需从新的方法获取
			if (parseInt(system.serverConfig["rpc-version"]) >= 15) {
				transmission.getFreeSpace(system.downloadDir, function (result) {
					system.serverConfig["download-dir-free-space"] = result.arguments["size-bytes"];
					system.showFreeSpace(result.arguments["size-bytes"]);
				});
			} else {
				system.showFreeSpace(system.serverConfig["download-dir-free-space"]);
			}
		});
	},
	showFreeSpace: function (size) {
		var tmp = size;
		if (tmp == -1) {
			tmp = system.lang["public"]["text-unknown"];
		} else {
			tmp = formatSize(tmp);
		}
		$("#status_freespace").text(tmp);
	},
	// 获取服务器当前状态		
	getServerStatus: function () {
		if (this.reloading) return;
		clearTimeout(this.autoReloadTimer);

		this.reloading = true;
		transmission.getStatus(function (data) {
			system.reloading = false;
			$("#status_downloadspeed").html(formatSize(data["downloadSpeed"], false, "speed"));
			$("#status_uploadspeed").html(formatSize(data["uploadSpeed"], false, "speed"));
			system.serverSessionStats = data;
		});
	},
	// 重新获取种子信息		
	reloadTorrentBaseInfos: function (ids) {
		if (this.reloading) return;
		clearTimeout(this.autoReloadTimer);
		this.reloading = true;
		var oldInfos = {
			trackers: transmission.trackers,
			folders: transmission.torrents.folders
		}

		// 获取所有种子id信息
		transmission.torrents.getallids(function (resultTorrents) {
			var ignore = new Array();
			for (var index in resultTorrents) {
				var item = resultTorrents[index];
				ignore.push(item.id);
			}

			// 错误的编号列表
			var errorIds = transmission.torrents.getErrorIds(ignore, true);

			if (errorIds.length > 0) {
				transmission.torrents.getallids(function () {
					system.resetTorrentInfos(oldInfos);
				}, errorIds);
			} else {
				system.resetTorrentInfos(oldInfos);
			}
		}, ids);
	},
	//		
	resetTorrentInfos: function (oldInfos) {
		var currentTorrentId = this.currentTorrentId;

		// 已暂停
		if (transmission.torrents.status[transmission._status.stopped]) {
			this.updateCount("paused", transmission.torrents.status[transmission._status.stopped].length);
		} else {
			this.updateCount("paused", 0);
		}

		// 做种
		if (transmission.torrents.status[transmission._status.seed]) {
			this.updateCount("sending", transmission.torrents.status[transmission._status.seed].length);
		} else {
			this.updateCount("sending", 0);
		}


		// 校验
		if (transmission.torrents.status[transmission._status.check]) {
			this.updateCount("check", transmission.torrents.status[transmission._status.check].length);
		} else {
			this.updateCount("check", 0);
		}


		// 下载中
		if (transmission.torrents.status[transmission._status.download]) {
			this.updateCount("downloading", transmission.torrents.status[transmission._status.download].length);
		} else {
			this.updateCount("downloading", 0);
		}


		// 活动中
		this.updateCount("actively", transmission.torrents.actively.length);
		// 发生错误
		this.updateCount("error", transmission.torrents.error.length);
		// 警告
		this.updateCount("warning", transmission.torrents.warning.length);


		system.reloading = false;

		if (system.config.autoReload) {
			system.autoReloadTimer = setTimeout(function () {
				system.reloadData();
			}, system.config.reloadStep);
		}

		// 总大小
		this.updateCount("all", transmission.torrents.count);

		if (this.currentContentPage == "torrent-list") {
			var _config = this.currentContentConfig;
			_config.reload = true;
			this.showContent(_config);
		}
	},
	// 更新状态中		
	updateCount: function (nodeId, count) {
		var item = $("#count-" + nodeId);
		item.text(count);
		if (count == 0) {
			item.hide();
		} else
			item.show();
	},
	// 重新加载数据		
	reloadData: function () {
		this.reloadSession();
		this.reloading = false;
		this.getServerStatus();
		this.reloading = false;
		this.reloadTorrentBaseInfos();
	},
	// 显示指定的内容		
	showContent: function (target) {
		var _default = {
			page: "",
			type: "",
			data: "",
			title: this.lang.system.title,
			reload: false,
			callback: null
		};
		var config = null;
		if (typeof (target) == "string") {
			_default.page = target;
			config = _default;
		} else
			config = jQuery.extend(_default, target);
		if (config.page == this.currentContentPage && !config.reload) {
			return;
		}

		$("#content-" + config.page).show();
		if (config.page != this.currentContentPage) {
			$("#content-" + this.currentContentPage).hide();
			this.control.torrentlist.find("input:checked").prop("checked", false).checkboxradio("refresh");
			this.torrentListChecked = false;
		}
		$("#torrent-page-bar").hide();
		if (!this.torrentListChecked)
			$("#torrent-toolbar").hide();

		this.currentContentPage = config.page;
		switch (config.type) {
			case "torrent-list":
				config.title = this.lang.tree[config.data];
				this.loadTorrentToList({
					target: config.data
				});
				break;
		}
		$("#page-title").text(config.title);
		config.reload = false;
		this.currentContentConfig = config;
		if (config.callback)
			config.callback();
	},
	getTorrentFromType: function (type) {
		var torrents = null;
		switch (type) {
			case "torrent-all":
			case "all":
			case "servers":
				torrents = transmission.torrents.all;
				break;
			case "paused":
				torrents = transmission.torrents.status[transmission._status.stopped];
				break;
			case "sending":
				torrents = transmission.torrents.status[transmission._status.seed];
				break;

			case "seedwait":
				torrents = transmission.torrents.status[transmission._status.seedwait];
				break;

			case "check":
				torrents = transmission.torrents.status[transmission._status.check];
				break;
			case "checkwait":
				torrents = transmission.torrents.status[transmission._status.checkwait];
				break;

			case "downloading":
				torrents = transmission.torrents.status[transmission._status.download];
				break;
			case "downloadwait":
				torrents = transmission.torrents.status[transmission._status.downloadwait];
				break;

			case "actively":
				torrents = transmission.torrents.actively;
				break;

			case "error":
				torrents = transmission.torrents.error;
				break;

			case "warning":
				torrents = transmission.torrents.warning;
				break;

			case "search-result":
				torrents = transmission.torrents.searchResult;
				break;

			default:
				break;
		}
		return torrents;
	},
	// 加载种子列表		
	loadTorrentToList: function (config) {
		// 如果有种子选中，则不重新加载列表
		if (this.torrentListChecked) return;
		if (!transmission.torrents.all) {
			return;
		}
		var def = {
			node: null,
			page: 1,
			target: "all"
		};

		jQuery.extend(def, config);
		if (!config.target) return;

		var torrents = this.getTorrentFromType(config.target);

		this.config.defaultSelectNode = config.target;
		this.saveConfig();

		var datas = new Array();
		this.control.torrentlist.empty();
		for (var index in torrents) {
			if (!torrents[index]) {
				continue;
			}
			var percentDone = parseFloat(torrents[index].percentDone * 100).toFixed(2);
			var status = this.lang.torrent["status-text"][torrents[index].status];
			if (torrents[index].error != 0) {
				status = "<span class='text-status-error'>" + status + "</span>";
			} else if (torrents[index].warning) {
				status = "<span class='text-status-warning' title='" + torrents[index].warning + "'>" + status + "</span>";
			}
			var data = {
				id: torrents[index].id,
				name: this.getTorrentNameBar(torrents[index]),
				totalSize: torrents[index].totalSize,
				percentDone: this.getTorrentProgressBar(percentDone, torrents[index]),
				percentDoneNumber: percentDone,
				status: status,
				addedDate: formatLongTime(torrents[index].addedDate),
				completeSize: (torrents[index].totalSize - torrents[index].leftUntilDone),
				rateDownload: torrents[index].rateDownload,
				rateUpload: torrents[index].rateUpload,
				leecherCount: torrents[index].leecher,
				seederCount: torrents[index].seeder,
				uploadRatio: torrents[index].uploadRatio,
				uploadedEver: torrents[index].uploadedEver
			};

			datas.push(data);
			//this.appendTorrentToList(data);
		}
		if (datas.length == 0) {
			setTimeout(function () {
				system.showContent('home');
			}, 100);
			return;
		}
		if (this.torrentPager.onGotoPage == null) {
			this.torrentPager.onGotoPage = function (datas) {
				system.control.torrentlist.empty();
				$("#torrent-toolbar").hide();
				for (var key in datas) {
					system.appendTorrentToList(datas[key]);
				}
				// 刷新列表并指定事件
				$(system.control.torrentlist).listview('refresh').find("input[type='checkbox']").click(function () {
					system.changeTorrentToolbar(this, data);
					if (system.torrentListChecked) {
						system.control.torrentlist.find("a[name='torrent']").css("marginLeft", "0px");
					} else {
						system.control.torrentlist.find("a[name='torrent']").css("marginLeft", "0px");
					}
				}).checkboxradio();
			}
		}

		this.torrentPager.setDatas(datas, config.target);
	},
	// 添加种子信息到列表		
	appendTorrentToList: function (data) {
		var replaces = {
			id: data.id,
			name: data.name,
			rateDownload: formatSize(data.rateDownload, false, "speed"),
			rateUpload: formatSize(data.rateUpload, false, "speed"),
			completeSize: formatSize(data.completeSize),
			totalSize: formatSize(data.totalSize),
			percentDone: data.percentDone
		};

		// 由于不能以对象的方式来创建 listview 子项，所以只能用拼接字符串的方式
		var templates = "<li id='li-torrent-$id$' torrentid='$id$' style='padding:0px;'><a name='torrent' style='padding:0px;margin-left:0px;'>" +
			"<label data-corners='false' style='margin:0px;border:0px;padding:0px;'>" +
			"<input type='checkbox' id='torrent-$id$'/><label for='torrent-$id$'>" +
			"<h3 style='margin:0px;'>$name$</h3>" +
			"<div style='padding:0px 10px 5px 0px;'>$percentDone$</div>" +
			"<p class='torrent-list-infos'>↓$rateDownload$ ↑$rateUpload$|$completeSize$/$totalSize$</p>" +
			"</label></label></a>" +
			"<a class='more'></a>";

		// 替换模板中以 $w$ 方式组成的内容
		templates = templates.replace(/\$([^\$]*)\$/g, function (string, key) {
			return replaces[key];
		});

		var li = $(templates);
		// 向右划动
		li.on("swiperight", function (event) {
			//system.control.torrentlist.find("#torrent-"+$(this).attr("torrentid")).click();
			system.control.torrentlist.find("a[name='torrent']").css("marginLeft", "0px");
		});
		li.on("swipeleft", function (event) {
			//system.control.torrentlist.find("#torrent-"+$(this).attr("torrentid")).click();
			system.control.torrentlist.find("a[name='torrent']").css("marginLeft", "0px");
		});

		li.appendTo(this.control.torrentlist);
	},
	// 获取种子名称显示区域的内容		
	getTorrentNameBar: function (torrent) {
		var className = "";
		var tip = torrent.name;
		switch (torrent.status) {
			case transmission._status.stopped:
				className = "iconlabel icon-pause-small";
				break;

			case transmission._status.check:
				className = "iconlabel icon-checking";
				break;

			case transmission._status.download:
				className = "iconlabel icon-down";
				break;

			case transmission._status.seed:
				className = "iconlabel icon-up";
				break;

			case transmission._status.seedwait:
			case transmission._status.downloadwait:
			case transmission._status.checkwait:
				className = "iconlabel icon-wait";
				break;
		}

		if (torrent.warning) {
			className = "iconlabel icon-warning-type1";
			tip += "\n\n" + this.lang["public"]["text-info"] + ": " + torrent.warning;
		}

		if (torrent.error != 0) {
			className = "iconlabel icon-exclamation";
			tip += "\n\n" + this.lang["public"]["text-info"] + ": " + torrent.errorString;
		}


		return '<span class="' + className + '" title="' + tip + '">' + torrent.name + '</span>';
	},
	// 获取指定种子的进度条		
	getTorrentProgressBar: function (progress, torrent) {
		progress = progress + "%";
		var className = "";
		switch (torrent.status) {
			case transmission._status.stopped:
				className = "torrent-progress-stop";
				break;

			case transmission._status.checkwait:
			case transmission._status.check:
				className = "torrent-progress-check";
				break;

			case transmission._status.downloadwait:
			case transmission._status.download:
				className = "torrent-progress-download";
				break;

			case transmission._status.seedwait:
			case transmission._status.seed:
				className = "torrent-progress-seed";
				break;
		}
		if (torrent.warning) {
			className = "torrent-progress-warning";
		}
		if (torrent.error != 0) {
			className = "torrent-progress-error";
		}
		return '<div class="torrent-progress" title="' + progress + '"><div class="torrent-progress-text">' + progress + '</div><div class="torrent-progress-bar ' + className + '" style="width:' + progress + ';"></div></div>';
	},
	// 改变种子的工具栏		
	changeTorrentToolbar: function (source, item) {
		var checked = this.control.torrentlist.find("input:checked");
		$("#torrent-checked-count").html(checked.length);
		if (checked.length > 0) {
			this.torrentListChecked = true;
			$("#torrent-toolbar").show();
		} else {
			this.torrentListChecked = false;
			$("#torrent-toolbar").hide();
		}
		if (item)
			this.currentTorrentId = item.id;
	},
	// 种子列表分页处理		
	torrentPager: {
		datas: null,
		pageSize: 30,
		pageNumber: 0,
		pageCount: 0,
		count: 0,
		onGotoPage: null,
		currentDatas: null,
		pageBar: null,
		controls: {
			prev: null,
			next: null,
			number: null
		},
		head: "",
		init: function (datas) {
			this.pageBar = $("#torrent-page-bar");
			this.controls.next = this.pageBar.find("#page-next");
			this.controls.next.click(function () {
				system.torrentPager.gotoPage("next");
			});
			this.controls.prev = this.pageBar.find("#page-prev");
			this.controls.prev.click(function () {
				system.torrentPager.gotoPage("prev");
			});

			this.controls.number = this.pageBar.find("#page-number");
			if (datas) {
				this.setDatas(datas);
			}
		},
		setDatas: function (datas, head) {
			if (!this.datas) {
				this.init();
			}
			this.datas = datas;
			this.pageBar.show();
			this.count = this.datas.length;
			this.pageCount = parseInt(this.count / this.pageSize);
			if (this.count % this.pageSize > 0) {
				this.pageCount++;
			}
			if (this.pageCount == 1) {
				this.pageBar.hide();
			}
			if (this.head == head) {
				this.gotoPage();
			} else
				this.gotoPage(1);
			this.head = head;
		},
		gotoPage: function (page) {
			if (typeof (page) == "number") {
				this.pageNumber = page;
			} else {
				switch (page) {
					case "next":
						this.pageNumber++;
						break;
					case "prev":
						this.pageNumber--;
						break;
				}
			}
			if (this.pageNumber > this.pageCount) {
				this.pageNumber = this.pageCount;
			}
			if (this.pageNumber < 1) {
				this.pageNumber = 1;
			}
			var start = (this.pageNumber - 1) * parseInt(this.pageSize);
			var end = start + parseInt(this.pageSize);
			this.currentDatas = (this.datas.slice(start, end));

			this.controls.number.text(this.pageNumber + "/" + this.pageCount);
			if (this.pageNumber > 1) {
				this.controls.prev.show();
			} else {
				this.controls.prev.hide();
			}

			if (this.pageNumber < this.pageCount) {
				this.controls.next.show();
			} else {
				this.controls.next.hide();
			}

			if (this.onGotoPage) {
				this.onGotoPage(this.currentDatas);
			}
		}
	},
	// 开始/暂停已选择的种子	
	changeSelectedTorrentStatus: function (status, button, options) {
		var items = this.control.torrentlist.find("input:checked");
		var ids = new Array();
		if (!status) {
			status = "start";
		}
		for (var i = 0; i < items.length; i++) {
			ids.push(parseInt(items[i].id.replace("torrent-", "")));
		}

		if (ids.length > 0) {
			var arguments = {
				ids: ids
			};
			switch (status) {
				case "remove":
					arguments["delete-local-data"] = options.removeData;
					break;
				case "verify":
					if (ids.length == 1) {
						var torrent = transmission.torrents.all[ids[0]];
						if (torrent.percentDone > 0) {
							if (confirm(system.lang.toolbar.tip["recheck-confirm"]) == false) {
								return;
							}
						}
					} else if (confirm(system.lang.toolbar.tip["recheck-confirm"]) == false) {
						return;
					}
					break;

			}
			button = $(button);
			button.attr("disabled", true);
			transmission.exec({
				method: "torrent-" + status,
				arguments: arguments
			}, function (data) {
				button.attr("disabled", false);
				system.reloadTorrentBaseInfos();
			});
			// 操作完成后，取消所有已选择的项
			this.torrentListChecked = false;
		}
	},
	// 增加种子		
	addTorrentsToServer: function (urls, count, autostart, savepath, callback) {
		//this.config.autoReload = false;
		var index = count - urls.length;
		var url = urls.shift();
		if (!url) {
			this.showStatus(this.lang.system.status.queuefinish);
			//this.config.autoReload = true;
			this.getServerStatus();
			if (callback)
				callback();
			return;
		}
		this.showStatus(this.lang.system.status.queue, (count - index + 1));
		transmission.addTorrentFromUrl(url, savepath, autostart, function (data) {
			system.addTorrentsToServer(urls, count, autostart, savepath, callback);
		});
	},
	showStatus: function (msg, count) {
		if (!msg) {
			$("#status").hide();
			return;
		}
		$("#status").show();
		$("#status-msg").html(msg);
		if ($.isNumeric(count))
			$("#status-count").html(count).show();
		else
			$("#status-count").hide();
	}
};

$(document).ready(function () {
	// Loads the default language content
	$.getJSON(system.rootPath + "i18n/en.json").done(function (result) {
		system.defaultLang = result;
	});

	// Loads a list of available languages
	$.getJSON(system.rootPath + "i18n.json").done(function (result) {
		system.languages = result;
		system.init(location.search.getQueryString("lang"), location.search.getQueryString("local"));
	});
});