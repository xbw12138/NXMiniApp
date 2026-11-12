var COLS = 5;
var LIB_COLS = 3;

function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) rows.push(list.slice(i, i + cols));
  return rows;
}

function typeText(type) {
  var map = { movies: '电影', tvshows: '剧集', music: '音乐', homevideos: '家庭视频', photos: '照片' };
  return map[type] || '媒体库';
}

Page({
  data: {
    serverLabel: '',
    userName: '账号',
    loading: true,
    errMsg: '',
    hero: null,
    resume: [],
    libraries: [],
    libRows: [],
    latest: [],
    latestRows: [],
    favs: [],
    favRows: []
  },

  onLoad() {
    var app = getApp();
    if (!app.isConfigured()) {
      wx.redirectTo({ url: '/pages/login/login' });
      return;
    }
    var cfg = app.loadConfig();
    this.setData({
      serverLabel: app.serverLabel(cfg),
      userName: cfg.userName || '账号'
    });
    this.loadAll();
  },

  onShow() {
    // 从详情/播放返回时只静默刷新继续观看，避免整页重载丢滚动位置。
    if (this._loadedAt && Date.now() - this._loadedAt > 15000) {
      this.refreshResume();
    }
  },

  loadAll() {
    var self = this;
    var app = getApp();
    self.setData({ loading: true, errMsg: '' });

    function safe(fn, fallback) {
      return fn().catch(function () { return fallback; });
    }

    var viewsPromise = app.getViews();
    var dataPromise = Promise.all([
      safe(function () { return app.getResume(20); }, []),
      safe(function () { return app.getLatest(24); }, []),
      safe(function () { return app.getFavorites(20); }, [])
    ]);

    Promise.all([viewsPromise, dataPromise]).then(function (result) {
      var libs = (result[0] || []).map(function (lib) {
        lib.typeText = typeText(lib.type);
        return lib;
      });
      var resume = (result[1][0] || []).map(function (item) {
        item.railTitle = item.seriesName || item.title;
        item.railSub = item.seriesName ? item.title : '';
        return item;
      });
      var latest = result[1][1] || [];
      var favs = result[1][2] || [];

      self._loadedAt = Date.now();
      self.setData({
        loading: false,
        errMsg: '',
        resume: resume,
        libraries: libs,
        libRows: chunkRows(libs, LIB_COLS),
        latest: latest,
        latestRows: chunkRows(latest, COLS),
        favs: favs,
        favRows: chunkRows(favs, COLS),
        hero: self.buildHero(resume, latest)
      });
    }).catch(function (err) {
      self.setData({
        loading: false,
        errMsg: (err && err.message) || '无法连接飞牛影视服务器'
      });
    });
  },

  refreshResume() {
    var self = this;
    if (self._refreshing) return;
    self._refreshing = true;
    getApp().getResume(20).then(function (list) {
      self._refreshing = false;
      self._loadedAt = Date.now();
      list = list.map(function (item) {
        item.railTitle = item.seriesName || item.title;
        item.railSub = item.seriesName ? item.title : '';
        return item;
      });
      self.setData({
        resume: list,
        hero: self.buildHero(list, self.data.latest)
      });
    }).catch(function () {
      self._refreshing = false;
    });
  },

  buildHero(resume, latest) {
    var item = resume && resume.length ? resume[0] : null;
    if (item) {
      return {
        id: item.id,
        isResume: true,
        kicker: '继续观看',
        title: item.seriesName || item.title,
        subTitle: item.seriesName ? item.title : '',
        meta: item.metaLine,
        progressPercent: item.progressPercent,
        progressText: item.progressText,
        backdrop: item.backdrop || item.poster,
        action: item.progressPercent > 0 ? ('继续播放 · 已看 ' + item.progressPercent + '%') : '继续播放',
        position: item.positionSeconds
      };
    }
    item = latest && latest.length ? latest[0] : null;
    if (!item) return null;
    return {
      id: item.id,
      isResume: false,
      kicker: '最新添加',
      title: item.title,
      subTitle: item.genre || '',
      meta: item.metaLine,
      progressPercent: 0,
      progressText: '',
      backdrop: item.backdrop || item.poster,
      action: '查看详情',
      position: 0
    };
  },

  onHeroTap() {
    var hero = this.data.hero;
    if (!hero) return;
    if (hero.isResume) {
      this.play(hero.id, hero.title, hero.position);
    } else {
      this.openDetail(hero.id);
    }
  },

  onResumeTap(e) {
    var d = e.currentTarget.dataset;
    this.play(d.id, d.title || '', Number(d.position) || 0);
  },

  onItemTap(e) {
    this.openDetail(e.currentTarget.dataset.id);
  },

  onLibraryTap(e) {
    var d = e.currentTarget.dataset;
    wx.navigateTo({
      url: '/pages/library/library?id=' + encodeURIComponent(d.id) +
        '&name=' + encodeURIComponent(d.name || '媒体库')
    });
  },

  play(itemId, title, position) {
    if (!itemId) return;
    wx.navigateTo({
      url: '/pages/play/play?itemId=' + encodeURIComponent(itemId) +
        '&title=' + encodeURIComponent(title || '') +
        '&position=' + encodeURIComponent(String(position || 0))
    });
  },

  openDetail(itemId) {
    if (!itemId) return;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + encodeURIComponent(itemId) });
  },

  goSearch() {
    wx.navigateTo({ url: '/pages/search/search' });
  },

  goSettings() {
    wx.navigateTo({ url: '/pages/settings/settings' });
  },

  onRefresh() {
    this.loadAll();
  }
});
