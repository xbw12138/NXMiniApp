Page({
  data: {
    sources: [],
    sourceCount: 0,
    allMovies: [],
    // 按来源分组
    groups: [],
    recent: [],
    favCount: 0,
    loading: false,
    errMsg: ''
  },

  onLoad() {
    this.refresh();
  },

  onShow() {
    // 每个页面的 App 实例独立，返回首页时按存储里的数据源数量决定是否重新加载。
    if (getApp().getEnabledSources().length !== this.data.sourceCount) {
      this.refresh();
    } else {
      this.refreshRecent();
    }
  },

  refresh() {
    var app = getApp();
    var sources = app.getEnabledSources();
    this.setData({
      sources: sources,
      sourceCount: sources.length
    });

    if (sources.length === 0) {
      this.setData({ allMovies: [], groups: [], loading: false });
      return;
    }

    var self = this;
    self.setData({ loading: true, errMsg: '' });
    app.aggregateMovies().then(function (list) {
      // 按来源分组
      var groupMap = {};
      var groups = [];
      list.forEach(function (m) {
        if (!groupMap[m.sourceId]) {
          groupMap[m.sourceId] = {
            sourceId: m.sourceId,
            sourceName: m.sourceName,
            sourceType: m.sourceType,
            movies: []
          };
          groups.push(groupMap[m.sourceId]);
        }
        groupMap[m.sourceId].movies.push(m);
      });
      self.setData({
        allMovies: list,
        groups: groups,
        loading: false
      });
      self.refreshRecent();
    }).catch(function (err) {
      self.setData({
        loading: false,
        errMsg: (err && err.message) || '加载失败'
      });
    });
  },

  refreshRecent() {
    var app = getApp();
    var ids = app.getRecent();
    var recent = ids.slice(0, 6).map(function (id) {
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
        // 缓存里没有，构造一个占位
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
      favCount: app.getFavorites().length
    });
  },

  onItem(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + encodeURIComponent(id) });
  },

  onRecent(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + encodeURIComponent(id) });
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
