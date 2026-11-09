// 网易爆米花 · 小程序入口
// 数据源：Emby / Jellyfin / WebDAV / 阿里云盘 / 百度网盘
// 统一适配接口：listMovies / getDetail / getPlayUrl / search / testConnection
console.log('bmh app loaded: media-library-v3');
App({
  globalData: {
    sources: [],          // 已配置的数据源
    cache: { movies: {} }, // 按 sourceId 缓存的影片列表
    smbSessions: {}       // SMB 会话 id 内存缓存（sourceId -> smbId，不持久化）
  },

  VIDEO_EXT: ['mp4', 'mkv', 'avi', 'mov', 'wmv', 'flv', 'ts', 'm2ts', 'webm', 'mpg', 'mpeg', 'rmvb', 'rm', '3gp', 'm3u8', 'vob', 'iso'],
  SUB_EXT: ['srt', 'ass', 'ssa', 'vtt', 'sub', 'smi'],

  onLaunch() {
    this.loadSources();
  },

  // ---------- 数据源存储管理 ----------
  // 容器的每个页面各自持有独立的 App 实例（各页 QuickJS context 独立），
  // globalData 不会跨页共享，所以始终以本地存储为准，每次读取都重新加载。
  loadSources() {
    try {
      var list = wx.getStorageSync('bmh_sources');
      this.globalData.sources = Array.isArray(list) ? list : [];
    } catch (e) {
      this.globalData.sources = [];
    }
    return this.globalData.sources;
  },

  saveSources() {
    try { wx.setStorageSync('bmh_sources', this.globalData.sources); } catch (e) {}
  },

  getSources() { return this.loadSources(); },

  getEnabledSources() {
    return this.getSources().filter(function (s) { return s.enabled !== false; });
  },

  getSource(id) {
    var list = this.getSources();
    for (var i = 0; i < list.length; i++) {
      if (list[i].id === id) return list[i];
    }
    return null;
  },

  addSource(source) {
    var list = this.getSources();
    source.id = source.id || ('src_' + Date.now());
    source.enabled = source.enabled !== false;
    list.push(source);
    this.saveSources();
    return source;
  },

  updateSource(id, patch) {
    var s = this.getSource(id);
    if (!s) return null;
    for (var k in patch) {
      if (Object.prototype.hasOwnProperty.call(patch, k)) s[k] = patch[k];
    }
    this.saveSources();
    return s;
  },

  removeSource(id) {
    this.globalData.sources = this.getSources().filter(function (s) { return s.id !== id; });
    this.saveSources();
  },

  sourceTypeLabel(type) {
    var map = { emby: 'Emby', jellyfin: 'Jellyfin', webdav: 'WebDAV', aliyun: '阿里云盘', baidu: '百度网盘', smb: 'SMB' };
    return map[type] || type;
  },

  // ---------- 工具函数 ----------
  // Base64 编码（WebDAV Basic Auth 用）
  btoa(str) {
    var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    var out = '', i = 0;
    while (i < str.length) {
      var c1 = str.charCodeAt(i++) & 0xff;
      var c2 = i < str.length ? str.charCodeAt(i++) & 0xff : -1;
      var c3 = i < str.length ? str.charCodeAt(i++) & 0xff : -1;
      out += chars[c1 >> 2];
      out += chars[((c1 & 0x03) << 4) | (c2 >= 0 ? c2 >> 4 : 0)];
      out += c2 >= 0 ? chars[((c2 & 0x0f) << 2) | (c3 >= 0 ? c3 >> 6 : 0)] : '=';
      out += c3 >= 0 ? chars[c3 & 0x3f] : '=';
    }
    return out;
  },

  fmtSize(bytes) {
    var n = Number(bytes) || 0;
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    if (n < 1024 * 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + ' MB';
    return (n / 1024 / 1024 / 1024).toFixed(2) + ' GB';
  },

  fmtTime(seconds) {
    var s = Math.floor(seconds || 0);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    function p(x) { return x < 10 ? '0' + x : '' + x; }
    return (h > 0 ? h + ':' : '') + p(m) + ':' + p(sec);
  },

  extOf(name) {
    var i = (name || '').lastIndexOf('.');
    return i >= 0 ? name.slice(i + 1).toLowerCase() : '';
  },

  isVideo(name) {
    return this.VIDEO_EXT.indexOf(this.extOf(name)) >= 0;
  },

  // 统一的影片对象工厂
  makeMovie(source, itemId, fields) {
    return Object.assign({
      id: source.id + ':' + itemId,
      sourceId: source.id,
      sourceType: source.type,
      sourceName: source.name,
      itemId: itemId,
      title: '',
      poster: '',
      rating: '',
      year: '',
      genre: '',
      duration: '',
      region: '',
      director: '',
      actors: '',
      summary: '',
      episodes: [],
      videoUrl: ''
    }, fields || {});
  },

  // HTTP 请求 Promise 封装
  http(opts) {
    return new Promise(function (resolve, reject) {
      wx.request({
        url: opts.url,
        method: opts.method || 'GET',
        data: opts.data,
        header: opts.header || {},
        success: function (res) { resolve(res); },
        fail: function (err) { reject(err); }
      });
    });
  },

  // 给任意 Promise 加超时兜底：网络请求卡住时不会让聚合永远等待。
  withTimeout(promise, ms, fallback) {
    return new Promise(function (resolve) {
      var finished = false;
      var timer = setTimeout(function () {
        if (finished) return;
        finished = true;
        resolve(fallback);
      }, ms);
      promise.then(function (value) {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        resolve(value);
      }, function () {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        resolve(fallback);
      });
    });
  },

  // 影片列表落盘缓存：首次扫描后保存，下次进入先用缓存秒开，过期再后台刷新。
  moviesCacheKey(id) { return 'bmh_movies_' + id; },

  getMoviesCache(id) {
    try {
      var entry = wx.getStorageSync(this.moviesCacheKey(id));
      if (!entry || !entry.list || entry.list.length === 0) return null;
      entry.stale = (Date.now() - (entry.time || 0)) > 30 * 60000;
      return entry;
    } catch (e) {
      return null;
    }
  },

  saveMoviesCache(id, list) {
    if (!list || !list.length) return;
    try {
      wx.setStorageSync(this.moviesCacheKey(id), { time: Date.now(), list: list });
    } catch (e) {}
  },

  // ---------- 聚合接口 ----------
  // 从所有已启用数据源拉取影片
  aggregateMovies() {
    var self = this;
    var sources = self.getEnabledSources();
    if (sources.length === 0) return Promise.resolve([]);

    var tasks = sources.map(function (s) {
      var started = Date.now();
      var cached = self.getMoviesCache(s.id);
      if (cached) {
        self.globalData.cache.movies[s.id] = cached.list;
        if (!cached.stale) {
          console.log('source ' + s.id + ' ' + s.type + ' -> ' + cached.list.length +
                      ' movies from cache');
          return Promise.resolve(cached.list);
        }
        // 过期缓存：先返回旧列表让页面立刻可用，后台静默刷新并写回。
        self.withTimeout(self.listMovies(s), 20000, []).then(function (list) {
          if (list && list.length) {
            self.saveMoviesCache(s.id, list);
            self.globalData.cache.movies[s.id] = list;
          }
        }).catch(function () {});
        return Promise.resolve(cached.list);
      }
      return self.withTimeout(self.listMovies(s), 10000, []).then(function (list) {
        console.log('source ' + s.id + ' ' + s.type + ' -> ' + (list || []).length +
                    ' movies in ' + (Date.now() - started) + 'ms');
        self.globalData.cache.movies[s.id] = list;
        if (list && list.length) self.saveMoviesCache(s.id, list);
        return list;
      }).catch(function () {
        return []; // 单个源失败不影响其他
      });
    });

    return Promise.all(tasks).then(function (results) {
      var all = [];
      results.forEach(function (list) { all = all.concat(list || []); });
      return all;
    });
  },

  // 跨数据源搜索
  aggregateSearch(keyword) {
    var self = this;
    var sources = self.getEnabledSources();
    if (!keyword || sources.length === 0) return Promise.resolve([]);
    var tasks = sources.map(function (s) {
      return self.withTimeout(self.search(s, keyword), 10000, []).catch(function () { return []; });
    });
    return Promise.all(tasks).then(function (results) {
      var all = [];
      results.forEach(function (list) { all = all.concat(list || []); });
      return all;
    });
  },

  // 根据全局 id 解析出源和源内 id
  parseMovieId(gid) {
    var idx = (gid || '').indexOf(':');
    if (idx < 0) return null;
    var sourceId = gid.slice(0, idx);
    var itemId = gid.slice(idx + 1);
    var source = this.getSource(sourceId);
    if (!source) return null;
    return { source: source, itemId: itemId };
  },

  // 统一入口：根据源类型分发
  listMovies(source) {
    switch (source.type) {
      case 'emby':
      case 'jellyfin': return this.embyListMovies(source);
      case 'webdav': return this.webdavListMovies(source);
      case 'aliyun': return this.aliyunListMovies(source);
      case 'baidu': return this.baiduListMovies(source);
      case 'smb': return this.smbListMovies(source);
      default: return Promise.reject(new Error('不支持的数据源类型：' + source.type));
    }
  },

  getDetail(source, itemId) {
    switch (source.type) {
      case 'emby':
      case 'jellyfin': return this.embyGetDetail(source, itemId);
      case 'webdav': return this.webdavGetDetail(source, itemId);
      case 'aliyun': return this.aliyunGetDetail(source, itemId);
      case 'baidu': return this.baiduGetDetail(source, itemId);
      case 'smb': return this.smbGetDetail(source, itemId);
      default: return Promise.reject(new Error('不支持的数据源类型'));
    }
  },

  getPlayUrl(source, itemId) {
    switch (source.type) {
      case 'emby':
      case 'jellyfin': return this.embyGetPlayUrl(source, itemId);
      case 'webdav': return this.webdavGetPlayUrl(source, itemId);
      case 'aliyun': return this.aliyunGetPlayUrl(source, itemId);
      case 'baidu': return this.baiduGetPlayUrl(source, itemId);
      case 'smb': return this.smbGetPlayUrl(source, itemId);
      default: return Promise.reject(new Error('不支持的数据源类型'));
    }
  },

  search(source, keyword) {
    switch (source.type) {
      case 'emby':
      case 'jellyfin': return this.embySearch(source, keyword);
      case 'webdav': return this.webdavSearch(source, keyword);
      case 'aliyun': return this.aliyunSearch(source, keyword);
      case 'baidu': return this.baiduSearch(source, keyword);
      case 'smb': return this.smbSearch(source, keyword);
      default: return Promise.resolve([]);
    }
  },

  testConnection(source) {
    // 用一次轻量请求验证
    return this.listMovies(source).then(function (list) {
      return { ok: true, count: list.length };
    });
  },

  // =====================================================================
  // Emby / Jellyfin 适配器
  // API 文档：https://swagger.emby.media/
  // =====================================================================
  embyAuthHeader(source) {
    var cfg = source.config || {};
    var token = cfg.token || '';
    var auth = 'MediaBrowser Client="NXMiniApp", Device="Switch", DeviceId="nxminiapp-bmh", Version="1.0.0"';
    if (token) auth += ', Token="' + token + '"';
    return { 'X-Emby-Authorization': auth, 'Accept': 'application/json' };
  },

  embyBaseUrl(source) {
    var url = (source.config && source.config.url) || '';
    return url.replace(/\/+$/, '');
  },

  // 登录（用户名密码）获取 token
  embyLogin(source) {
    var self = this;
    var base = self.embyBaseUrl(source);
    var cfg = source.config || {};
    if (cfg.token) return Promise.resolve();
    if (!cfg.username || !cfg.password) return Promise.resolve();
    return self.http({
      url: base + '/Users/AuthenticateByName',
      method: 'POST',
      header: self.embyAuthHeader(source),
      data: { Username: cfg.username, Pw: cfg.password }
    }).then(function (res) {
      var body = res.data || {};
      if (body.AccessToken && body.User && body.User.Id) {
        cfg.token = body.AccessToken;
        cfg.userId = body.User.Id;
        source.config = cfg;
        self.saveSources();
      }
    });
  },

  embyEnsureUserId(source) {
    var self = this;
    var cfg = source.config || {};
    if (cfg.userId) return Promise.resolve(cfg.userId);
    // 没有 userId 时，用 /Users/Public 或 /Users 取第一个公开用户
    return self.http({
      url: self.embyBaseUrl(source) + '/Users/Public',
      header: self.embyAuthHeader(source)
    }).then(function (res) {
      var users = res.data || [];
      if (users.length > 0 && users[0].Id) {
        cfg.userId = users[0].Id;
        source.config = cfg;
        self.saveSources();
        return cfg.userId;
      }
      throw new Error('无法获取用户 ID');
    });
  },

  embyListMovies(source) {
    var self = this;
    return self.embyLogin(source).then(function () {
      return self.embyEnsureUserId(source);
    }).then(function (userId) {
      var base = self.embyBaseUrl(source);
      var fields = 'Overview,Genres,ProductionYear,Runtime,OfficialRating,ProviderIds,CumulativeRunTimeTicks';
      // 同时取电影和剧集
      return self.http({
        url: base + '/Users/' + userId + '/Items',
        header: self.embyAuthHeader(source),
        data: {
          IncludeItemTypes: 'Movie,Series',
          Recursive: 'true',
          Fields: fields,
          SortBy: 'SortName',
          SortOrder: 'Ascending',
          Limit: 200
        }
      }).then(function (res) {
        var items = (res.data && res.data.Items) || [];
        return items.map(function (it) {
          var isSeries = it.Type === 'Series';
          var poster = it.ImageTags && it.ImageTags.Primary
            ? base + '/Items/' + it.Id + '/Images/Primary?maxWidth=300'
            : '';
          return self.makeMovie(source, it.Id, {
            title: it.Name || '',
            poster: poster,
            rating: it.CommunityRating ? it.CommunityRating.toFixed(1) : '',
            year: it.ProductionYear ? String(it.ProductionYear) : '',
            genre: (it.Genres || []).join(' / '),
            duration: it.RunTimeTicks ? Math.floor(it.RunTimeTicks / 600000000) + ' 分钟' : '',
            summary: it.Overview || '',
            type: isSeries ? 'tv' : 'movie'
          });
        });
      });
    });
  },

  embyGetDetail(source, itemId) {
    var self = this;
    return self.embyLogin(source).then(function () {
      return self.embyEnsureUserId(source);
    }).then(function (userId) {
      var base = self.embyBaseUrl(source);
      return self.http({
        url: base + '/Users/' + userId + '/Items/' + itemId,
        header: self.embyAuthHeader(source)
      }).then(function (res) {
        var it = res.data || {};
        var isSeries = it.Type === 'Series';
        var poster = it.ImageTags && it.ImageTags.Primary
          ? base + '/Items/' + it.Id + '/Images/Primary?maxWidth=500'
          : '';

        // 如果是剧集，拉取季和集
        if (isSeries) {
          return self.http({
            url: base + '/Shows/' + itemId + '/Episodes',
            header: self.embyAuthHeader(source),
            data: { UserId: userId, Fields: 'Overview' }
          }).then(function (res2) {
            var eps = (res2.data && res2.data.Items) || [];
            var episodes = eps.map(function (e) {
              return 'S' + (e.ParentIndexNumber || 0) + 'E' + (e.IndexNumber || 0) + ' ' + (e.Name || '');
            });
            return self.makeMovie(source, itemId, {
              title: it.Name || '',
              poster: poster,
              rating: it.CommunityRating ? it.CommunityRating.toFixed(1) : '',
              year: it.ProductionYear ? String(it.ProductionYear) : '',
              genre: (it.Genres || []).join(' / '),
              duration: '',
              region: '',
              director: '',
              actors: '',
              summary: it.Overview || '',
              episodes: episodes
            });
          });
        }

        return self.makeMovie(source, itemId, {
          title: it.Name || '',
          poster: poster,
          rating: it.CommunityRating ? it.CommunityRating.toFixed(1) : '',
          year: it.ProductionYear ? String(it.ProductionYear) : '',
          genre: (it.Genres || []).join(' / '),
          duration: it.RunTimeTicks ? Math.floor(it.RunTimeTicks / 600000000) + ' 分钟' : '',
          summary: it.Overview || ''
        });
      });
    });
  },

  embyGetPlayUrl(source, itemId) {
    var self = this;
    return self.embyLogin(source).then(function () {
      return self.embyEnsureUserId(source);
    }).then(function (userId) {
      var base = self.embyBaseUrl(source);
      return self.http({
        url: base + '/Items/' + itemId + '/PlaybackInfo',
        header: self.embyAuthHeader(source),
        data: {
          UserId: userId,
          AutoOpenLiveStream: 'true',
          MaxStreamingBitrate: 140000000
        }
      }).then(function (res) {
        var body = res.data || {};
        var mediaSources = body.MediaSources || [];
        // 优先直链，其次转码
        for (var i = 0; i < mediaSources.length; i++) {
          var ms = mediaSources[i];
          if (ms.SupportsDirectPlay && ms.Path) {
            // 直链需要走 /Videos/{id}/stream?Static=true
            return base + '/Videos/' + itemId + '/stream?Static=true&mediaSourceId=' + ms.Id + '&DeviceId=nxminiapp-bmh&api_key=' + (source.config.token || '');
          }
        }
        // 转码
        if (mediaSources.length > 0) {
          var ms0 = mediaSources[0];
          if (ms0.TranscodingUrl) {
            return base + ms0.TranscodingUrl + (ms0.TranscodingUrl.indexOf('?') >= 0 ? '&' : '?') + 'api_key=' + (source.config.token || '');
          }
        }
        throw new Error('未获取到播放地址');
      });
    });
  },

  embySearch(source, keyword) {
    var self = this;
    return self.embyLogin(source).then(function () {
      return self.embyEnsureUserId(source);
    }).then(function (userId) {
      var base = self.embyBaseUrl(source);
      return self.http({
        url: base + '/Users/' + userId + '/Items',
        header: self.embyAuthHeader(source),
        data: {
          SearchTerm: keyword,
          IncludeItemTypes: 'Movie,Series',
          Recursive: 'true',
          Limit: 30
        }
      }).then(function (res) {
        var items = (res.data && res.data.Items) || [];
        return items.map(function (it) {
          var poster = it.ImageTags && it.ImageTags.Primary
            ? base + '/Items/' + it.Id + '/Images/Primary?maxWidth=200'
            : '';
          return self.makeMovie(source, it.Id, {
            title: it.Name || '',
            poster: poster,
            rating: it.CommunityRating ? it.CommunityRating.toFixed(1) : '',
            year: it.ProductionYear ? String(it.ProductionYear) : '',
            genre: (it.Genres || []).join(' / '),
            type: it.Type === 'Series' ? 'tv' : 'movie'
          });
        });
      });
    });
  },

  // =====================================================================
  // WebDAV 适配器
  // 用 PROPFIND 列目录，Basic Auth 认证
  // =====================================================================
  webdavConfig(source) {
    var cfg = source.config || {};
    return {
      url: (cfg.url || '').replace(/\/+$/, ''),
      username: cfg.username || '',
      password: cfg.password || '',
      rootPath: cfg.rootPath || '/'
    };
  },

  webdavHeaders(source) {
    var cfg = this.webdavConfig(source);
    var headers = { 'Depth': '1' };
    if (cfg.username) {
      headers['Authorization'] = 'Basic ' + this.btoa(cfg.username + ':' + cfg.password);
    }
    return headers;
  },

  // 解析 PROPFIND XML 响应（QuickJS 无 DOMParser，用正则）
  webdavParseList(xml) {
    var items = [];
    // 匹配每个 <D:response>...</D:response>
    var respRe = /<[^:>]*:response>([\s\S]*?)<\/[^:>]*:response>/g;
    var m;
    while ((m = respRe.exec(xml)) !== null) {
      var block = m[1];
      // href
      var hrefM = block.match(/<[^:>]*:href>([^<]+)<\/[^:>]*:href>/);
      if (!hrefM) continue;
      var href = decodeURIComponent(hrefM[1].trim());
      // 跳过根目录自身
      if (href === '' || href === '/') continue;
      // collection 标记
      var isDir = /<[^:>]*:collection\s*\/>/.test(block);
      // displayname
      var nameM = block.match(/<[^:>]*:displayname>([^<]+)<\/[^:>]*:displayname>/);
      var name = nameM ? decodeURIComponent(nameM[1].trim()) : '';
      if (!name) {
        var parts = href.replace(/\/$/, '').split('/');
        name = parts[parts.length - 1];
      }
      // content-length
      var lenM = block.match(/<[^:>]*:getcontentlength>(\d+)<\/[^:>]*:getcontentlength>/);
      var size = lenM ? Number(lenM[1]) : 0;
      items.push({ href: href, name: name, isDir: isDir, size: size });
    }
    return items;
  },

  // 递归扫描所有视频文件
  webdavScanDir(source, dirPath, depth) {
    var self = this;
    if (depth > 5) return Promise.resolve([]); // 限制递归深度
    var cfg = self.webdavConfig(source);
    var url = cfg.url + encodeURI(dirPath);
    return self.http({
      url: url,
      method: 'PROPFIND',
      header: self.webdavHeaders(source)
    }).then(function (res) {
      var items = self.webdavParseList(res.data || '');
      var dirs = [];
      var files = [];
      items.forEach(function (it) {
        // PROPFIND 返回的第一个通常是目录自身，跳过
        if (it.href === dirPath || it.href === dirPath + '/') return;
        if (it.isDir) dirs.push(it);
        else if (self.isVideo(it.name)) files.push(it);
      });

      // 递归子目录
      var subTasks = dirs.map(function (d) {
        return self.webdavScanDir(source, d.href, depth + 1).catch(function () { return []; });
      });

      return Promise.all(subTasks).then(function (subResults) {
        var all = files.slice();
        subResults.forEach(function (list) { all = all.concat(list); });
        return all;
      });
    });
  },

  webdavListMovies(source) {
    var self = this;
    var cfg = self.webdavConfig(source);
    return self.webdavScanDir(source, cfg.rootPath, 0).then(function (files) {
      return files.map(function (f) {
        // WebDAV 没有刮削信息，用文件名作为标题
        var name = f.name.replace(/\.[^.]+$/, '');
        return self.makeMovie(source, f.href, {
          title: name,
          poster: '',
          year: '',
          genre: '',
          duration: self.fmtSize(f.size),
          summary: 'WebDAV 文件：' + f.href,
          videoUrl: cfg.url + encodeURI(f.href),
          type: 'movie'
        });
      });
    });
  },

  webdavGetDetail(source, itemId) {
    var cfg = this.webdavConfig(source);
    var name = decodeURIComponent(itemId).split('/').pop().replace(/\.[^.]+$/, '');
    return Promise.resolve(this.makeMovie(source, itemId, {
      title: name,
      poster: '',
      summary: 'WebDAV 文件路径：' + itemId,
      videoUrl: cfg.url + encodeURI(itemId),
      type: 'movie'
    }));
  },

  webdavGetPlayUrl(source, itemId) {
    var cfg = this.webdavConfig(source);
    var url = cfg.url + encodeURI(itemId);
    // 带 Basic Auth 的播放地址（video 组件的 http-headers）
    return Promise.resolve({
      url: url,
      headers: cfg.username ? 'Authorization: Basic ' + this.btoa(cfg.username + ':' + cfg.password) : ''
    });
  },

  webdavSearch(source, keyword) {
    var self = this;
    return self.webdavListMovies(source).then(function (list) {
      var kw = keyword.toLowerCase();
      return list.filter(function (m) {
        return m.title.toLowerCase().indexOf(kw) >= 0;
      });
    });
  },

  // =====================================================================
  // 阿里云盘适配器
  // API: https://api.aliyundrive.com
  // 认证：refresh_token → access_token
  // =====================================================================
  aliyunEnsureToken(source) {
    var self = this;
    var cfg = source.config || {};
    // 如果有 access_token 且未过期，直接用
    var now = Date.now();
    if (cfg.accessToken && cfg.expireAt && cfg.expireAt > now) {
      return Promise.resolve(cfg.accessToken);
    }
    if (!cfg.refreshToken) return Promise.reject(new Error('缺少 refresh_token'));
    return self.http({
      url: 'https://api.aliyundrive.com/v2/account/token',
      method: 'POST',
      header: { 'Content-Type': 'application/json' },
      data: {
        grant_type: 'refresh_token',
        refresh_token: cfg.refreshToken
      }
    }).then(function (res) {
      var body = res.data || {};
      if (!body.access_token) throw new Error(body.message || 'token 刷新失败');
      cfg.accessToken = body.access_token;
      cfg.refreshToken = body.refresh_token || cfg.refreshToken;
      cfg.expireAt = now + (body.expires_in || 7200) * 1000 - 60000;
      cfg.driveId = body.default_drive_id || cfg.driveId;
      source.config = cfg;
      self.saveSources();
      return cfg.accessToken;
    });
  },

  aliyunHeaders(source, token) {
    return {
      'Authorization': 'Bearer ' + token,
      'Content-Type': 'application/json'
    };
  },

  // 递归扫描视频文件
  aliyunScanDir(source, token, parentId, depth) {
    var self = this;
    if (depth > 6) return Promise.resolve([]);
    var cfg = source.config || {};
    return self.http({
      url: 'https://api.aliyundrive.com/adrive/v1/file/list',
      method: 'POST',
      header: self.aliyunHeaders(source, token),
      data: {
        drive_id: cfg.driveId,
        parent_file_id: parentId || 'root',
        limit: 100
      }
    }).then(function (res) {
      var body = res.data || {};
      var items = body.items || [];
      var dirs = [];
      var files = [];
      items.forEach(function (it) {
        if (it.type === 'folder') dirs.push(it);
        else if (self.isVideo(it.name)) files.push(it);
      });
      var subTasks = dirs.map(function (d) {
        return self.aliyunScanDir(source, token, d.file_id, depth + 1).catch(function () { return []; });
      });
      return Promise.all(subTasks).then(function (subResults) {
        var all = files.slice();
        subResults.forEach(function (list) { all = all.concat(list); });
        return all;
      });
    });
  },

  aliyunListMovies(source) {
    var self = this;
    return self.aliyunEnsureToken(source).then(function (token) {
      return self.aliyunScanDir(source, token, 'root', 0);
    }).then(function (files) {
      return files.map(function (f) {
        var name = (f.name || '').replace(/\.[^.]+$/, '');
        return self.makeMovie(source, f.file_id, {
          title: name,
          poster: '',
          year: '',
          genre: '',
          duration: self.fmtSize(f.size),
          summary: '阿里云盘文件',
          type: 'movie'
        });
      });
    });
  },

  aliyunGetDetail(source, itemId) {
    var self = this;
    return self.aliyunEnsureToken(source).then(function (token) {
      var cfg = source.config || {};
      return self.http({
        url: 'https://api.aliyundrive.com/adrive/v1/file/get',
        method: 'POST',
        header: self.aliyunHeaders(source, token),
        data: { drive_id: cfg.driveId, file_id: itemId }
      }).then(function (res) {
        var f = res.data || {};
        var name = (f.name || '').replace(/\.[^.]+$/, '');
        return self.makeMovie(source, itemId, {
          title: name,
          poster: '',
          duration: self.fmtSize(f.size),
          summary: '阿里云盘文件，大小：' + self.fmtSize(f.size),
          type: 'movie'
        });
      });
    });
  },

  aliyunGetPlayUrl(source, itemId) {
    var self = this;
    return self.aliyunEnsureToken(source).then(function (token) {
      var cfg = source.config || {};
      return self.http({
        url: 'https://api.aliyundrive.com/v2/file/get_video_preview_play_info',
        method: 'POST',
        header: self.aliyunHeaders(source, token),
        data: {
          drive_id: cfg.driveId,
          file_id: itemId,
          category: 'live_transcoding'
        }
      }).then(function (res) {
        var body = res.data || {};
        var tasks = body.live_transcoding_task_list || [];
        // 优先选高清
        var sorted = tasks.slice().sort(function (a, b) {
          var ta = parseInt(a.template_id || '0', 10) || 0;
          var tb = parseInt(b.template_id || '0', 10) || 0;
          return tb - ta;
        });
        if (sorted.length > 0 && sorted[0].url) return sorted[0].url;
        // 退化为下载直链
        return self.http({
          url: 'https://api.aliyundrive.com/v2/file/get_download_url',
          method: 'POST',
          header: self.aliyunHeaders(source, token),
          data: { drive_id: cfg.driveId, file_id: itemId }
        }).then(function (res2) {
          var url = res2.data && res2.data.url;
          if (!url) throw new Error('未获取到播放地址');
          return url;
        });
      });
    });
  },

  aliyunSearch(source, keyword) {
    var self = this;
    return self.aliyunEnsureToken(source).then(function (token) {
      var cfg = source.config || {};
      return self.http({
        url: 'https://api.aliyundrive.com/adrive/v3/file/search',
        method: 'POST',
        header: self.aliyunHeaders(source, token),
        data: {
          drive_id: cfg.driveId,
          query: 'name contains "' + keyword + '"',
          limit: 30
        }
      }).then(function (res) {
        var items = (res.data && res.data.items) || [];
        return items.filter(function (it) { return self.isVideo(it.name); }).map(function (f) {
          var name = (f.name || '').replace(/\.[^.]+$/, '');
          return self.makeMovie(source, f.file_id, {
            title: name,
            duration: self.fmtSize(f.size),
            type: 'movie'
          });
        });
      });
    });
  },

  // =====================================================================
  // 百度网盘适配器
  // 用 access_token 调用 OpenAPI
  // =====================================================================
  baiduConfig(source) {
    return source.config || {};
  },

  baiduScanDir(source, dir, depth) {
    var self = this;
    if (depth > 6) return Promise.resolve([]);
    var cfg = self.baiduConfig(source);
    return self.http({
      url: 'https://pan.baidu.com/rest/2.0/xpan/file?method=list&access_token=' + cfg.accessToken + '&dir=' + encodeURIComponent(dir) + '&limit=1000',
      header: { 'User-Agent': 'pan.baidu.com' }
    }).then(function (res) {
      var body = res.data || {};
      if (body.errno && body.errno !== 0) {
        throw new Error('百度网盘错误：' + (body.errmsg || body.errno));
      }
      var items = body.list || [];
      var dirs = [];
      var files = [];
      items.forEach(function (it) {
        if (it.isdir === 1) dirs.push(it);
        else if (self.isVideo(it.server_filename || it.path)) files.push(it);
      });
      var subTasks = dirs.map(function (d) {
        return self.baiduScanDir(source, d.path, depth + 1).catch(function () { return []; });
      });
      return Promise.all(subTasks).then(function (subResults) {
        var all = files.slice();
        subResults.forEach(function (list) { all = all.concat(list); });
        return all;
      });
    });
  },

  baiduListMovies(source) {
    var self = this;
    return self.baiduScanDir(source, '/', 0).then(function (files) {
      return files.map(function (f) {
        var name = (f.server_filename || '').replace(/\.[^.]+$/, '');
        return self.makeMovie(source, f.fs_id, {
          title: name,
          poster: '',
          year: '',
          genre: '',
          duration: self.fmtSize(f.size),
          summary: '百度网盘文件，路径：' + f.path,
          type: 'movie'
        });
      });
    });
  },

  baiduGetDetail(source, itemId) {
    var self = this;
    var cfg = self.baiduConfig(source);
    return self.http({
      url: 'https://pan.baidu.com/rest/2.0/xpan/multimedia?method=filemetas&access_token=' + cfg.accessToken + '&fsids=[' + itemId + ']&dlink=1',
      header: { 'User-Agent': 'pan.baidu.com' }
    }).then(function (res) {
      var body = res.data || {};
      var info = (body.list && body.list[0]) || {};
      var name = (info.server_filename || '').replace(/\.[^.]+$/, '');
      return self.makeMovie(source, itemId, {
        title: name,
        duration: self.fmtSize(info.size),
        summary: '百度网盘文件，大小：' + self.fmtSize(info.size),
        type: 'movie'
      });
    });
  },

  baiduGetPlayUrl(source, itemId) {
    var self = this;
    var cfg = self.baiduConfig(source);
    return self.http({
      url: 'https://pan.baidu.com/rest/2.0/xpan/multimedia?method=filemetas&access_token=' + cfg.accessToken + '&fsids=[' + itemId + ']&dlink=1',
      header: { 'User-Agent': 'pan.baidu.com' }
    }).then(function (res) {
      var body = res.data || {};
      var info = (body.list && body.list[0]) || {};
      if (!info.dlink) throw new Error('未获取到下载链接');
      // dlink 需要带 access_token 才能访问
      return info.dlink + '&access_token=' + cfg.accessToken;
    });
  },

  baiduSearch(source, keyword) {
    var self = this;
    return self.baiduListMovies(source).then(function (list) {
      var kw = keyword.toLowerCase();
      return list.filter(function (m) {
        return m.title.toLowerCase().indexOf(kw) >= 0;
      });
    });
  },

  // =====================================================================
  // SMB 适配器
  // 通过容器内置的 wx.smb.* 能力访问 SMB 共享。
  // =====================================================================
  smbConfig(source) {
    return source.config || {};
  },

  // 建立连接并缓存会话 id（不持久化，会话随容器进程存活）。
  smbEnsureId(source) {
    var self = this;
    var cached = self.globalData.smbSessions[source.id];
    if (cached) return Promise.resolve(cached);
    var cfg = self.smbConfig(source);
    return new Promise(function (resolve, reject) {
      wx.smb.connect({
        server: cfg.server, share: cfg.share, user: cfg.user,
        password: cfg.password, domain: cfg.domain,
        success: function (res) {
          self.globalData.smbSessions[source.id] = res.id;
          resolve(res.id);
        },
        fail: function (err) {
          reject(new Error((err && err.errMsg) || 'SMB 连接失败'));
        }
      });
    });
  },

  // 递归扫描目录，返回视频文件完整路径列表（[{name, size}]）。
  // 串行遍历子目录：容器每个 listFiles 都新建线程，并发递归会瞬间堆积大量
  // 线程并抢占 SMB 会话锁，导致界面卡死。
  // 同时限制目录数与影片数：整盘递归 + 上千卡片渲染会把页面拖死。
  smbScanDir(smbId, path, depth, budget) {
    var self = this;
    budget = budget || { dirs: 20, files: 10 };
    if (depth > 4 || budget.dirs <= 0 || budget.files <= 0) return Promise.resolve([]);
    console.log('smb scan depth=' + depth + ' dirs=' + budget.dirs + ' files=' + budget.files +
                ' path=' + path);
    return new Promise(function (resolve) {
      wx.smb.listFiles({
        id: smbId, path: path,
        success: function (list) {
          var files = [];
          var dirs = [];
          (list || []).forEach(function (e) {
            if (e.isDir) {
              if (!/\/(\$RECYCLE|System Volume Information)/i.test(e.name)) dirs.push(e.name);
            } else if (self.isVideo(e.name)) {
              files.push(e);
            }
          });
          if (files.length > budget.files) files = files.slice(0, budget.files);
          budget.files -= files.length;
          var acc = files;
          var chain = Promise.resolve();
          dirs.forEach(function (d) {
            chain = chain.then(function () {
              if (budget.dirs <= 0 || budget.files <= 0) return;
              budget.dirs--;
              return self.smbScanDir(smbId, d, depth + 1, budget).then(function (sub) {
                acc = acc.concat(sub);
              });
            });
          });
          chain.then(function () { resolve(acc); }).catch(function () { resolve(acc); });
        },
        fail: function () { resolve([]); }
      });
    });
  },

  // 一次性递归扫描（容器原生完成遍历，只回一次结果），比逐目录往返快很多。
  smbScanNative(smbId, path, maxDepth, maxEntries) {
    var self = this;
    return new Promise(function (resolve) {
      var started = Date.now();
      wx.smb.scan({
        id: smbId, path: path, maxDepth: maxDepth, maxEntries: maxEntries,
        success: function (list) {
          var files = (list || []).filter(function (e) {
            return !e.isDir && self.isVideo(e.name);
          });
          if (files.length > maxEntries) files = files.slice(0, maxEntries);
          console.log('smb scan native: ' + files.length + ' files in ' +
                      (Date.now() - started) + 'ms');
          resolve(files);
        },
        fail: function () { resolve([]); }
      });
    });
  },

  // 原生媒体库：读本地索引（秒出，不需要 SMB 连接）。
  smbLibraryPage(source, offset, limit) {
    return new Promise(function (resolve) {
      wx.media.library({
        sourceId: source.id, offset: offset, limit: limit,
        success: function (res) { resolve(res || { total: 0, list: [] }); },
        fail: function () { resolve({ total: 0, list: [] }); }
      });
    });
  },

  // 原生媒体库：增量扫描（目录 mtime 未变化直接复用旧索引），结果落盘。
  smbNativeScan(source, maxDepth, maxFiles) {
    var self = this;
    return self.smbEnsureId(source).then(function (smbId) {
      return new Promise(function (resolve) {
        var started = Date.now();
        wx.media.scan({
          sourceId: source.id, id: smbId, root: '/',
          maxDepth: maxDepth || 4, maxFiles: maxFiles || 2000,
          success: function (res) {
            console.log('media scan source=' + source.id + ' total=' +
                        ((res && res.total) || 0) + ' in ' + (Date.now() - started) + 'ms');
            resolve((res && res.total) || 0);
          },
          fail: function (err) {
            console.log('media scan source=' + source.id + ' failed: ' +
                        ((err && err.errMsg) || 'unknown'));
            resolve(0);
          }
        });
      });
    }).catch(function () { return 0; });
  },

  // 原生媒体库路径：有索引立刻返回并后台增量刷新；
  // 无索引也立刻返回空列表（首页先可用），扫描在后台完成后再写入缓存。
  smbLibraryMovies(source) {
    var self = this;
    console.log('media library read source=' + source.id);
    return self.withTimeout(self.smbLibraryPage(source, 0, 0), 3000, { total: 0, list: [] })
      .then(function (res) {
        if (res.list && res.list.length) {
          console.log('media library hit source=' + source.id + ' total=' + res.list.length);
          self.smbNativeScan(source).catch(function () {});
          return res.list;
        }
        console.log('media library miss source=' + source.id + ', start background scan');
        self.smbNativeScan(source).then(function () {
          return self.smbLibraryPage(source, 0, 0);
        }).then(function (page) {
          var list = self.smbMapMovies(source, page.list || []);
          console.log('media library built source=' + source.id + ' total=' + list.length);
          // 写入影片列表缓存，下次进入直接命中，不必再等索引。
          if (list.length) self.saveMoviesCache(source.id, list);
        }).catch(function (e) {
          console.log('media library scan error: ' + ((e && e.message) || e));
        });
        return [];
      }).then(function (files) {
        return self.smbMapMovies(source, files);
      });
  },

  smbMapMovies(source, files) {
    var self = this;
    return (files || []).map(function (f) {
      var name = f.name.split('/').pop().replace(/\.[^.]+$/, '');
      return self.makeMovie(source, f.name, {
        title: name,
        poster: '',
        year: '',
        genre: '',
        duration: self.fmtSize(f.size),
        summary: 'SMB 文件：' + f.name,
        type: 'movie'
      });
    });
  },

  smbListMovies(source) {
    var self = this;
    if (wx.media && typeof wx.media.library === 'function' &&
        typeof wx.media.scan === 'function') {
      return self.smbLibraryMovies(source);
    }
    console.log('wx.media 不可用，回退 wx.smb.scan');
    return self.smbEnsureId(source).then(function (smbId) {
      if (typeof wx.smb.scan === 'function') {
        return self.smbScanNative(smbId, '/', 4, 300);
      }
      return self.smbScanDir(smbId, '/', 0);
    }).then(function (files) {
      return self.smbMapMovies(source, files);
    });
  },

  smbGetDetail(source, itemId) {
    var self = this;
    var name = decodeURIComponent(itemId).split('/').pop().replace(/\.[^.]+$/, '');
    return Promise.resolve(self.makeMovie(source, itemId, {
      title: name,
      poster: '',
      summary: 'SMB 文件路径：' + itemId,
      type: 'movie'
    }));
  },

  smbGetPlayUrl(source, itemId) {
    var self = this;
    return self.smbEnsureId(source).then(function (smbId) {
      var url = wx.smb.streamUrl({ id: smbId, path: itemId });
      if (!url) throw new Error('生成播放地址失败');
      return url;
    });
  },

  smbSearch(source, keyword) {
    var self = this;
    return self.smbListMovies(source).then(function (list) {
      var kw = keyword.toLowerCase();
      return list.filter(function (m) {
        return m.title.toLowerCase().indexOf(kw) >= 0;
      });
    });
  },

  // ---------- 本地存储：收藏 & 最近播放 ----------
  getFavorites() {
    try { return wx.getStorageSync('bmh_fav') || []; } catch (e) { return []; }
  },

  isFavorite(id) {
    return this.getFavorites().indexOf(id) >= 0;
  },

  toggleFavorite(id) {
    var list = this.getFavorites();
    var i = list.indexOf(id);
    if (i >= 0) list.splice(i, 1);
    else list.unshift(id);
    try { wx.setStorageSync('bmh_fav', list); } catch (e) {}
    return i < 0;
  },

  getRecent() {
    try { return wx.getStorageSync('bmh_recent') || []; } catch (e) { return []; }
  },

  addRecent(id) {
    var list = this.getRecent();
    var i = list.indexOf(id);
    if (i >= 0) list.splice(i, 1);
    list.unshift(id);
    if (list.length > 20) list = list.slice(0, 20);
    try { wx.setStorageSync('bmh_recent', list); } catch (e) {}
  },

  getRecentProgress() {
    try { return wx.getStorageSync('bmh_progress') || {}; } catch (e) { return {}; }
  },

  saveProgress(id, seconds) {
    if (!id || !seconds) return;
    var map = this.getRecentProgress();
    map[id] = { seconds: Math.floor(seconds), at: Date.now() };
    try { wx.setStorageSync('bmh_progress', map); } catch (e) {}
  },

  getProgress(id) {
    var map = this.getRecentProgress();
    return map[id] || null;
  }
});
