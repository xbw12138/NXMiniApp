// 每批渲染的影片数量：数据源可能返回上千条，一次性建树会卡住主线程。
// 5 列海报墙正好 3 行，一屏加「加载更多」按钮，翻页节奏接近 TV 版。
var PAGE_STEP = 15;
var GRID_COLS = 5;
// 每次 setData 追加的分组数。setData 会重建整棵渲染树，把后面的分组拆到
// 后续帧补上，加载过程中摇杆依然能响应（不会像一次性建树那样整段卡住）。
var BUILD_CHUNK = 2;
// 单个目录最多渲染的条目数：几百个文件夹一次建树会明显卡顿，超出提示。
var BROWSE_LIMIT = 120;

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
    // SMB 目录浏览：一个源一个浏览器，与海报墙并列展示
    browsers: [],
    chips: [],
    activeSource: '',
    recent: [],
    favCount: 0,
    hero: null,
    // 筛选到 SMB 源时收起 Hero 与「继续观看」，只留目录列表。
    hideRecent: false,
    loading: false,
    errMsg: ''
  },

  onLoad() {
    this._all = [];
    this._groups = [];
    this._limits = {};
    this._booted = false;
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
    // 每个页面的 App 实例独立，返回首页时对比数据源签名决定是否重新加载。
    // 只看数量会漏掉「同一个源改了配置」（例如把 SMB 根目录换成子目录）。
    if (!this._booted) return;
    var self = this;
    setTimeout(function () {
      var dirtyAt = 0;
      try { dirtyAt = wx.getStorageSync('bmh_dirty_at') || 0; } catch (e) {}
      if (self._sourcesKey(getApp().getEnabledSources()) !== self._sourcesKeySeen ||
          dirtyAt !== self._dirtySeen) {
        // 数据源刚被增删改：按非静默刷新，立刻给出“正在刷新”的反馈。
        self.refresh(false);
      } else {
        self.refreshRecent();
      }
    }, 0);
  },

  // 影响影片列表的配置都算进签名：根目录、地址、账号变了就必须重新聚合。
  _sourcesKey(sources) {
    return (sources || []).map(function (s) {
      var cfg = s.config || {};
      return [s.id, s.type, s.name || '', cfg.rootPath || '', cfg.server || '',
              cfg.share || '', cfg.url || ''].join('~');
    }).join('|');
  },

  onUnload() {
    this._buildSeq++;
  },

  refresh(silent) {
    var app = getApp();
    var sources = app.getEnabledSources();
    this._sourcesKeySeen = this._sourcesKey(sources);
    try { this._dirtySeen = wx.getStorageSync('bmh_dirty_at') || 0; } catch (e) {}
    var patch = { sources: sources, sourceCount: sources.length };
    if (!silent) {
      patch.loading = true;
      patch.errMsg = '';
    }
    this.setData(patch);
    this._syncHeaderVisibility(sources);

    if (sources.length === 0) {
      this._buildSeq++;
      this._all = [];
      this._groups = [];
      this.setData({ groups: [], browsers: [], chips: [], hero: null, recent: [], loading: false, errMsg: '' });
      return;
    }

    // SMB 不参与海报墙聚合，直接走目录浏览（没有刮削信息，扫整盘也没海报）。
    var smbSources = sources.filter(function (s) { return s.type === 'smb'; });
    var browsers = this.syncBrowsers(smbSources);
    if (smbSources.length === sources.length) {
      // 只有 SMB 源时不必等聚合，直接收掉 loading。
      this._all = [];
      this._groups = [];
      this.refreshRecent();
      this.setData({ groups: [], chips: this.composeChips(browsers), loading: false, errMsg: '' });
      return;
    }

    var self = this;
    var seq = ++this._buildSeq;
    app.aggregateMovies({ skipSmb: true }).then(function (list) {
      if (seq !== self._buildSeq) return;
      self._all = list || [];
      self.refreshRecent();
      self.buildGroupsChunked(seq);
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
    if (!this._groups.length && !(this.data.sources || []).length) {
      this.setData({ errMsg: '' });
    } else if (!this._groups.length && this._noLibrarySource()) {
      this.setData({ errMsg: '未找到影片，请检查数据源配置' });
    } else if (this.data.errMsg) {
      this.setData({ errMsg: '' });
    }
  },

  // 只有在既没有 SMB（有目录浏览兜底）又没有影片时才提示。
  _noLibrarySource() {
    return !(this.data.sources || []).some(function (s) { return s.type === 'smb'; });
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

  // 筛选到 SMB 源时，头部 Hero 与「继续观看」没有意义（SMB 没有刮削信息），直接收起。
  _syncHeaderVisibility(sources) {
    var list = sources || this.data.sources || [];
    var active = this.data.activeSource;
    var exists = false;
    var smbActive = false;
    for (var i = 0; i < list.length; i++) {
      if (!active || list[i].id !== active) continue;
      exists = true;
      smbActive = list[i].type === 'smb';
    }
    var patch = {};
    // 数据源被删掉后筛选条件会失配，一并回到「全部」。
    if (active && !exists) patch.activeSource = '';
    if (smbActive !== !!this.data.hideRecent) patch.hideRecent = smbActive;
    if (Object.keys(patch).length) this.setData(patch);
  },

  composeChips(browserList) {
    var counts = {};
    (this._all || []).forEach(function (m) {
      counts[m.sourceId] = (counts[m.sourceId] || 0) + 1;
    });
    var browsers = browserList || this.data.browsers || [];
    var chips = [{ id: '', label: '全部', count: (this._all || []).length }];
    (this.data.sources || []).forEach(function (s) {
      if (s.type === 'smb') {
        // SMB 是目录浏览，计数取当前目录的视频数，没有就显示 -1（模板里隐藏）。
        var b = null;
        for (var i = 0; i < browsers.length; i++) {
          if (browsers[i].sourceId === s.id) { b = browsers[i]; break; }
        }
        chips.push({ id: s.id, label: s.name, count: b ? b.videoCount : -1 });
        return;
      }
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
    this._syncHeaderVisibility();
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

  // ---------- SMB 目录浏览 ----------
  // 建立/复用浏览器状态。返回首页或改完数据源后保持用户当前所在的目录。
  syncBrowsers(smbSources) {
    if (!smbSources.length) {
      if ((this.data.browsers || []).length) this.setData({ browsers: [], chips: this.composeChips([]) });
      return [];
    }
    var app = getApp();
    var prev = {};
    (this.data.browsers || []).forEach(function (b) { prev[b.sourceId] = b; });
    var list = smbSources.map(function (s) {
      var root = app.smbRootPath(s);
      var old = prev[s.id];
      var path = (old && old.path) ? old.path : root;
      return {
        sourceId: s.id,
        sourceName: s.name,
        rootPath: root,
        path: path,
        crumbs: this._crumbs(root, path),
        loading: true,
        error: '',
        items: (old && old.path === path) ? old.items : [],
        videoCount: (old && old.path === path) ? old.videoCount : 0,
        truncated: false
      };
    }, this);
    this.setData({ browsers: list });
    var self = this;
    list.forEach(function (b) {
      self.loadBrowserDir(b.sourceId, b.path);
    });
    return list;
  },

  // 面包屑：从扫描根目录到当前目录，根目录用「根目录」占位。
  _crumbs(root, path) {
    var crumbs = [{ label: root === '/' ? '共享根目录' : '根目录', path: root }];
    if (!path || path === root) return crumbs;
    var rest = path.indexOf(root) === 0 ? path.slice(root.length) : path;
    var acc = root === '/' ? '' : root;
    rest.split('/').forEach(function (seg) {
      if (!seg) return;
      acc += '/' + seg;
      crumbs.push({ label: seg, path: acc });
    });
    return crumbs;
  },

  loadBrowserDir(sourceId, path) {
    var app = getApp();
    var source = app.getSource(sourceId);
    if (!source) return;
    var self = this;
    this._patchBrowser(sourceId, {
      path: path,
      crumbs: this._crumbs(app.smbRootPath(source), path),
      loading: true,
      error: ''
    });
    app.smbListDir(source, path).then(function (res) {
      // 用户可能已经切到别的目录，慢回来的旧请求直接丢弃。
      var cur = self._browserOf(sourceId);
      if (!cur || cur.path !== path) return;
      var items = res.items.slice(0, BROWSE_LIMIT).map(function (it) {
        return { path: it.path, label: it.label, kind: it.kind, sub: it.sub };
      });
      self._patchBrowser(sourceId, {
        path: res.path,
        items: items,
        videoCount: res.files.length,
        truncated: res.items.length > BROWSE_LIMIT,
        loading: false,
        error: ''
      });
    }).catch(function (err) {
      var cur = self._browserOf(sourceId);
      if (!cur || cur.path !== path) return;
      self._patchBrowser(sourceId, {
        loading: false,
        items: [],
        videoCount: 0,
        error: (err && err.message) || '读取目录失败'
      });
    });
  },

  _patchBrowser(sourceId, patch) {
    var list = (this.data.browsers || []).slice();
    var found = false;
    for (var i = 0; i < list.length; i++) {
      if (list[i].sourceId !== sourceId) continue;
      list[i] = Object.assign({}, list[i], patch);
      found = true;
      break;
    }
    if (!found) return;
    var data = { browsers: list };
    if (patch.videoCount !== undefined) data.chips = this.composeChips(list);
    this.setData(data);
  },

  _browserOf(sourceId) {
    var list = this.data.browsers || [];
    for (var i = 0; i < list.length; i++) {
      if (list[i].sourceId === sourceId) return list[i];
    }
    return null;
  },

  onCrumb(e) {
    var ds = e.currentTarget.dataset;
    var b = this._browserOf(ds.source);
    if (!b || !ds.path) return;
    if (b.path === ds.path) return;
    this.loadBrowserDir(ds.source, ds.path);
  },

  onBrowserUp(e) {
    var b = this._browserOf(e.currentTarget.dataset.source);
    if (!b) return;
    if (b.path === b.rootPath) return;
    var i = b.path.lastIndexOf('/');
    this.loadBrowserDir(b.sourceId, i <= 0 ? '/' : b.path.slice(0, i));
  },

  onBrowserItem(e) {
    var ds = e.currentTarget.dataset;
    var b = this._browserOf(ds.source);
    if (!b || !ds.path) return;
    if (String(ds.kind) === 'dir') {
      this.loadBrowserDir(b.sourceId, ds.path);
      return;
    }
    // 目录里的全部视频作为播放列表：播放页可以直接切上/下一个。
    var videos = (b.items || []).filter(function (it) { return it.kind === 'video'; });
    getApp().savePlaylist(b.sourceId, b.path, videos);
    var title = ds.label || '';
    getApp().addRecent(b.sourceId + ':' + ds.path);
    wx.navigateTo({
      url: '/pages/play/play?sourceId=' + encodeURIComponent(b.sourceId) +
           '&itemId=' + encodeURIComponent(ds.path) +
           '&title=' + encodeURIComponent(title)
    });
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
      // SMB 没有刮削信息和海报，用「继续观看」表达不了什么，直接跳过。
      if (!source || source.type === 'smb') return;
      var itemId = id.slice(idx + 1);
      var list = cache[source.id] || [];
      var movie = null;
      for (var i = 0; i < list.length; i++) {
        if (list[i].itemId === itemId) { movie = list[i]; break; }
      }
      var prog = progressMap[id];
      // 缓存还没建好时用文件名兜底，避免空标题。
      var fallback = itemId.split('/').pop().replace(/\.[^.]+$/, '');
      recent.push({
        id: id,
        title: movie ? movie.title : fallback,
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

  onHeroTap() {
    var hero = this.data.hero;
    if (!hero || !hero.id) return;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + hero.id });
  },

  onItem(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + id });
  },

  onRecent(e) {
    var id = e.currentTarget.dataset.id;
    if (!id) return;
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
