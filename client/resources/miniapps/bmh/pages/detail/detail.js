// 容器的 Yoga 不支持 flex-wrap，推荐位按行切好再渲染。
function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) {
    rows.push(list.slice(i, i + cols));
  }
  return rows;
}

Page({
  data: {
    gid: '',
    sourceId: '',
    itemId: '',
    movie: null,
    typeLabel: '',
    isFav: false,
    similar: [],
    similarRows: [],
    progressText: '',
    loading: true
  },

  onLoad(query) {
    var app = getApp();
    var gid = '';
    try { gid = decodeURIComponent(query.id || ''); } catch (e) { gid = query.id || ''; }
    var parsed = app.parseMovieId(gid);
    if (!parsed) {
      wx.showToast({ title: '影片信息无效' });
      setTimeout(function () { wx.navigateBack({ delta: 1 }); }, 800);
      return;
    }

    this.setData({ gid: gid, sourceId: parsed.source.id, itemId: parsed.itemId });
    wx.setNavigationBarTitle({ title: '加载中…' });

    var self = this;
    app.getDetail(parsed.source, parsed.itemId).then(function (m) {
      var typeLabel = app.sourceTypeLabel(m.sourceType);
      // 同数据源的其他影片作为推荐
      var cache = app.globalData.cache.movies[m.sourceId] || [];
      var similar = cache.filter(function (x) { return x.id !== m.id; }).slice(0, 5);
      var prog = app.getProgress(gid);
      self.setData({
        movie: m,
        typeLabel: typeLabel,
        isFav: app.isFavorite(gid),
        similar: similar,
        similarRows: chunkRows(similar, 5),
        progressText: prog ? '上次观看到 ' + app.fmtTime(prog.seconds) : '',
        loading: false
      });
      wx.setNavigationBarTitle({ title: m.title || '影片详情' });
    }).catch(function (err) {
      self.setData({ loading: false });
      wx.showModal({
        title: '加载失败',
        content: (err && err.message) || '无法获取影片信息',
        showCancel: false,
        success: function () { wx.navigateBack({ delta: 1 }); }
      });
    });
  },

  onPlay() {
    var m = this.data.movie;
    if (!m) return;
    var app = getApp();
    app.addRecent(this.data.gid);
    // 把 sourceId 和 itemId 传给播放页，由播放页获取真实播放地址
    wx.navigateTo({
      url: '/pages/play/play?sourceId=' + encodeURIComponent(this.data.sourceId) +
           '&itemId=' + encodeURIComponent(this.data.itemId) +
           '&title=' + encodeURIComponent(m.title || '')
    });
  },

  onToggleFav() {
    var app = getApp();
    var fav = app.toggleFavorite(this.data.gid);
    this.setData({ isFav: fav });
    wx.showToast({ title: fav ? '已加入收藏' : '已取消收藏' });
  },

  onSimilar(e) {
    var id = e.currentTarget.dataset.id;
    wx.redirectTo({ url: '/pages/detail/detail?id=' + id });
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
