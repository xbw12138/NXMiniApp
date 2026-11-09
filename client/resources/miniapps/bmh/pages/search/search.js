Page({
  data: {
    keyword: '',
    results: [],
    mode: 'search',
    title: '搜索',
    loading: false,
    sourceCount: 0
  },

  onLoad(query) {
    var mode = query.mode === 'fav' ? 'fav' : 'search';
    this.setData({ mode: mode, title: mode === 'fav' ? '我的收藏' : '搜索影片' });
    wx.setNavigationBarTitle({ title: this.data.title });
    var app = getApp();
    this.setData({ sourceCount: app.getEnabledSources().length });
    if (mode === 'fav') this.loadFavorites();
  },

  loadFavorites() {
    var app = getApp();
    var ids = app.getFavorites();
    var results = [];
    ids.forEach(function (id) {
      var parsed = app.parseMovieId(id);
      if (!parsed) return;
      // 从缓存找
      var cache = app.globalData.cache.movies[parsed.source.id] || [];
      for (var i = 0; i < cache.length; i++) {
        if (cache[i].itemId === parsed.itemId) {
          results.push(cache[i]);
          break;
        }
      }
    });
    this.setData({ results: results });
  },

  onInput(e) {
    this.setData({ keyword: e.detail.value });
  },

  onConfirm(e) {
    var kw = (e.detail.value || this.data.keyword || '').trim();
    this.setData({ keyword: kw });
    this.doSearch(kw);
  },

  onSearch() {
    this.doSearch(this.data.keyword);
  },

  doSearch(kw) {
    if (!kw) {
      this.setData({ results: [] });
      return;
    }
    var self = this;
    var app = getApp();
    self.setData({ loading: true, results: [] });
    app.aggregateSearch(kw).then(function (results) {
      self.setData({ results: results, loading: false });
    }).catch(function () {
      self.setData({ loading: false });
    });
  },

  onItem(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + encodeURIComponent(id) });
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
