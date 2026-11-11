// 搜索 / 收藏结果统一成海报卡需要的字段，模板里不再做字符串拼接。
function toCard(m) {
  return {
    id: m.id,
    title: m.title,
    poster: m.poster || '',
    rating: m.rating ? String(m.rating) : '',
    meta: [m.year, m.genre, m.sourceName].filter(function (x) { return !!x; }).join(' · ')
  };
}

// 容器的 Yoga 不支持 flex-wrap，结果海报按行切好再渲染。
function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) {
    rows.push(list.slice(i, i + cols));
  }
  return rows;
}

Page({
  data: {
    keyword: '',
    results: [],
    resultRows: [],
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
    if (mode === 'fav') {
      // App 实例不跨页共享，收藏要用缓存还原影片信息，先聚合一次（命中缓存时几乎瞬时）。
      var self = this;
      self.setData({ loading: true });
      app.aggregateMovies().then(function () {
        self.loadFavorites();
        self.setData({ loading: false });
      }).catch(function () {
        self.setData({ loading: false });
      });
    }
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
          results.push(toCard(cache[i]));
          break;
        }
      }
    });
    this.setData({ results: results, resultRows: chunkRows(results, 5) });
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
      this.setData({ results: [], resultRows: [] });
      return;
    }
    var self = this;
    var app = getApp();
    self.setData({ loading: true, results: [], resultRows: [] });
    app.aggregateSearch(kw).then(function (results) {
      var cards = results.map(toCard);
      self.setData({ results: cards, resultRows: chunkRows(cards, 5), loading: false });
    }).catch(function () {
      self.setData({ loading: false });
    });
  },

  onItem(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + id });
  }
});
