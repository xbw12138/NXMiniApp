var COLS = 5;

function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) rows.push(list.slice(i, i + cols));
  return rows;
}

Page({
  data: {
    itemId: '',
    movie: null,
    isSeries: false,
    typeLabel: '',
    isFav: false,
    playLabel: '立即播放',
    seasons: [],
    seasonIndex: 0,
    activeSeasonName: '剧集',
    episodes: [],
    episodesLoading: false,
    similar: [],
    similarRows: [],
    errMsg: ''
  },

  onLoad(query) {
    var id = query.id || '';
    this.setData({ itemId: id });
    if (!id) {
      this.setData({ errMsg: '影片参数缺失' });
      return;
    }
    this.load();
  },

  load() {
    var self = this;
    var app = getApp();
    self.setData({ errMsg: '', movie: null });

    app.getDetail(self.data.itemId).then(function (movie) {
      var isSeries = movie.type === 'series';
      self.setData({
        movie: movie,
        isSeries: isSeries,
        typeLabel: isSeries ? '剧集' : '电影',
        isFav: !!movie.isFavorite,
        playLabel: isSeries ? '播放选集' : (movie.progressPercent > 0 ? '继续播放' : '立即播放')
      });
      wx.setNavigationBarTitle({ title: movie.title || '影片详情' });
      self.loadSimilar();
      if (isSeries) self.loadSeasons();
    }).catch(function (err) {
      self.setData({ errMsg: (err && err.message) || '无法获取影片信息' });
    });
  },

  loadSeasons() {
    var self = this;
    var app = getApp();
    app.getSeasons(self.data.itemId).then(function (seasons) {
      self.setData({
        seasons: seasons,
        activeSeasonName: seasons.length ? seasons[0].name : '剧集'
      });
      if (seasons.length > 0) {
        self.loadEpisodes(seasons[0].id);
      } else {
        self.loadEpisodes('');
      }
    }).catch(function () {
      self.loadEpisodes('');
    });
  },

  loadEpisodes(seasonId) {
    var self = this;
    self.setData({ episodesLoading: true });
    getApp().getEpisodes(self.data.itemId, seasonId).then(function (list) {
      var first = list.length ? list[0] : null;
      self.setData({
        episodes: list,
        episodesLoading: false,
        playLabel: first ? ('播放 ' + first.label) : '播放选集'
      });
    }).catch(function () {
      self.setData({ episodes: [], episodesLoading: false, playLabel: '播放选集' });
    });
  },

  onSeasonTap(e) {
    var index = Number(e.currentTarget.dataset.index);
    var season = this.data.seasons[index];
    if (!season || index === this.data.seasonIndex) return;
    this.setData({ seasonIndex: index, activeSeasonName: season.name || '剧集' });
    this.loadEpisodes(season.id);
  },

  play() {
    var movie = this.data.movie;
    if (!movie) return;
    if (this.data.isSeries) {
      var episodes = this.data.episodes || [];
      if (!episodes.length) {
        wx.showToast({ title: this.data.episodesLoading ? '选集加载中…' : '暂无可播放剧集' });
        return;
      }
      var target = episodes[0];
      for (var i = 0; i < episodes.length; i++) {
        if (episodes[i].progressPercent > 0 && episodes[i].progressPercent < 99) {
          target = episodes[i];
          break;
        }
      }
      this.playItem(target.id, movie.title + ' ' + target.label, target.positionSeconds);
      return;
    }
    this.playItem(movie.id, movie.title, movie.positionSeconds);
  },

  onEpisodeTap(e) {
    var d = e.currentTarget.dataset;
    var movie = this.data.movie;
    var title = (movie ? movie.title : '') + ' ' + (d.label || '');
    this.playItem(d.id, title, Number(d.position) || 0);
  },

  playItem(itemId, title, position) {
    if (!itemId) return;
    wx.navigateTo({
      url: '/pages/play/play?itemId=' + encodeURIComponent(itemId) +
        '&title=' + encodeURIComponent(title || '') +
        '&position=' + encodeURIComponent(String(position || 0))
    });
  },

  onToggleFav() {
    var self = this;
    var target = !self.data.isFav;
    self.setData({ isFav: target });
    getApp().setFavorite(self.data.itemId, target).then(function () {
      wx.showToast({ title: target ? '已加入收藏' : '已取消收藏' });
    }).catch(function (err) {
      self.setData({ isFav: !target });
      wx.showModal({
        title: '操作失败',
        content: (err && err.message) || '请稍后重试',
        showCancel: false
      });
    });
  },

  loadSimilar() {
    var self = this;
    getApp().getSimilar(self.data.itemId).then(function (list) {
      self.setData({
        similar: list,
        similarRows: chunkRows(list, COLS)
      });
    }).catch(function () {});
  },

  onSimilarTap(e) {
    var id = e.currentTarget.dataset.id;
    if (!id) return;
    wx.redirectTo({ url: '/pages/detail/detail?id=' + encodeURIComponent(id) });
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
