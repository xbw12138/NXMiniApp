// 每批渲染的影片数量：数据源可能返回上千条，一次性建树会卡住主线程。
// 5 列海报墙正好 3 行，一屏加「加载更多」按钮，翻页节奏接近 TV 版。
var PAGE_STEP = 15;
var SCAN_POLL_MS = 3000;
var SCAN_POLL_MAX = 20;
var GRID_COLS = 5;

// 容器的 Yoga 不支持 flex-wrap，海报墙按行切好再渲染。
function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) {
    rows.push(list.slice(i, i + cols));
  }
  return rows;
}

Page({
  data: {
    sources: [],
    sourceCount: 0,
    // 当前展示的分组（可能被数据源筛选过）
    groups: [],
    chips: [],
    activeSource: '',
    recent: [],
    favCount: 0,
    hero: null,
    loading: false,
    errMsg: ''
  },

  onLoad() {
    this._all = [];
    this._groups = [];
    this._limits = {};
    this._booted = false;
    this._scanTimer = null;
    // 先让首帧画出来，再做存储读取 / 网络聚合，避免启动页卡在 loading。
    var self = this;
    setTimeout(function () {
      self._booted = true;
      self.refresh();
    }, 0);
  },

  onShow() {
    // 每个页面的 App 实例独立，返回首页时按存储里的数据源数量决定是否重新加载。
    if (!this._booted) return;
    var self = this;
    setTimeout(function () {
      if (getApp().getEnabledSources().length !== self.data.sourceCount) {
        self.refresh(true);
      } else {
        self.refreshRecent();
      }
    }, 0);
  },

  onUnload() {
    this.stopScanWatch();
  },

  refresh(silent) {
    var app = getApp();
    var sources = app.getEnabledSources();
    this.setData({
      sources: sources,
      sourceCount: sources.length
    });

    if (sources.length === 0) {
      this._all = [];
      this._groups = [];
      this.setData({ groups: [], chips: [], hero: null, recent: [], loading: false, errMsg: '' });
      return;
    }

    var self = this;
    if (!silent) self.setData({ loading: true, errMsg: '' });
    app.aggregateMovies().then(function (list) {
      self._all = list || [];
      self.rebuildGroups();
      self.refreshRecent();
      self.setData({ loading: false });
      self.watchScan(sources);
    }).catch(function (err) {
      self.setData({
        loading: false,
        errMsg: (err && err.message) || '加载失败'
      });
    });
  },

  // 只把当前批次的数据放进 setData，避免上千条影片对象跨线程传输 + 建树。
  rebuildGroups() {
    var self = this;
    var groupMap = {};
    var groups = [];
    (this._all || []).forEach(function (m) {
      if (!groupMap[m.sourceId]) {
        groupMap[m.sourceId] = {
          sourceId: m.sourceId,
          sourceName: m.sourceName,
          sourceType: m.sourceType,
          all: []
        };
        groups.push(groupMap[m.sourceId]);
      }
      groupMap[m.sourceId].all.push(m);
    });

    groups.forEach(function (g) {
      var limit = self._limits[g.sourceId] || PAGE_STEP;
      g.total = g.all.length;
      g.shown = Math.min(limit, g.total);
      g.hasMore = g.total > g.shown;
      // 提前拼好标题行，模板里不再做字符串运算，渲染更快。
      g.movies = g.all.slice(0, g.shown).map(function (m) {
        return {
          id: m.id,
          title: m.title,
          poster: m.poster,
          ratingText: m.rating ? String(m.rating) : '',
          metaLine: [m.year, m.genre].filter(function (x) { return !!x; }).join(' · ')
        };
      });
      g.rows = chunkRows(g.movies, GRID_COLS);
    });

    this._groups = groups;

    var counts = {};
    (this._all || []).forEach(function (m) {
      counts[m.sourceId] = (counts[m.sourceId] || 0) + 1;
    });
    var chips = [{ id: '', label: '全部', count: this._all.length }];
    (this.data.sources || []).forEach(function (s) {
      chips.push({ id: s.id, label: s.name, count: counts[s.id] || 0 });
    });

    var errMsg = this.data.errMsg;
    if (!groups.length && this._hasSmbSource()) {
      errMsg = 'SMB 媒体库首次扫描中，完成后会自动刷新…';
    } else if (groups.length) {
      errMsg = '';
    }

    this.setData({ chips: chips, errMsg: errMsg });
    this.applyFilter();
  },

  applyFilter() {
    var active = this.data.activeSource;
    var exists = active === '' || this._groups.some(function (g) { return g.sourceId === active; });
    if (!exists) active = '';
    var groups = this._groups;
    if (active) {
      groups = groups.filter(function (g) { return g.sourceId === active; });
    }
    this.setData({ activeSource: active, groups: groups });
  },

  onFilter(e) {
    var id = e.currentTarget.dataset.source || '';
    if (id === this.data.activeSource) return;
    this.setData({ activeSource: id });
    this.applyFilter();
  },

  _hasSmbSource() {
    return (this.data.sources || []).some(function (s) { return s.type === 'smb'; });
  },

  onMore(e) {
    var sourceId = e.currentTarget.dataset.source;
    this._limits[sourceId] = (this._limits[sourceId] || PAGE_STEP) + PAGE_STEP;
    this.rebuildGroups();
  },

  // SMB 首次扫描是后台任务：轮询到扫描完成后静默刷新一次，页面会自己出现影片。
  watchScan(sources) {
    var smbSources = (sources || []).filter(function (s) { return s.type === 'smb'; });
    if (!smbSources.length || this._scanTimer) return;
    var hasSmbMovies = (this._all || []).some(function (m) { return m.sourceType === 'smb'; });
    if (hasSmbMovies) return;

    var self = this;
    var app = getApp();
    var mark = app.globalData.smbUpdatedAt || 0;
    var ticks = 0;
    this._scanTimer = setInterval(function () {
      ticks++;
      if ((app.globalData.smbUpdatedAt || 0) > mark) {
        self.stopScanWatch();
        self.refresh(true);
      } else if (ticks >= SCAN_POLL_MAX) {
        self.stopScanWatch();
      }
    }, SCAN_POLL_MS);
  },

  stopScanWatch() {
    if (this._scanTimer) {
      clearInterval(this._scanTimer);
      this._scanTimer = null;
    }
  },

  refreshRecent() {
    var app = getApp();
    var ids = app.getRecent() || [];
    var recent = ids.slice(0, 8).map(function (id) {
      var parsed = app.parseMovieId(id);
      if (!parsed) return null;
      var m = app.globalData.cache.movies[parsed.source.id];
      var movie = null;
      if (m) {
        for (var i = 0; i < m.length; i++) {
          if (m[i].itemId === parsed.itemId) { movie = m[i]; break; }
        }
      }
      if (!movie) {
        movie = { id: id, title: '最近观看', poster: '', sourceName: parsed.source.name };
      }
      var prog = app.getProgress(id);
      return {
        id: id,
        title: movie.title,
        poster: movie.poster,
        sourceName: movie.sourceName,
        progressText: prog ? '已看 ' + app.fmtTime(prog.seconds) : '继续观看'
      };
    }).filter(function (x) { return x; });

    this.setData({
      recent: recent,
      favCount: app.getFavorites().length,
      hero: this.buildHero(recent)
    });
  },

  // 顶部大图优先「最近观看」，没有播放记录时用海报墙第一部。
  buildHero(recent) {
    if (recent && recent.length) {
      var r = recent[0];
      return {
        id: r.id,
        title: r.title,
        poster: r.poster,
        kicker: '继续观看',
        meta: [r.sourceName, r.progressText].filter(function (x) { return !!x; }).join(' · '),
        action: '接着看'
      };
    }
    var first = this._groups[0] && this._groups[0].movies[0];
    if (!first) return null;
    var g = this._groups[0];
    return {
      id: first.id,
      title: first.title,
      poster: first.poster,
      kicker: '最近入库',
      meta: [g.sourceName, first.metaLine].filter(function (x) { return !!x; }).join(' · '),
      action: '查看详情'
    };
  },

  onHeroTap(e) {
    var id = e.currentTarget.dataset.id;
    if (!id) return;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + id });
  },

  onItem(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + id });
  },

  onRecent(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + id });
  },

  onSearch() {
    wx.navigateTo({ url: '/pages/search/search' });
  },

  onSources() {
    wx.navigateTo({ url: '/pages/sources/sources' });
  },

  onFavEntry() {
    wx.navigateTo({ url: '/pages/search/search?mode=fav' });
  }
});
