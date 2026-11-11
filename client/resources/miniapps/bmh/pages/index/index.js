// 每批渲染的影片数量：数据源可能返回上千条，一次性建树会卡住主线程。
// 5 列海报墙正好 3 行，一屏加「加载更多」按钮，翻页节奏接近 TV 版。
var PAGE_STEP = 15;
var SCAN_POLL_MS = 3000;
var SCAN_POLL_MAX = 20;
var GRID_COLS = 5;
// 每次 setData 追加的分组数。setData 会重建整棵渲染树，把后面的分组拆到
// 后续帧补上，加载过程中摇杆依然能响应（不会像一次性建树那样整段卡住）。
var BUILD_CHUNK = 2;

// 容器的 Yoga 不支持 flex-wrap，海报墙按行切好再渲染。
function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) {
    rows.push(list.slice(i, i + cols));
  }
  return rows;
}

// 卡片只保留模板要用的字段，减少跨线程传输与建树开销。
function toCard(m) {
  return {
    id: m.id,
    title: m.title,
    poster: m.poster || '',
    ratingText: m.rating ? String(m.rating) : '',
    metaLine: [m.year, m.genre].filter(function (x) { return !!x; }).join(' · ')
  };
}

// 放进 data 的分组视图：绝对不能带 all（整库原始对象），
// 否则每次 setData 都会把整库序列化一遍，几百条就能卡住几秒。
function toView(g) {
  return {
    sourceId: g.sourceId,
    sourceName: g.sourceName,
    sourceType: g.sourceType,
    total: g.total,
    shown: g.shown,
    hasMore: g.hasMore,
    rows: g.rows
  };
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
    // 递增序号：刷新/卸载时让还在排队的构建任务自动失效，避免旧数据回写。
    this._buildSeq = 0;
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
    this._buildSeq++;
    this.stopScanWatch();
  },

  refresh(silent) {
    var app = getApp();
    var sources = app.getEnabledSources();
    var patch = { sources: sources, sourceCount: sources.length };
    if (!silent) {
      patch.loading = true;
      patch.errMsg = '';
    }
    this.setData(patch);

    if (sources.length === 0) {
      this._buildSeq++;
      this._all = [];
      this._groups = [];
      this.setData({ groups: [], chips: [], hero: null, recent: [], loading: false, errMsg: '' });
      return;
    }

    var self = this;
    var seq = ++this._buildSeq;
    app.aggregateMovies().then(function (list) {
      if (seq !== self._buildSeq) return;
      self._all = list || [];
      self.refreshRecent();
      self.buildGroupsChunked(seq);
      self.watchScan(sources);
    }).catch(function (err) {
      if (seq !== self._buildSeq) return;
      self.setData({
        loading: false,
        errMsg: (err && err.message) || '加载失败'
      });
    });
  },

  // 分桶很便宜，先同步做完；卡片的行数据与 setData 拆到多帧，把主线程切开。
  buildGroupsChunked(seq) {
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
    this._groups = groups;

    var chips = this.composeChips();
    // 当前筛选的数据源还在就保留，否则回到「全部」。
    var active = this.data.activeSource;
    if (active && !groups.some(function (g) { return g.sourceId === active; })) {
      active = '';
      this.setData({ activeSource: '' });
    }

    var index = 0;
    var first = true;

    function step() {
      if (seq !== self._buildSeq) return;
      var visible = self.data.activeSource;
      var batch = [];
      for (var n = 0; n < BUILD_CHUNK && index < groups.length; n++, index++) {
        var g = groups[index];
        self.fillGroup(g);
        if (!visible || g.sourceId === visible) batch.push(toView(g));
      }

      if (first) {
        first = false;
        self.setData({ chips: chips, groups: batch, loading: false, errMsg: '' });
      } else if (batch.length) {
        // 防止筛选切换后同一分组被重复追加（模板 key 会撞车）。
        var existing = {};
        (self.data.groups || []).forEach(function (g) { existing[g.sourceId] = true; });
        batch = batch.filter(function (g) { return !existing[g.sourceId]; });
        if (batch.length) {
          self.setData({ groups: (self.data.groups || []).concat(batch), loading: false });
        }
      }

      if (index < groups.length) {
        setTimeout(step, 0);
        return;
      }
      self.finishBuild();
    }

    step();
  },

  finishBuild() {
    if (!this._groups.length && this._hasSmbSource()) {
      this.setData({ errMsg: 'SMB 媒体库首次扫描中，完成后会自动刷新…' });
    } else if (this.data.errMsg) {
      this.setData({ errMsg: '' });
    }
  },

  // 只为当前分页内的影片生成卡片，翻页/筛选时不用重算整库。
  fillGroup(g) {
    var limit = this._limits[g.sourceId] || PAGE_STEP;
    g.total = g.all.length;
    g.shown = Math.min(limit, g.total);
    g.hasMore = g.total > g.shown;
    g.movies = g.all.slice(0, g.shown).map(toCard);
    g.rows = chunkRows(g.movies, GRID_COLS);
  },

  composeChips() {
    var counts = {};
    (this._all || []).forEach(function (m) {
      counts[m.sourceId] = (counts[m.sourceId] || 0) + 1;
    });
    var chips = [{ id: '', label: '全部', count: (this._all || []).length }];
    (this.data.sources || []).forEach(function (s) {
      chips.push({ id: s.id, label: s.name, count: counts[s.id] || 0 });
    });
    return chips;
  },

  onFilter(e) {
    var id = e.currentTarget.dataset.source || '';
    if (id === this.data.activeSource) return;
    var self = this;
    var groups = this._groups;
    groups.forEach(function (g) {
      if (!g.rows) self.fillGroup(g);
    });
    if (id) {
      groups = groups.filter(function (g) { return g.sourceId === id; });
    }
    this.setData({ activeSource: id, groups: groups.map(toView) });
  },

  _hasSmbSource() {
    return (this.data.sources || []).some(function (s) { return s.type === 'smb'; });
  },

  onMore(e) {
    var sourceId = e.currentTarget.dataset.source;
    this._limits[sourceId] = (this._limits[sourceId] || PAGE_STEP) + PAGE_STEP;
    var g = null;
    for (var i = 0; i < this._groups.length; i++) {
      if (this._groups[i].sourceId === sourceId) { g = this._groups[i]; break; }
    }
    if (!g) return;
    this.fillGroup(g);
    // 只替换被翻页的那一组视图，整库对象始终留在页面实例里，不进 data。
    var views = (this.data.groups || []).slice();
    var view = toView(g);
    for (var j = 0; j < views.length; j++) {
      if (views[j].sourceId === sourceId) { views[j] = view; break; }
    }
    this.setData({ groups: views });
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
    // 一次读出数据源表与观看进度，避免每条记录都做一次同步读盘（原来 8 条记录要读十几次）。
    var sources = app.getSources();
    var sourceMap = {};
    sources.forEach(function (s) { sourceMap[s.id] = s; });
    var progressMap = app.getRecentProgress();
    var cache = app.globalData.cache.movies;

    var recent = [];
    ids.slice(0, 8).forEach(function (id) {
      var idx = id.indexOf(':');
      if (idx < 0) return;
      var source = sourceMap[id.slice(0, idx)];
      if (!source) return;
      var itemId = id.slice(idx + 1);
      var list = cache[source.id] || [];
      var movie = null;
      for (var i = 0; i < list.length; i++) {
        if (list[i].itemId === itemId) { movie = list[i]; break; }
      }
      var prog = progressMap[id];
      recent.push({
        id: id,
        title: movie ? movie.title : '最近观看',
        poster: movie ? movie.poster : '',
        sourceName: movie ? movie.sourceName : source.name,
        progressText: prog ? '已看 ' + app.fmtTime(prog.seconds) : '继续观看'
      });
    });

    this.setData({
      recent: recent,
      favCount: app.getFavorites().length,
      hero: this.buildHero(recent)
    });
  },

  // 顶部大图优先「最近观看」，没有播放记录时用库里第一部。
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
    var first = (this._all || [])[0];
    if (!first) return null;
    var metaLine = [first.year, first.genre].filter(function (x) { return !!x; }).join(' · ');
    return {
      id: first.id,
      title: first.title,
      poster: first.poster,
      kicker: '最近入库',
      meta: [first.sourceName, metaLine].filter(function (x) { return !!x; }).join(' · '),
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
