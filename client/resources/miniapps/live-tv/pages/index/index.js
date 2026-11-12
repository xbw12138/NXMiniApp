// 直播电视 · 频道首页
var PER_ROW = 4;
var PAGE_SIZE = 60;
var SEARCH_LIMIT = 120;

function trim(text) {
  return String(text || '').replace(/^\s+|\s+$/g, '');
}

Page({
  data: {
    nav: [],
    active: '',
    keyword: '',
    rows: [],
    visibleCount: 0,
    listCount: 0,
    total: 0,
    scopeLabel: '',
    updatedText: '',
    loading: true,
    errMsg: '',
    more: false
  },

  onLoad() {
    this.library = null;
    this.visible = [];
    this.badLogos = {};
    this.searchTimer = null;
    this.load(false);
  },

  onShow() {
    // 收藏、自定义直播源改动后回到首页，需要重新算一遍。
    if (!this.library) return;
    var dirty = false;
    try {
      dirty = !!wx.getStorageSync('ltv_dirty');
      if (dirty) wx.setStorageSync('ltv_dirty', 0);
    } catch (e) {}
    if (dirty) {
      this.load(false);
      return;
    }
    this.syncState(false);
  },

  onUnload() {
    if (this.searchTimer) clearTimeout(this.searchTimer);
  },

  load(force) {
    var self = this;
    var app = getApp();
    if (!self.library) self.setData({ loading: true, errMsg: '' });

    app.loadLibrary({
      force: force,
      onRefresh: function () {
        // 后台静默刷新完成：不打断阅读，直接换数据。
        if (self.library) self.applyLibrary(app.loadLibraryCached(), true);
      }
    }).then(function (lib) {
      self.applyLibrary(lib, false);
    }).catch(function (err) {
      self.setData({
        loading: false,
        errMsg: (err && err.message) || '直播源加载失败，请检查网络后重试'
      });
    });
  },

  applyLibrary(lib, silent) {
    var app = getApp();
    this.library = lib;
    var nav = this.buildNav(lib);
    var active = this.data.active;
    var exists = false;
    for (var i = 0; i < nav.length; i++) {
      if (nav[i].id === active) { exists = true; break; }
    }
    if (!active || !exists) active = this.pickDefault(nav);

    this.setData({
      nav: nav,
      active: active,
      total: lib.total,
      updatedText: lib.seed ? '正在更新频道列表…' : app.formatUpdated(lib.updated),
      loading: false,
      errMsg: lib.total ? '' : '直播源里暂时没有频道，试试刷新或添加自己的直播源'
    });
    this.syncState(true);
    if (!silent && lib.failed && lib.total) {
      wx.showToast({ title: lib.failed + ' 个直播源暂时不可用', icon: 'none' });
    }
  },

  pickDefault(nav) {
    for (var i = 0; i < nav.length; i++) {
      if (nav[i].id === 'g:央视') return nav[i].id;
    }
    for (var j = 0; j < nav.length; j++) {
      if (nav[j].id.indexOf('g:') === 0) return nav[j].id;
    }
    return nav.length ? nav[0].id : 'all';
  },

  buildNav(lib) {
    var app = getApp();
    var favs = app.getFavs();
    var recents = app.getRecent();
    var nav = [
      { id: 'all', label: '全部频道', count: lib.total },
      { id: 'fav', label: '我的收藏', count: favs.length },
      { id: 'recent', label: '最近观看', count: recents.length }
    ];
    (lib.groups || []).forEach(function (g) {
      nav.push({ id: 'g:' + g.name, label: g.name, count: g.count });
    });
    return nav;
  },

  syncState(resetPage) {
    var app = getApp();
    var nav = this.buildNav(this.library);
    var patch = { nav: nav };
    if (resetPage) patch.active = this.data.active;
    this.setData(patch);
    if (resetPage) this.limit = PAGE_SIZE;
    this.renderList();
  },

  renderList() {
    var app = getApp();
    if (!this.library) return;
    var active = this.data.active;
    var keyword = trim(this.data.keyword).toLowerCase();
    var all = this.library.channels || [];
    var list;
    var label;

    if (keyword) {
      list = all.filter(function (c) {
        return c.name.toLowerCase().indexOf(keyword) >= 0 ||
               c.group.indexOf(keyword) >= 0;
      });
      label = '搜索“' + trim(this.data.keyword) + '” · ' + list.length + ' 个结果';
    } else if (active === 'fav') {
      list = app.getFavs().map(function (k) {
        return app.findChannel(all, k);
      }).filter(function (c) { return !!c; });
      label = '我的收藏 · ' + list.length + ' 个频道';
    } else if (active === 'recent') {
      list = app.getRecent().map(function (k) {
        return app.findChannel(all, k);
      }).filter(function (c) { return !!c; });
      label = '最近观看 · ' + list.length + ' 个频道';
    } else if (active === 'all') {
      list = all.slice();
      label = '全部频道 · ' + list.length + ' 个';
    } else {
      var group = active.slice(2);
      list = all.filter(function (c) { return c.group === group; });
      label = group + ' · ' + list.length + ' 个频道';
    }

    var take = keyword ? SEARCH_LIMIT : (this.limit || PAGE_SIZE);
    this.visible = list.slice(0, take);
    this.setData({
      rows: this.toRows(this.visible),
      visibleCount: this.visible.length,
      listCount: list.length,
      more: list.length > this.visible.length,
      scopeLabel: label
    });
  },

  toRows(list) {
    var app = getApp();
    var favs = app.getFavs();
    var rows = [];
    var row = [];
    for (var i = 0; i < list.length; i++) {
      var c = list[i];
      row.push({
        key: c.key,
        name: app.truncate(c.name, 9),
        logo: this.badLogos[c.key] ? '' : c.logo,
        initial: c.name.charAt(0),
        lines: c.urls ? c.urls.length : 0,
        fav: favs.indexOf(c.key) >= 0,
        ghost: false
      });
      if (row.length === PER_ROW) {
        rows.push(row);
        row = [];
      }
    }
    if (row.length) {
      // 用占位卡片补齐最后一行：容器不支持 flex-wrap，且 wx:if 不能挂在 wx:for 元素上。
      while (row.length < PER_ROW) {
        row.push({ key: '', name: '', logo: '', initial: '', lines: 0, fav: false, ghost: true });
      }
      rows.push(row);
    }
    return rows;
  },

  onNavTap(e) {
    var id = e.currentTarget.dataset.id;
    if (!id || id === this.data.active) return;
    this.limit = PAGE_SIZE;
    this.setData({ active: id, keyword: '' });
    this.renderList();
  },

  onSearch(e) {
    var self = this;
    var value = e.detail.value;
    self.setData({ keyword: value });
    if (self.searchTimer) clearTimeout(self.searchTimer);
    self.searchTimer = setTimeout(function () {
      self.renderList();
    }, 260);
  },

  onSearchClear() {
    this.setData({ keyword: '' });
    this.renderList();
  },

  onRefresh() {
    if (this.refreshing) return;
    var self = this;
    this.refreshing = true;
    wx.showToast({ title: '正在刷新直播源…', icon: 'none' });
    this.load(true);
    setTimeout(function () { self.refreshing = false; }, 1500);
  },

  onMore() {
    this.limit = (this.limit || PAGE_SIZE) + PAGE_SIZE;
    this.renderList();
  },

  onSources() {
    wx.navigateTo({ url: '/pages/sources/sources' });
  },

  onClearRecent() {
    var app = getApp();
    app.clearRecent();
    wx.showToast({ title: '已清空最近观看', icon: 'none' });
    this.syncState(false);
  },

  onPlay(e) {
    var key = e.currentTarget.dataset.key;
    if (!key) return;
    var url = '/pages/play/play?key=' + encodeURIComponent(key) +
              '&scope=' + encodeURIComponent(this.data.active) +
              '&kw=' + encodeURIComponent(trim(this.data.keyword));
    wx.navigateTo({ url: url });
  },

  onLogoErr(e) {
    var key = e.currentTarget.dataset.key;
    if (!key || this.badLogos[key]) return;
    this.badLogos[key] = true;
    if (this.logoTimer) return;
    var self = this;
    this.logoTimer = setTimeout(function () {
      self.logoTimer = null;
      self.setData({ rows: self.toRows(self.visible) });
    }, 500);
  }
});
