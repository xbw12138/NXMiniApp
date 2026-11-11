// 网易爆米花 · 小程序入口
// 数据源：Emby / Jellyfin / WebDAV / 阿里云盘 / 百度网盘
// 统一适配接口：listMovies / getDetail / getPlayUrl / search / testConnection
console.log('bmh app loaded: media-library-v3');
App({
  globalData: {
    sources: [],          // 已配置的数据源
    cache: { movies: {} }, // 按 sourceId 缓存的影片列表
    smbSessions: {},      // SMB 会话 id 内存缓存（sourceId -> smbId，不持久化）
    smbScanAt: {},        // 上次扫描时间，防止每次刷新都重新扫盘
    smbScanning: {},      // 扫描中的源，避免重复触发
    smbUpdatedAt: 0       // 扫描完成时间戳，页面据此自动刷新
  },

  // 影片列表落盘上限：SMB 索引可能有上千条，全量写 storage 会阻塞 JS 线程。
  MOVIE_CACHE_LIMIT: 300,

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

  // Base64 解码（阿里云扫码返回的 bizExt 是 base64 JSON）
  atob(str) {
    var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    var s = String(str || '').replace(/[\s=]/g, '').replace(/-/g, '+').replace(/_/g, '/');
    var out = '', i = 0;
    while (i < s.length) {
      var c1 = chars.indexOf(s.charAt(i++));
      var c2 = chars.indexOf(s.charAt(i++));
      var c3 = chars.indexOf(s.charAt(i++));
      var c4 = chars.indexOf(s.charAt(i++));
      if (c1 < 0 || c2 < 0) break;
      out += String.fromCharCode((c1 << 2) | (c2 >> 4));
      if (c3 >= 0) out += String.fromCharCode(((c2 & 15) << 4) | (c3 >> 2));
      if (c4 >= 0) out += String.fromCharCode(((c3 & 3) << 6) | c4);
    }
    return out;
  },

  // ---------- 纯 JS 二维码编码器（byte 模式 / 纠错 L / 版本 1-10，参考 qrcode-generator） ----------
  qrMatrix(text) {
    var RS_BLOCKS_L = [
      [1, 26, 19], [1, 44, 34], [1, 70, 55], [1, 100, 80], [1, 134, 108],
      [2, 86, 68], [2, 98, 78], [2, 121, 97], [2, 146, 116], [2, 86, 68, 2, 87, 69]
    ];
    var ALIGN_POS = [
      [], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34],
      [6, 22, 38], [6, 24, 42], [6, 26, 46], [6, 28, 50]
    ];
    var G15 = 0x0537, G18 = 0x1f25, G15_MASK = 0x5412;

    var EXP = [], LOG = [];
    (function () {
      var x = 1;
      for (var i = 0; i < 255; i++) {
        EXP[i] = x;
        LOG[x] = i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11d;
      }
    })();
    function gexp(n) { return EXP[n % 255]; }
    function gmul(a, b) { return (a === 0 || b === 0) ? 0 : EXP[(LOG[a] + LOG[b]) % 255]; }

    function rsGenPoly(deg) {
      var poly = [1];
      for (var i = 0; i < deg; i++) {
        var factor = [1, gexp(i)];
        var next = [];
        for (var k = 0; k < poly.length + 1; k++) next[k] = 0;
        for (var a = 0; a < poly.length; a++) {
          for (var b = 0; b < 2; b++) next[a + b] ^= gmul(poly[a], factor[b]);
        }
        poly = next;
      }
      return poly;
    }

    function rsEncode(data, ecLen) {
      var gen = rsGenPoly(ecLen);
      var res = data.slice(0);
      for (var i = 0; i < ecLen; i++) res.push(0);
      for (var d = 0; d < data.length; d++) {
        var coef = res[d];
        if (coef !== 0) {
          for (var j = 1; j < gen.length; j++) res[d + j] ^= gmul(gen[j], coef);
        }
      }
      return res.slice(data.length);
    }

    function BitBuf() { this.buf = []; this.len = 0; }
    BitBuf.prototype.put = function (num, length) {
      for (var i = 0; i < length; i++) this.putBit(((num >>> (length - i - 1)) & 1) === 1);
    };
    BitBuf.prototype.putBit = function (bit) {
      if (this.len % 8 === 0) this.buf.push(0);
      if (bit) this.buf[Math.floor(this.len / 8)] |= (0x80 >>> (this.len % 8));
      this.len++;
    };

    function totalDataCount(version) {
      var rs = RS_BLOCKS_L[version - 1], total = 0;
      for (var i = 0; i < rs.length; i += 3) total += rs[i] * rs[i + 2];
      return total;
    }

    function makeData(version, text) {
      var totalData = totalDataCount(version);
      var buffer = new BitBuf();
      buffer.put(4, 4); // byte 模式
      buffer.put(text.length, version < 10 ? 8 : 16);
      for (var i = 0; i < text.length; i++) buffer.put(text.charCodeAt(i) & 0xff, 8);
      if (buffer.len + 4 <= totalData * 8) buffer.put(0, 4);
      while (buffer.len % 8 !== 0) buffer.putBit(false);
      while (buffer.buf.length < totalData) {
        buffer.put(0xec, 8);
        if (buffer.buf.length < totalData) buffer.put(0x11, 8);
      }
      var rs = RS_BLOCKS_L[version - 1];
      var dcdata = [], ecdata = [], offset = 0, maxDc = 0, maxEc = 0;
      for (i = 0; i < rs.length; i += 3) {
        for (var b = 0; b < rs[i]; b++) {
          var dcCount = rs[i + 2], ecCount = rs[i + 1] - rs[i + 2];
          var dc = buffer.buf.slice(offset, offset + dcCount);
          offset += dcCount;
          dcdata.push(dc);
          var ec = rsEncode(dc, ecCount);
          ecdata.push(ec);
          if (dcCount > maxDc) maxDc = dcCount;
          if (ec.length > maxEc) maxEc = ec.length;
        }
      }
      var data = [];
      for (i = 0; i < maxDc; i++) {
        for (var r = 0; r < dcdata.length; r++) if (i < dcdata[r].length) data.push(dcdata[r][i]);
      }
      for (i = 0; i < maxEc; i++) {
        for (r = 0; r < ecdata.length; r++) if (i < ecdata[r].length) data.push(ecdata[r][i]);
      }
      return data;
    }

    function bchDigit(data) {
      var digit = 0, d = data >>> 0;
      while (d !== 0) { digit++; d >>>= 1; }
      return digit;
    }
    function bchTypeInfo(data) {
      var d = data << 10;
      while (bchDigit(d) - bchDigit(G15) >= 0) d ^= (G15 << (bchDigit(d) - bchDigit(G15)));
      return ((data << 10) | d) ^ G15_MASK;
    }
    function bchTypeNumber(data) {
      var d = data << 12;
      while (bchDigit(d) - bchDigit(G18) >= 0) d ^= (G18 << (bchDigit(d) - bchDigit(G18)));
      return (data << 12) | d;
    }

    function maskBit(mask, row, col) {
      switch (mask) {
        case 0: return (row + col) % 2 === 0;
        case 1: return row % 2 === 0;
        case 2: return col % 3 === 0;
        case 3: return (row + col) % 3 === 0;
        case 4: return (Math.floor(row / 2) + Math.floor(col / 3)) % 2 === 0;
        case 5: return (row * col) % 2 + (row * col) % 3 === 0;
        case 6: return ((row * col) % 2 + (row * col) % 3) % 2 === 0;
        default: return ((row + col) % 2 + (row * col) % 3) % 2 === 0;
      }
    }

    function buildMatrix(version, data, test, mask) {
      var n = version * 4 + 17;
      var modules = [];
      var r, c, i, j;
      for (r = 0; r < n; r++) {
        var rowArr = [];
        for (c = 0; c < n; c++) rowArr.push(null);
        modules.push(rowArr);
      }
      function probe(r0, c0) {
        for (var rr = -1; rr <= 7; rr++) {
          for (var cc = -1; cc <= 7; cc++) {
            if (r0 + rr < 0 || r0 + rr >= n || c0 + cc < 0 || c0 + cc >= n) continue;
            modules[r0 + rr][c0 + cc] =
              (0 <= rr && rr <= 6 && (cc === 0 || cc === 6)) ||
              (0 <= cc && cc <= 6 && (rr === 0 || rr === 6)) ||
              (2 <= rr && rr <= 4 && 2 <= cc && cc <= 4);
          }
        }
      }
      probe(0, 0);
      probe(n - 7, 0);
      probe(0, n - 7);
      var pos = ALIGN_POS[version - 1];
      for (i = 0; i < pos.length; i++) {
        for (j = 0; j < pos.length; j++) {
          var ar = pos[i], ac = pos[j];
          if (modules[ar][ac] !== null) continue;
          for (r = -2; r <= 2; r++) {
            for (c = -2; c <= 2; c++) {
              modules[ar + r][ac + c] = Math.max(Math.abs(r), Math.abs(c)) !== 1;
            }
          }
        }
      }
      for (r = 8; r < n - 8; r++) {
        if (modules[r][6] === null) modules[r][6] = r % 2 === 0;
      }
      for (c = 8; c < n - 8; c++) {
        if (modules[6][c] === null) modules[6][c] = c % 2 === 0;
      }
      var bits = bchTypeInfo((1 << 3) | mask); // 纠错级别 L = 01
      var mod;
      for (i = 0; i < 15; i++) {
        mod = !test && ((bits >> i) & 1) === 1;
        if (i < 6) modules[i][8] = mod;
        else if (i < 8) modules[i + 1][8] = mod;
        else modules[n - 15 + i][8] = mod;
      }
      for (i = 0; i < 15; i++) {
        mod = !test && ((bits >> i) & 1) === 1;
        if (i < 8) modules[8][n - i - 1] = mod;
        else if (i < 9) modules[8][15 - i - 1 + 1] = mod;
        else modules[8][15 - i - 1] = mod;
      }
      modules[n - 8][8] = !test;
      if (version >= 7) {
        bits = bchTypeNumber(version);
        for (i = 0; i < 18; i++) {
          mod = !test && ((bits >> i) & 1) === 1;
          modules[Math.floor(i / 3)][i % 3 + n - 11] = mod;
          modules[i % 3 + n - 11][Math.floor(i / 3)] = mod;
        }
      }
      var inc = -1, rr2 = n - 1, bitIndex = 7, byteIndex = 0;
      for (var col = n - 1; col > 0; col -= 2) {
        if (col === 6) col--;
        while (true) {
          for (c = 0; c < 2; c++) {
            if (modules[rr2][col - c] === null) {
              var dark = false;
              if (byteIndex < data.length) dark = ((data[byteIndex] >>> bitIndex) & 1) === 1;
              if (maskBit(mask, rr2, col - c)) dark = !dark;
              modules[rr2][col - c] = dark;
              bitIndex--;
              if (bitIndex === -1) { byteIndex++; bitIndex = 7; }
            }
          }
          rr2 += inc;
          if (rr2 < 0 || rr2 >= n) { rr2 -= inc; inc = -inc; break; }
        }
      }
      return modules;
    }

    function lostPoint(modules, n) {
      var lost = 0, row, col, sameCount, dark, c2;
      for (row = 0; row < n; row++) {
        sameCount = 0;
        dark = modules[row][0];
        for (col = 0; col < n; col++) {
          if (modules[row][col] === dark) {
            sameCount++;
            if (sameCount === 5) lost += 3;
            else if (sameCount > 5) lost++;
          } else {
            sameCount = 1;
            dark = modules[row][col];
          }
        }
      }
      for (col = 0; col < n; col++) {
        sameCount = 0;
        dark = modules[0][col];
        for (row = 0; row < n; row++) {
          if (modules[row][col] === dark) {
            sameCount++;
            if (sameCount === 5) lost += 3;
            else if (sameCount > 5) lost++;
          } else {
            sameCount = 1;
            dark = modules[row][col];
          }
        }
      }
      for (row = 0; row < n - 1; row++) {
        for (col = 0; col < n - 1; col++) {
          var count = 0;
          if (modules[row][col]) count++;
          if (modules[row + 1][col]) count++;
          if (modules[row][col + 1]) count++;
          if (modules[row + 1][col + 1]) count++;
          if (count === 0 || count === 4) lost += 3;
        }
      }
      for (row = 0; row < n; row++) {
        for (col = 0; col < n - 6; col++) {
          if (modules[row][col] && !modules[row][col + 1] && modules[row][col + 2] &&
              modules[row][col + 3] && modules[row][col + 4] && !modules[row][col + 5] &&
              modules[row][col + 6]) lost += 40;
        }
      }
      for (col = 0; col < n; col++) {
        for (row = 0; row < n - 6; row++) {
          if (modules[row][col] && !modules[row + 1][col] && modules[row + 2][col] &&
              modules[row + 3][col] && modules[row + 4][col] && !modules[row + 5][col] &&
              modules[row + 6][col]) lost += 40;
        }
      }
      var darkCount = 0;
      for (row = 0; row < n; row++) {
        for (c2 = 0; c2 < n; c2++) if (modules[row][c2]) darkCount++;
      }
      lost += Math.floor(Math.abs(100 * darkCount / n / n - 50) / 5) * 10;
      return lost;
    }

    var version = 0;
    for (var v = 1; v <= 10; v++) {
      var need = 4 + (v < 10 ? 8 : 16) + text.length * 8;
      if (need <= totalDataCount(v) * 8) { version = v; break; }
    }
    if (!version) throw new Error('内容过长，无法生成二维码');
    var data = makeData(version, text);
    var n = version * 4 + 17;
    var bestLost = -1, bestMask = 0;
    for (var mask = 0; mask < 8; mask++) {
      var lost = lostPoint(buildMatrix(version, data, true, mask), n);
      if (bestLost < 0 || lost < bestLost) { bestLost = lost; bestMask = mask; }
    }
    return { size: n, modules: buildMatrix(version, data, false, bestMask), mask: bestMask, version: version };
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
        timeout: opts.timeout || 0,
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

  // 把一段同步工作挪到下一帧：QuickJS 与输入处理共用主线程，
  // 连续读盘/建对象之间必须让出一次，否则加载期间摇杆整段无响应。
  defer(fn) {
    return new Promise(function (resolve) {
      setTimeout(function () {
        var value;
        try {
          value = fn();
        } catch (e) {
          resolve(undefined);
          return;
        }
        resolve(value);
      }, 0);
    });
  },

  // 影片列表落盘缓存：首次扫描后保存，下次进入先用缓存秒开，过期再后台刷新。
  moviesCacheKey(id) { return 'bmh_movies_' + id; },

  moviesCountKey(id) { return 'bmh_count_' + id; },

  // 数据源页只需要数量，读一个数字比解析 300 条 JSON 便宜得多。
  getMoviesCount(id) {
    try {
      var n = wx.getStorageSync(this.moviesCountKey(id));
      if (typeof n === 'number') return n;
    } catch (e) {}
    var cache = this.getMoviesCache(id);
    return cache ? cache.list.length : 0;
  },

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

  // 列表缓存只保留卡片要用的字段，体积能少一半以上，读盘/解析也更快。
  slimMovie(m) {
    return {
      id: m.id,
      sourceId: m.sourceId,
      sourceName: m.sourceName,
      sourceType: m.sourceType,
      itemId: m.itemId,
      title: m.title,
      year: m.year,
      genre: m.genre,
      rating: m.rating,
      poster: m.poster
    };
  },

  saveMoviesCache(id, list) {
    if (!list || !list.length) return;
    var self = this;
    // 300 条 JSON 的序列化 + 落盘在主线程要几十毫秒，推迟到首屏渲染之后再写，
    // 否则加载刚结束时摇杆会顿一下。
    setTimeout(function () {
      var count = Math.min(list.length, self.MOVIE_CACHE_LIMIT);
      var kept = [];
      for (var i = 0; i < count; i++) kept.push(self.slimMovie(list[i]));
      try {
        wx.setStorageSync(self.moviesCacheKey(id), { time: Date.now(), list: kept });
        wx.setStorageSync(self.moviesCountKey(id), kept.length);
      } catch (e) {}
    }, 200);
  },

  // ---------- 聚合接口 ----------
  // 从所有已启用数据源拉取影片
  aggregateMovies() {
    var self = this;
    var sources = self.getEnabledSources();
    if (sources.length === 0) return Promise.resolve([]);

    // 每个数据源错开一帧再读缓存：同步读 300 条 JSON 会阻塞输入，
    // 串行执行等于把多份阻塞叠在一起，分帧后摇杆始终有响应。
    var tasks = sources.map(function (s) {
      return self.defer(function () {
        return self.loadSourceMovies(s);
      });
    });

    return Promise.all(tasks).then(function (results) {
      var all = [];
      results.forEach(function (list) { all = all.concat(list || []); });
      return all;
    });
  },

  // 单个数据源：优先用落盘缓存，过期则先返回旧列表、后台静默刷新。
  loadSourceMovies(s) {
    var self = this;
    var started = Date.now();
    var cached = self.getMoviesCache(s.id);
    if (cached) {
      self.globalData.cache.movies[s.id] = cached.list;
      if (!cached.stale) {
        console.log('source ' + s.id + ' ' + s.type + ' -> ' + cached.list.length +
                    ' movies from cache');
        return cached.list;
      }
      self.withTimeout(self.listMovies(s), 20000, []).then(function (list) {
        if (list && list.length) {
          self.saveMoviesCache(s.id, list);
          self.globalData.cache.movies[s.id] = list;
        }
      }).catch(function () {});
      return cached.list;
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
    // 用一次轻量请求验证；网络挂住时给个上限，避免测试按钮一直转圈。
    // 失败要把真实原因抛出去（withTimeout 会把 reject 吞成 fallback，这里不用它）。
    var self = this;
    return new Promise(function (resolve, reject) {
      var finished = false;
      var timer = setTimeout(function () {
        if (finished) return;
        finished = true;
        reject(new Error('连接超时，请检查地址与网络'));
      }, 15000);
      self.listMovies(source).then(function (list) {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        resolve({ ok: true, count: (list || []).length });
      }, function (err) {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        reject(err || new Error('连接失败'));
      });
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
  // 扫码登录（阿里云盘 / 百度网盘）
  // 二维码在本地渲染（纯 JS 生成矩阵，WXSS 画点），不依赖 web-view。
  // =====================================================================

  jsonOf(res) {
    var d = res && res.data;
    if (typeof d === 'string') {
      try { return JSON.parse(d); } catch (e) { return null; }
    }
    return d || null;
  },

  // ---------- 阿里云盘：网页登录二维码换 refresh_token ----------
  aliyunQrHeaders() {
    return {
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36',
      'Referer': 'https://www.aliyundrive.com/'
    };
  },

  aliyunQrCreate() {
    var self = this;
    return self.http({
      url: 'https://passport.aliyun.com/newlogin/qrcode/generate.do?appName=aliyun_drive&fromSite=0',
      header: self.aliyunQrHeaders(),
      timeout: 12000
    }).then(function (res) {
      var body = self.jsonOf(res) || {};
      var d = (body.content && body.content.data) || {};
      if (!d.codeContent || !d.ck) throw new Error('阿里云盘二维码获取失败');
      return { content: d.codeContent, t: d.t, ck: d.ck };
    });
  },

  aliyunQrPoll(session) {
    var self = this;
    var header = self.aliyunQrHeaders();
    header['Content-Type'] = 'application/x-www-form-urlencoded';
    // query.do 只认 POST 表单，GET 会直接返回 EXPIRED。
    var payload = 't=' + session.t + '&ck=' + encodeURIComponent(session.ck) +
                  '&appName=aliyun_drive&fromSite=0';
    return self.http({
      url: 'https://passport.aliyun.com/newlogin/qrcode/query.do?appName=aliyun_drive&fromSite=0',
      method: 'POST',
      header: header,
      data: payload,
      timeout: 10000
    }).then(function (res) {
      var body = self.jsonOf(res) || {};
      var data = (body.content && body.content.data) || {};
      var status = data.qrCodeStatus || 'NEW';
      if (status !== 'CONFIRMED') return { status: status };
      var parsed = self.parseAliyunLogin(data.bizExt);
      if (!parsed.refreshToken) throw new Error('扫码成功但没有拿到登录凭证，请刷新二维码重试');
      return {
        status: 'CONFIRMED',
        refreshToken: parsed.refreshToken,
        accessToken: parsed.accessToken,
        driveId: parsed.driveId,
        nickname: parsed.nickname
      };
    });
  },

  parseAliyunLogin(bizExt) {
    if (!bizExt) return {};
    try {
      var json = JSON.parse(this.atob(bizExt));
      var r = json.pds_login_result || json;
      return {
        refreshToken: r.refresh_token || '',
        accessToken: r.access_token || '',
        driveId: r.default_drive_id || '',
        nickname: r.nick_name || ''
      };
    } catch (e) {
      var m = String(bizExt).match(/refresh_token["'\s:=]+([A-Za-z0-9._-]+)/);
      return { refreshToken: m ? m[1] : '' };
    }
  },

  // ---------- 百度网盘：网页扫码拿 BDUSS / STOKEN ----------
  baiduQrHeaders() {
    return {
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36',
      'Referer': 'https://pan.baidu.com/'
    };
  },

  baiduQrCreate() {
    var self = this;
    var gid = String(Math.floor(Math.random() * 90000000) + 10000000);
    return self.http({
      url: 'https://passport.baidu.com/v2/api/getqrcode?lp=pc&qrloginfrom=pc&gid=' + gid,
      header: self.baiduQrHeaders(),
      timeout: 12000
    }).then(function (res) {
      var d = self.jsonOf(res) || {};
      if (!d.sign || !d.imgurl) throw new Error('百度网盘二维码获取失败');
      // imgurl 本身就是要编码进二维码的登录地址，本地画码即可，
      // 不依赖百度返回的 1bit 调色板 PNG（容器图片解码不一定支持）。
      var content = String(d.imgurl);
      if (content.indexOf('//') === 0) content = 'https:' + content;
      else if (content.indexOf('http') !== 0) content = 'https://' + content;
      return { content: content, sign: d.sign };
    });
  },

  baiduQrPoll(session) {
    var self = this;
    var url = 'https://passport.baidu.com/channel/unicast?channel_id=' +
              encodeURIComponent(session.sign) + '&tpl=netdisk&callback=&_=' + Date.now();
    return self.http({ url: url, header: self.baiduQrHeaders(), timeout: 10000 }).then(function (res) {
      var body = self.jsonOf(res) || {};
      var errno = body.errno;
      if (errno === 1) return { status: 'SCANED' };
      if (errno === 2) return { status: 'EXPIRED' };
      if (errno !== 0) return { status: 'NEW' };
      var cookie = self.parseBaiduChannel(body.channel_v);
      if (!cookie) return { status: 'NEW' };
      return self.baiduFillStoken(cookie).then(function (full) {
        return { status: 'CONFIRMED', cookie: full };
      });
    });
  },

  parseBaiduChannel(channelV) {
    var text = String(channelV || '');
    try {
      var outer = JSON.parse(text);
      if (outer) text = outer.v || outer.cookie || text;
    } catch (e) {}
    for (var i = 0; i < 2; i++) {
      try {
        var decoded = decodeURIComponent(text);
        if (decoded === text) break;
        text = decoded;
      } catch (e) { break; }
    }
    var bduss = (text.match(/bduss=([^&;\s"']+)/i) || [])[1];
    var stoken = (text.match(/stoken=([^&;\s"']+)/i) || [])[1];
    if (!bduss) return '';
    return 'BDUSS=' + bduss + (stoken ? '; STOKEN=' + stoken : '');
  },

  // 部分账号的通道只回 BDUSS，STOKEN 需要再取一次；失败也不影响列表。
  baiduFillStoken(cookie) {
    var self = this;
    if (/stoken=/i.test(cookie)) return Promise.resolve(cookie);
    return self.http({
      url: 'https://pan.baidu.com/api/gettemplatevariable?fields=' +
           encodeURIComponent('["stoken"]'),
      header: { 'Cookie': cookie, 'User-Agent': 'pan.baidu.com', 'Referer': 'https://pan.baidu.com/' }
    }).then(function (res) {
      var body = self.jsonOf(res) || {};
      var stoken = (body.result && body.result.stoken) || '';
      return stoken ? (cookie + '; STOKEN=' + stoken) : cookie;
    }).catch(function () { return cookie; });
  },

  // =====================================================================
  // 百度网盘适配器
  // 扫码登录后走网页接口（Cookie），也兼容旧版 access_token。
  // =====================================================================
  baiduConfig(source) {
    return source.config || {};
  },

  baiduCookie(source) {
    return String(this.baiduConfig(source).cookie || '').trim();
  },

  baiduHeaders(source) {
    var cookie = this.baiduCookie(source);
    if (!cookie) return { 'User-Agent': 'pan.baidu.com' };
    return {
      'Cookie': cookie,
      'User-Agent': 'pan.baidu.com',
      'Referer': 'https://pan.baidu.com/'
    };
  },

  // mpv 的 http-header-fields 是逗号分隔的字符串列表。
  baiduPlayHeaders(source) {
    var cookie = this.baiduCookie(source);
    if (!cookie) return '';
    return 'Cookie: ' + cookie + ',User-Agent: pan.baidu.com,Referer: https://pan.baidu.com/';
  },

  // 统一的文件信息接口：网页端返回 info，开放平台返回 list。
  baiduFileMeta(source, fsid) {
    var self = this;
    var cookie = self.baiduCookie(source);
    var cfg = self.baiduConfig(source);
    var url;
    if (cookie) {
      url = 'https://pan.baidu.com/api/filemetas?target=' + encodeURIComponent('[' + fsid + ']') +
            '&dlink=1&web=5&clienttype=0&app_id=250528';
    } else {
      url = 'https://pan.baidu.com/rest/2.0/xpan/multimedia?method=filemetas&access_token=' +
            cfg.accessToken + '&fsids=' + encodeURIComponent('[' + fsid + ']') + '&dlink=1';
    }
    return self.http({ url: url, header: self.baiduHeaders(source) }).then(function (res) {
      var body = res.data || {};
      if (body.errno && body.errno !== 0) {
        throw new Error(body.errno === -6
          ? '百度网盘登录已过期，请重新扫码'
          : ('百度网盘错误：' + (body.errmsg || body.errno)));
      }
      return (body.list && body.list[0]) || (body.info && body.info[0]) || {};
    });
  },

  baiduScanDir(source, dir, depth) {
    var self = this;
    if (depth > 6) return Promise.resolve([]);
    var cfg = self.baiduConfig(source);
    var cookie = self.baiduCookie(source);
    var url;
    if (cookie) {
      url = 'https://pan.baidu.com/api/list?order=name&desc=0&showempty=0&web=1&page=1&num=1000&dir=' +
            encodeURIComponent(dir) + '&app_id=250528';
    } else {
      url = 'https://pan.baidu.com/rest/2.0/xpan/file?method=list&access_token=' + cfg.accessToken +
            '&dir=' + encodeURIComponent(dir) + '&limit=1000';
    }
    return self.http({
      url: url,
      header: self.baiduHeaders(source)
    }).then(function (res) {
      var body = res.data || {};
      if (body.errno && body.errno !== 0) {
        throw new Error(body.errno === -6
          ? '百度网盘登录已过期，请重新扫码'
          : ('百度网盘错误：' + (body.errmsg || body.errno)));
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
    return self.baiduFileMeta(source, itemId).then(function (info) {
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
    var cookie = self.baiduCookie(source);
    var cfg = self.baiduConfig(source);
    return self.baiduFileMeta(source, itemId).then(function (info) {
      if (!info.dlink) throw new Error('未获取到下载链接');
      if (!cookie) {
        // 旧版 access_token 直接拼在 dlink 上
        return info.dlink + '&access_token=' + cfg.accessToken;
      }
      // 网页端 dlink 带防盗链，必须把 Cookie 一起交给播放器
      return { url: info.dlink, headers: self.baiduPlayHeaders(source) };
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

  // SMB 扫描根目录：用户手动选的子目录，留空/“/”表示共享根。
  // 只扫这个目录及其子目录，不再遍历整个共享。
  smbRootPath(source) {
    var p = ((source.config && source.config.rootPath) || '/').trim();
    if (p.charAt(0) !== '/') p = '/' + p;
    return p.replace(/\/+$/, '') || '/';
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
          sourceId: source.id, id: smbId, root: self.smbRootPath(source),
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

  // 扫描节流：30 分钟内不重复扫盘；同一数据源不会并发扫描。
  // 扫盘要走 SMB 网络遍历，在硬件羸弱的 Switch 上很吃 CPU/网络，
  // 而片库变化很慢，没必要每次进首页都扫一遍。
  smbScheduleScan(source, delay) {
    var self = this;
    var now = Date.now();
    var last = self.globalData.smbScanAt[source.id] || 0;
    // App 实例不跨页共享，扫描时间也落盘，避免每次进入都重新扫盘。
    try {
      var stored = wx.getStorageSync('bmh_scan_' + source.id) || 0;
      if (stored > last) last = stored;
    } catch (e) {}
    if (self.globalData.smbScanning[source.id]) return;
    if (now - last < 30 * 60000) return;
    self.globalData.smbScanAt[source.id] = now;
    try { wx.setStorageSync('bmh_scan_' + source.id, now); } catch (e) {}
    self.globalData.smbScanning[source.id] = true;
    var run = function () {
      self.smbNativeScan(source).then(function () {
        self.globalData.smbScanning[source.id] = false;
        self.globalData.smbUpdatedAt = Date.now();
      }, function () {
        self.globalData.smbScanning[source.id] = false;
      });
    };
    if (delay > 0) setTimeout(run, delay);
    else run();
  },

  // 原生媒体库路径：只读索引，不在首屏等待扫描。
  // 有索引立刻返回并延后增量刷新；无索引立刻返回空列表，后台扫描完成由页面轮询刷新。
  // 只取首页要用的前 MOVIE_CACHE_LIMIT 条：索引里可能上万条，全量取回来
  // 要在 QuickJS 里逐条建影片对象，Switch 上会卡好几秒（缓存本身也只留这么多）。
  smbLibraryMovies(source) {
    var self = this;
    var limit = self.MOVIE_CACHE_LIMIT;
    console.log('media library read source=' + source.id + ' limit=' + limit);
    // 本地索引读盘在 Switch 上可能超过 3 秒，超时会导致列表永远为空、
    // 每次进首页都重新读一遍，所以给足 20 秒。
    return self.withTimeout(self.smbLibraryPage(source, 0, limit), 20000, { total: 0, list: [] })
      .then(function (res) {
        var files = res.list || [];
        if (files.length) {
          console.log('media library hit source=' + source.id + ' total=' + res.total +
                      ' loaded=' + files.length);
          // 先让首屏渲染完，再启动扫描，避免和布局/图片抢 CPU。
          self.smbScheduleScan(source, 3000);
          return self.smbMapMovies(source, files);
        }
        console.log('media library miss source=' + source.id + ', background scan scheduled');
        self.smbScheduleScan(source, 300);
        return [];
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
      var root = self.smbRootPath(source);
      if (typeof wx.smb.scan === 'function') {
        return self.smbScanNative(smbId, root, 4, 300);
      }
      return self.smbScanDir(smbId, root, 0);
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
