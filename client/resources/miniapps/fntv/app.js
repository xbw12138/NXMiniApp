// 飞牛影视 · 电视端小程序
// 数据来自飞牛影视自带的 Jellyfin 兼容接口（默认 http://NAS:8005）
var CONFIG_KEY = 'fntv_config';
var PAGE_SIZE = 24;

function buildQuery(params) {
  var parts = [];
  for (var k in params) {
    if (!Object.prototype.hasOwnProperty.call(params, k)) continue;
    var v = params[k];
    if (v === undefined || v === null || v === '') continue;
    parts.push(encodeURIComponent(k) + '=' + encodeURIComponent(String(v)));
  }
  return parts.join('&');
}

function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) rows.push(list.slice(i, i + cols));
  return rows;
}

App({
  globalData: {
    config: null
  },

  ITEM_FIELDS: 'Overview,Genres,ProductionYear,RunTimeTicks,CommunityRating,OfficialRating,Taglines,People,MediaSources,MediaStreams,Studios,UserData',
  DETAIL_FIELDS: 'Overview,Genres,ProductionYear,RunTimeTicks,CommunityRating,OfficialRating,Taglines,People,MediaSources,MediaStreams,Studios,UserData',

  onLaunch() {
    this.loadConfig();
  },

  // ---------- 配置 ----------
  loadConfig() {
    var cfg = null;
    try { cfg = wx.getStorageSync(CONFIG_KEY) || null; } catch (e) { cfg = null; }
    this.globalData.config = cfg;
    return cfg;
  },

  saveConfig(cfg) {
    this.globalData.config = cfg;
    try { wx.setStorageSync(CONFIG_KEY, cfg); } catch (e) {}
  },

  clearConfig() {
    this.globalData.config = null;
    try { wx.removeStorageSync(CONFIG_KEY); } catch (e) {}
  },

  isConfigured() {
    var cfg = this.loadConfig();
    return !!(cfg && cfg.server && cfg.token && cfg.userId);
  },

  getConfig() {
    var cfg = this.loadConfig();
    if (!cfg || !cfg.server || !cfg.token) throw new Error('尚未连接飞牛影视服务器');
    return cfg;
  },

  // 输入 192.168.1.10 → http://192.168.1.10:8005
  // 输入 http://192.168.1.10 → http://192.168.1.10:8005
  // 输入 https://nas.example.com 则原样保留（走 HTTPS 反向代理）
  normalizeServer(raw) {
    var s = String(raw || '').trim();
    if (!s) return '';
    var hadScheme = /^https?:\/\//i.test(s);
    if (!hadScheme) s = 'http://' + s;
    s = s.replace(/\/+$/, '');
    var m = s.match(/^https?:\/\/[^\/]+/i);
    if (!m) return '';
    var origin = m[0];
    var rest = s.slice(origin.length);
    var host = origin.replace(/^https?:\/\//i, '');
    var isHttps = /^https:\/\//i.test(origin);
    if (!/:\d+$/.test(host) && !isHttps) origin += ':8005';
    return origin + rest;
  },

  serverLabel(cfg) {
    var server = (cfg && cfg.server) || '';
    return server.replace(/^https?:\/\//i, '');
  },

  // ---------- HTTP ----------
  authHeaders(cfg) {
    var auth = 'MediaBrowser Client="FeiniuTV", Device="Switch", DeviceId="nxminiapp-fntv", Version="1.0.0"';
    if (cfg && cfg.token) auth += ', Token="' + cfg.token + '"';
    var headers = { 'Accept': 'application/json', 'X-Emby-Authorization': auth };
    if (cfg && cfg.token) headers['X-Emby-Token'] = cfg.token;
    return headers;
  },

  request(cfg, path, opts) {
    var self = this;
    opts = opts || {};
    var query = opts.query ? buildQuery(opts.query) : '';
    var url = cfg.server + path + (query ? (path.indexOf('?') >= 0 ? '&' : '?') + query : '');
    return new Promise(function (resolve, reject) {
      wx.request({
        url: url,
        method: opts.method || 'GET',
        data: opts.data,
        header: self.authHeaders(cfg),
        timeout: 15000,
        success: function (res) {
          var code = res.statusCode || 0;
          if (code >= 200 && code < 300) {
            resolve(res.data);
            return;
          }
          if (code === 401 || code === 403) {
            var e = new Error('登录已失效，请重新登录');
            e.statusCode = code;
            reject(e);
            return;
          }
          reject(new Error('服务器返回 HTTP ' + code));
        },
        fail: function (err) {
          reject(new Error((err && err.errMsg) || '网络请求失败，请检查 NAS 地址与网络'));
        }
      });
    });
  },

  get(path, query) {
    return this.request(this.getConfig(), path, { query: query });
  },

  post(path, data, query) {
    return this.request(this.getConfig(), path, { method: 'POST', data: data, query: query });
  },

  deleteReq(path, query) {
    return this.request(this.getConfig(), path, { method: 'DELETE', query: query });
  },

  // ---------- 登录 ----------
  loginWithPassword(server, username, password) {
    var self = this;
    var cfg = { server: server, token: '', userId: '' };
    return self.request(cfg, '/Users/AuthenticateByName', {
      method: 'POST',
      data: { Username: username, Pw: password }
    }).then(function (body) {
      if (!body || !body.AccessToken || !body.User || !body.User.Id) {
        throw new Error('账号或密码错误');
      }
      return {
        server: server,
        token: body.AccessToken,
        userId: body.User.Id,
        userName: body.User.Name || username,
        authMode: 'password'
      };
    });
  },

  loginWithApiKey(server, apiKey) {
    var self = this;
    var cfg = { server: server, token: apiKey, userId: '' };
    function pickUser(me) {
      return {
        server: server,
        token: apiKey,
        userId: me.Id,
        userName: me.Name || 'API 密钥用户',
        authMode: 'apikey'
      };
    }
    return self.request(cfg, '/Users/Me').then(pickUser).catch(function () {
      return self.request(cfg, '/Users').then(function (list) {
        if (list && list.length > 0 && list[0].Id) return pickUser(list[0]);
        throw new Error('API 密钥无效，或没有可访问的用户');
      });
    });
  },

  // 登录后用媒体库列表做一次连通性验证，避免保存了坏配置。
  verifyConnection(cfg) {
    var self = this;
    return self.request(cfg, '/Users/' + cfg.userId + '/Views').then(function (res) {
      var items = (res && res.Items) || [];
      if (!items.length) throw new Error('连接成功，但没有找到媒体库，请先在飞牛影视中创建媒体库');
      return items;
    });
  },

  // ---------- 图片 ----------
  imageUrl(itemId, kind, width, tag) {
    var cfg = this.loadConfig();
    if (!cfg || !cfg.server || !itemId) return '';
    var path = kind === 'backdrop'
      ? '/Items/' + itemId + '/Images/Backdrop/0'
      : '/Items/' + itemId + '/Images/Primary';
    var q = buildQuery({
      maxWidth: width || 360,
      quality: 90,
      tag: tag || '',
      api_key: cfg.token
    });
    return cfg.server + path + '?' + q;
  },

  // ---------- 数据映射 ----------
  mapItem(it) {
    it = it || {};
    var cfg = this.loadConfig() || {};
    var userData = it.UserData || {};
    var durationSeconds = it.RunTimeTicks ? Math.floor(it.RunTimeTicks / 10000000) : 0;
    var positionSeconds = userData.PlaybackPositionTicks ? Math.floor(userData.PlaybackPositionTicks / 10000000) : 0;
    var progressPercent = durationSeconds > 0 ? Math.min(99, Math.round(positionSeconds / durationSeconds * 100)) : 0;

    var poster = '';
    if (it.ImageTags && it.ImageTags.Primary) {
      poster = this.imageUrl(it.Id, 'primary', 360, it.ImageTags.Primary);
    } else if (it.SeriesId && it.SeriesPrimaryImageTag) {
      poster = this.imageUrl(it.SeriesId, 'primary', 360, it.SeriesPrimaryImageTag);
    }

    var backdrop = '';
    if (it.BackdropImageTags && it.BackdropImageTags.length) {
      backdrop = this.imageUrl(it.Id, 'backdrop', 1280, it.BackdropImageTags[0]);
    } else if (it.ParentBackdropItemId && it.ParentBackdropImageTags && it.ParentBackdropImageTags.length) {
      backdrop = this.imageUrl(it.ParentBackdropItemId, 'backdrop', 1280, it.ParentBackdropImageTags[0]);
    } else {
      backdrop = poster;
    }

    var type = 'movie';
    if (it.Type === 'Series') type = 'series';
    else if (it.Type === 'Episode') type = 'episode';

    var rating = it.CommunityRating || it.OfficialRating || '';
    if (typeof rating === 'number') rating = rating.toFixed(1);

    var meta = [];
    if (it.ProductionYear) meta.push(String(it.ProductionYear));
    if (rating) meta.push(rating + (typeof it.CommunityRating === 'number' ? ' 分' : ''));
    if (durationSeconds > 0) meta.push(Math.max(1, Math.round(durationSeconds / 60)) + ' 分钟');

    return {
      id: it.Id,
      title: it.Name || it.SeriesName || '未命名',
      type: type,
      poster: poster,
      backdrop: backdrop,
      rating: rating,
      year: it.ProductionYear ? String(it.ProductionYear) : '',
      genre: (it.Genres || []).join(' / '),
      duration: durationSeconds > 0 ? Math.max(1, Math.round(durationSeconds / 60)) + ' 分钟' : '',
      durationSeconds: durationSeconds,
      summary: it.Overview || '',
      played: !!userData.Played,
      positionSeconds: positionSeconds,
      progressPercent: progressPercent,
      progressText: progressPercent > 0 ? '已看 ' + progressPercent + '%' : '',
      seriesId: it.SeriesId || '',
      seriesName: it.SeriesName || '',
      seasonNumber: it.ParentIndexNumber || 0,
      episodeNumber: it.IndexNumber || 0,
      metaLine: meta.join(' · '),
      filePath: ''
    };
  },

  // ---------- 首页数据 ----------
  getResume(limit) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Users/' + cfg.userId + '/Items/Resume', {
      MediaTypes: 'Video',
      Limit: limit || 20,
      Fields: this.ITEM_FIELDS,
      ImageTypeLimit: 1,
      EnableImageTypes: 'Primary,Backdrop',
      EnableTotalRecordCount: 'false'
    }).then(function (res) {
      return ((res && res.Items) || []).map(function (it) { return self.mapItem(it); });
    });
  },

  getLatest(limit) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Users/' + cfg.userId + '/Items/Latest', {
      Limit: limit || PAGE_SIZE,
      Fields: this.ITEM_FIELDS,
      ImageTypeLimit: 1,
      EnableImageTypes: 'Primary,Backdrop'
    }).then(function (list) {
      return (list || []).map(function (it) { return self.mapItem(it); });
    });
  },

  getViews() {
    var cfg = this.getConfig();
    return this.get('/Users/' + cfg.userId + '/Views', { Fields: 'PrimaryImageAspectRatio' })
      .then(function (res) {
        return ((res && res.Items) || []).map(function (v) {
          return {
            id: v.Id,
            name: v.Name || '媒体库',
            type: v.CollectionType || '',
            count: v.ChildCount || 0
          };
        });
      });
  },

  getFavorites(limit) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Users/' + cfg.userId + '/Items', {
      Filters: 'IsFavorite',
      Recursive: 'true',
      IncludeItemTypes: 'Movie,Series',
      Fields: this.ITEM_FIELDS,
      ImageTypeLimit: 1,
      EnableImageTypes: 'Primary,Backdrop',
      SortBy: 'DateCreated',
      SortOrder: 'Descending',
      Limit: limit || 20
    }).then(function (res) {
      return ((res && res.Items) || []).map(function (it) { return self.mapItem(it); });
    });
  },

  getLibraryItems(parentId, startIndex, limit, sortBy, sortOrder) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Users/' + cfg.userId + '/Items', {
      ParentId: parentId,
      Recursive: 'true',
      IncludeItemTypes: 'Movie,Series',
      Fields: this.ITEM_FIELDS,
      ImageTypeLimit: 1,
      EnableImageTypes: 'Primary,Backdrop',
      SortBy: sortBy || 'SortName',
      SortOrder: sortOrder || 'Ascending',
      StartIndex: startIndex || 0,
      Limit: limit || PAGE_SIZE,
      EnableTotalRecordCount: 'true'
    }).then(function (res) {
      var items = ((res && res.Items) || []).map(function (it) { return self.mapItem(it); });
      return { items: items, total: (res && res.TotalRecordCount) || items.length };
    });
  },

  getDetail(itemId) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Users/' + cfg.userId + '/Items/' + itemId, {
      Fields: this.DETAIL_FIELDS
    }).then(function (it) {
      var movie = self.mapItem(it);
      var sources = it.MediaSources || [];
      var source = sources.length ? sources[0] : null;
      var streams = (source && source.MediaStreams) || it.MediaStreams || [];
      var subtitleCount = 0;
      var audioCount = 0;
      var audioInfo = '';
      for (var i = 0; i < streams.length; i++) {
        if (streams[i].Type === 'Subtitle') {
          subtitleCount++;
        } else if (streams[i].Type === 'Audio') {
          if (!audioInfo && streams[i].DisplayTitle) audioInfo = streams[i].DisplayTitle;
          audioCount++;
        }
      }
      movie.subtitleCount = subtitleCount;
      movie.audioCount = audioCount;
      movie.isFavorite = !!(it.UserData && it.UserData.IsFavorite);
      movie.filePath = (source && source.Path) || '';
      movie.people = (it.People || []).map(function (p) { return p.Name; });
      movie.director = (it.People || []).filter(function (p) {
        return p.Type === 'Director';
      }).map(function (p) { return p.Name; }).join(' / ');
      movie.actors = (it.People || []).filter(function (p) {
        return p.Type === 'Actor';
      }).map(function (p) { return p.Name; }).slice(0, 8).join(' / ');
      movie.streamsText = [];
      if (audioInfo) movie.streamsText.push(audioInfo);
      if (audioCount > 0) movie.streamsText.push(audioCount + ' 条音轨');
      if (subtitleCount > 0) movie.streamsText.push(subtitleCount + ' 条字幕');
      movie.streamsText = movie.streamsText.join(' · ');
      return movie;
    });
  },

  getSeasons(seriesId) {
    var cfg = this.getConfig();
    return this.get('/Shows/' + seriesId + '/Seasons', {
      UserId: cfg.userId,
      Fields: 'Overview,ProductionYear',
      EnableImages: 'false'
    }).then(function (res) {
      return ((res && res.Items) || []).map(function (s) {
        return {
          id: s.Id,
          name: s.Name || ('第 ' + (s.IndexNumber || 0) + ' 季'),
          index: s.IndexNumber || 0
        };
      });
    });
  },

  getEpisodes(seriesId, seasonId) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Shows/' + seriesId + '/Episodes', {
      UserId: cfg.userId,
      SeasonId: seasonId,
      Fields: 'Overview,RunTimeTicks,UserData',
      EnableImageTypes: 'Primary',
      ImageTypeLimit: 1,
      SortBy: 'ParentIndexNumber,IndexNumber',
      SortOrder: 'Ascending'
    }).then(function (res) {
      return ((res && res.Items) || []).map(function (it) {
        var ep = self.mapItem(it);
        ep.label = 'S' + (it.ParentIndexNumber || 0) + 'E' + (it.IndexNumber || 0);
        ep.name = it.Name || ep.label;
        return ep;
      });
    });
  },

  getSimilar(itemId) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Items/' + itemId + '/Similar', {
      UserId: cfg.userId,
      Limit: 12,
      Fields: this.ITEM_FIELDS,
      ImageTypeLimit: 1,
      EnableImageTypes: 'Primary,Backdrop'
    }).then(function (res) {
      return ((res && res.Items) || []).map(function (it) { return self.mapItem(it); });
    });
  },

  search(keyword) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Users/' + cfg.userId + '/Items', {
      SearchTerm: keyword,
      Recursive: 'true',
      IncludeItemTypes: 'Movie,Series',
      Fields: this.ITEM_FIELDS,
      ImageTypeLimit: 1,
      EnableImageTypes: 'Primary,Backdrop',
      Limit: 40
    }).then(function (res) {
      return ((res && res.Items) || []).map(function (it) { return self.mapItem(it); });
    });
  },

  // ---------- 收藏 / 播放 ----------
  isFavorite(itemId) {
    var cfg = this.getConfig();
    var self = this;
    return this.get('/Users/' + cfg.userId + '/Items/' + itemId, { Fields: 'UserData' })
      .then(function (it) { return !!(it && it.UserData && it.UserData.IsFavorite); });
  },

  setFavorite(itemId, fav) {
    var cfg = this.getConfig();
    var path = '/Users/' + cfg.userId + '/FavoriteItems/' + itemId;
    return fav ? this.post(path, {}) : this.deleteReq(path);
  },

  getPlayUrl(itemId) {
    var cfg = this.getConfig();
    var self = this;
    var directUrl = cfg.server + '/Videos/' + itemId + '/stream?Static=true' +
      '&MediaSourceId=' + encodeURIComponent(itemId) +
      '&DeviceId=nxminiapp-fntv' +
      '&api_key=' + encodeURIComponent(cfg.token);
    return this.post('/Items/' + itemId + '/PlaybackInfo', {
      UserId: cfg.userId,
      AutoOpenLiveStream: true,
      MaxStreamingBitrate: 140000000
    }, { UserId: cfg.userId }).then(function (body) {
      var sources = (body && body.MediaSources) || [];
      for (var i = 0; i < sources.length; i++) {
        var ms = sources[i];
        if (ms.TranscodingUrl) {
          var t = ms.TranscodingUrl;
          return cfg.server + t + (t.indexOf('?') >= 0 ? '&' : '?') +
            'api_key=' + encodeURIComponent(cfg.token);
        }
        if (ms.SupportsDirectPlay || ms.SupportsDirectStream) {
          return cfg.server + '/Videos/' + itemId + '/stream?Static=true' +
            '&MediaSourceId=' + encodeURIComponent(ms.Id || itemId) +
            '&DeviceId=nxminiapp-fntv' +
            '&api_key=' + encodeURIComponent(cfg.token);
        }
      }
      return directUrl;
    }).catch(function () {
      // 部分服务端未实现 PlaybackInfo，直接按静态流播放。
      return directUrl;
    });
  },

  reportPlayback(event, itemId, positionSeconds) {
    var cfg = this.loadConfig();
    if (!cfg || !cfg.userId || !itemId) return;
    var path = event === 'start' ? '/Sessions/Playing'
      : event === 'progress' ? '/Sessions/Playing/Progress'
      : '/Sessions/Playing/Stopped';
    var body = {
      ItemId: itemId,
      PositionTicks: Math.max(0, Math.floor(positionSeconds || 0)) * 10000000,
      CanSeek: true,
      IsPaused: event === 'stop',
      IsMuted: false,
      PlayMethod: 'DirectPlay'
    };
    try {
      this.post(path, body).catch(function () {});
    } catch (e) {}
  },

  markPlayed(itemId) {
    var cfg = this.loadConfig();
    if (!cfg || !cfg.userId || !itemId) return;
    try {
      this.post('/Users/' + cfg.userId + '/PlayedItems/' + itemId, {}).catch(function () {});
    } catch (e) {}
  },

  // ---------- 工具 ----------
  chunkRows(list, cols) { return chunkRows(list, cols); },

  pageSize() { return PAGE_SIZE; },

  fmtTime(seconds) {
    var s = Math.floor(seconds || 0);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    function p(x) { return x < 10 ? '0' + x : '' + x; }
    return (h > 0 ? h + ':' : '') + p(m) + ':' + p(sec);
  }
});
