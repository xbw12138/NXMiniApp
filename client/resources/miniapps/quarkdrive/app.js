App({
  globalData: {
    base: 'https://drive-pc.quark.cn',
    panBase: 'https://pan.quark.cn',
    common: 'pr=ucpro&fr=pc&uc_param_str=',
    ua: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36',
    pageSize: 30
  },

  VIDEO_EXT: ['mp4', 'mkv', 'avi', 'mov', 'wmv', 'flv', 'ts', 'm2ts', 'webm', 'mpg', 'mpeg', 'rmvb', 'rm', '3gp', 'm3u8', 'vob'],
  AUDIO_EXT: ['mp3', 'flac', 'wav', 'aac', 'ogg', 'm4a', 'wma', 'ape'],
  IMAGE_EXT: ['jpg', 'jpeg', 'png', 'gif', 'webp', 'bmp', 'heic'],
  DOC_EXT: ['txt', 'md', 'pdf', 'doc', 'docx', 'xls', 'xlsx', 'ppt', 'pptx', 'epub'],

  onLaunch() {},

  cookie() {
    var v = '';
    try { v = wx.getStorageSync('quark_cookie') || ''; } catch (e) {}
    return v;
  },

  headers() {
    return {
      'cookie': this.cookie(),
      'user-agent': this.globalData.ua,
      'referer': this.globalData.panBase + '/',
      'accept': 'application/json, text/plain, */*'
    };
  },

  apiUrl(path) {
    var sep = path.indexOf('?') >= 0 ? '&' : '?';
    return this.globalData.base + path + sep + this.globalData.common;
  },

  parseBody(res) {
    var d = res && res.data;
    if (typeof d === 'string') {
      try { d = JSON.parse(d); } catch (e) { return null; }
    }
    return d || null;
  },

  request(opts) {
    var self = this;
    return new Promise(function (resolve, reject) {
      wx.request({
        url: opts.url,
        method: opts.method || 'GET',
        data: opts.data,
        header: self.headers(),
        success: function (res) {
          var body = self.parseBody(res);
          if (!body) {
            reject(new Error('响应解析失败（HTTP ' + res.statusCode + '）'));
            return;
          }
          if (res.statusCode === 401 || res.statusCode === 403 || body.status === 401 || body.code === 41001 || body.code === 31001) {
            reject(new Error('登录已失效，请重新粘贴 Cookie'));
            return;
          }
          resolve(body);
        },
        fail: function (err) {
          reject(new Error(err && err.errMsg ? err.errMsg : '网络请求失败'));
        }
      });
    });
  },

  // ---------- 扫码登录（流程参考 AnyCloud：uop 令牌 → 轮询 → 票据换 Cookie） ----------

  uuid4() {
    var d = '0123456789abcdef', s = '';
    for (var i = 0; i < 36; i++) {
      if (i === 8 || i === 13 || i === 18 || i === 23) s += '-';
      else if (i === 14) s += '4';
      else if (i === 19) s += d[8 + Math.floor(Math.random() * 4)];
      else s += d[Math.floor(Math.random() * 16)];
    }
    return s;
  },

  qrHeaders() {
    return {
      'user-agent': this.globalData.ua,
      'referer': this.globalData.panBase + '/',
      'accept': 'application/json, text/plain, */*'
    };
  },

  netFail(err) {
    var d = err && err.errMsg ? String(err.errMsg).replace(/^request:fail\s*/, '') : '';
    return new Error('网络请求失败，请检查网络' + (d ? '（' + d + '）' : ''));
  },

  qrToken() {
    var self = this;
    return new Promise(function (resolve, reject) {
      wx.request({
        url: 'https://uop.quark.cn/cas/ajax/getTokenForQrcodeLogin?client_id=532&v=1.2&request_id=' + self.uuid4(),
        method: 'GET',
        header: self.qrHeaders(),
        success: function (res) {
          var body = self.parseBody(res);
          var token = body && body.data && body.data.members && body.data.members.token;
          if (!token) {
            reject(new Error((body && body.message) || '获取登录二维码失败'));
            return;
          }
          resolve({
            token: token,
            content: 'https://su.quark.cn/4_eMHBJ?token=' + token +
              '&client_id=532&ssb=weblogin&uc_param_str=&uc_biz_str=' +
              'S%3Acustom%7COPT%3ASAREA%400%7COPT%3AIMMERSIVE%401%7COPT%3ABACK_BTN_STYLE%400'
          });
        },
        fail: function (err) { reject(self.netFail(err)); }
      });
    });
  },

  qrPoll(token) {
    var self = this;
    return new Promise(function (resolve, reject) {
      wx.request({
        url: 'https://uop.quark.cn/cas/ajax/getServiceTicketByQrcodeToken?client_id=532&v=1.2&token=' +
          encodeURIComponent(token) + '&request_id=' + self.uuid4(),
        method: 'GET',
        header: self.qrHeaders(),
        success: function (res) {
          var body = self.parseBody(res);
          if (!body) { reject(new Error('响应解析失败')); return; }
          var st = Number(body.status);
          if (st === 50004001) { resolve({ state: 'waiting' }); return; }
          if (st === 50004002) { resolve({ state: 'expired' }); return; }
          if (st !== 2000000) { resolve({ state: 'waiting', message: body.message || '' }); return; }
          var ticket = body.data && body.data.members && body.data.members.service_ticket;
          if (!ticket) { reject(new Error('未获取到登录票据')); return; }
          resolve({ state: 'ok', ticket: ticket });
        },
        fail: function (err) { reject(self.netFail(err)); }
      });
    });
  },

  cookieString(map) {
    var parts = [];
    for (var k in map) {
      if (Object.prototype.hasOwnProperty.call(map, k) && map[k]) parts.push(k + '=' + map[k]);
    }
    return parts.join('; ');
  },

  mergeCookies(a, b) {
    var map = {};
    function add(s) {
      if (!s) return;
      var pairs = s.split('; ');
      for (var i = 0; i < pairs.length; i++) {
        var eq = pairs[i].indexOf('=');
        if (eq > 0) map[pairs[i].slice(0, eq)] = pairs[i].slice(eq + 1);
      }
    }
    add(a);
    add(b);
    return this.cookieString(map);
  },

  exchangeTicket(ticket) {
    var self = this;
    var url = self.globalData.panBase + '/account/info?st=' + encodeURIComponent(ticket) +
      '&lw=scan&fr=pc&platform=pc';
    return new Promise(function (resolve, reject) {
      wx.request({
        url: url,
        method: 'GET',
        header: self.qrHeaders(),
        success: function (res) {
          var cookie = self.cookieString(res.cookies || {});
          if (!cookie) { reject(new Error('EMPTY_COOKIES')); return; }
          resolve(cookie);
        },
        fail: function (err) { reject(self.netFail(err)); }
      });
    }).then(function (cookie) {
      // 再补一轮 drive 侧 Cookie（失败不致命）
      return new Promise(function (resolve) {
        wx.request({
          url: self.apiUrl('/1/clouddrive/config'),
          method: 'GET',
          header: {
            'cookie': cookie,
            'user-agent': self.globalData.ua,
            'referer': self.globalData.panBase + '/',
            'accept': 'application/json, text/plain, */*'
          },
          success: function (res2) {
            var extra = self.cookieString(res2.cookies || {});
            resolve(extra ? self.mergeCookies(cookie, extra) : cookie);
          },
          fail: function () { resolve(cookie); }
        });
      });
    }).then(function (cookie) {
      wx.setStorageSync('quark_cookie', cookie);
      return cookie;
    });
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

  accountInfo() {
    var self = this;
    return new Promise(function (resolve, reject) {
      wx.request({
        url: self.globalData.panBase + '/account/info?fr=pc&platform=pc',
        method: 'GET',
        header: self.headers(),
        success: function (res) {
          var body = self.parseBody(res);
          // pan.quark.cn/account/info 返回 {"success":true,"data":{...}}，
          // 只有部分接口才是 {"code":0,"data":{...}}，两种都要认。
          if (body && body.data && (body.code === 0 || body.success === true)) {
            resolve(body.data);
          } else {
            reject(new Error('Cookie 无效或已过期'));
          }
        },
        fail: function () { reject(new Error('网络请求失败，请检查网络')); }
      });
    });
  },

  listFiles(fid, page) {
    return this.request({
      url: this.apiUrl('/1/clouddrive/file/sort'),
      data: {
        pdir_fid: fid,
        _page: page,
        _size: this.globalData.pageSize,
        _fetch_total: 1,
        _fetch_sub_dirs: 0,
        _sort: 'file_type:asc,updated_at:desc'
      }
    });
  },

  searchFiles(q, page) {
    var url = this.apiUrl('/1/clouddrive/file/search') +
      '&q=' + encodeURIComponent(q) +
      '&_page=' + page + '&_size=' + this.globalData.pageSize +
      '&_fetch_total=1&_sort=file_type:asc,updated_at:desc';
    return this.request({ url: url });
  },

  videoSources(fid) {
    return this.request({
      url: this.apiUrl('/1/clouddrive/file/v2/play'),
      method: 'POST',
      data: {
        fid: fid,
        resolutions: 'low,normal,high,super,2k,4k',
        supports: 'fmp4_av,m3u8_av,m3u8_hdr,m3u8,avc265,avc264'
      }
    });
  },

  // download 接口通常直接返回 download_url/preview_url；个别文件才需要轮询任务。
  fileUrls(fid) {
    var self = this;
    return this.request({
      url: this.apiUrl('/1/clouddrive/file/download'),
      method: 'POST',
      data: { fids: [fid] }
    }).then(function (body) {
      var d = body && body.data && body.data[0];
      if (d && (d.download_url || d.preview_url)) return d;
      var taskId = body && body.data && body.data.task_id;
      if (!taskId) throw new Error('获取文件链接失败');
      return self.waitTask(taskId, 0).then(function (url) {
        return { download_url: url };
      });
    });
  },

  downloadUrl(fid) {
    var self = this;
    return this.request({
      url: this.apiUrl('/1/clouddrive/file/download'),
      method: 'POST',
      data: { fids: [fid] }
    }).then(function (body) {
      var taskId = body && body.data && body.data.task_id;
      if (!taskId) throw new Error('创建下载任务失败');
      return self.waitTask(taskId, 0);
    });
  },

  waitTask(taskId, retry) {
    var self = this;
    return this.request({
      url: this.apiUrl('/1/clouddrive/task'),
      data: { task_id: taskId, retry_index: retry }
    }).then(function (body) {
      var d = body && body.data;
      if (d && d.download_url) return d.download_url;
      if (retry >= 20) throw new Error('下载链接生成超时');
      return new Promise(function (resolve) { setTimeout(resolve, 700); })
        .then(function () { return self.waitTask(taskId, retry + 1); });
    });
  },

  fileModel(f) {
    var name = f.file_name || f.name || '未命名';
    var dir = !!f.dir;
    var kind = 'file';
    if (dir) {
      kind = 'dir';
    } else {
      var ext = '';
      var i = name.lastIndexOf('.');
      if (i >= 0) ext = name.slice(i + 1).toLowerCase();
      if (f.category === 1 || this.VIDEO_EXT.indexOf(ext) >= 0) kind = 'video';
      else if (this.AUDIO_EXT.indexOf(ext) >= 0) kind = 'audio';
      else if (this.IMAGE_EXT.indexOf(ext) >= 0) kind = 'image';
      else if (this.DOC_EXT.indexOf(ext) >= 0) kind = 'doc';
    }
    var badgeMap = { dir: '目录', video: '视频', audio: '音频', image: '图片', doc: '文档', file: '文件' };
    return {
      fid: f.fid || '',
      name: name,
      dir: dir,
      kind: kind,
      badge: badgeMap[kind],
      sizeText: dir ? '' : this.fmtSize(f.size || 0),
      timeText: this.fmtTime(f.updated_at || f.l_updated_at || 0)
    };
  },

  fmtSize(bytes) {
    var n = Number(bytes) || 0;
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    if (n < 1024 * 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + ' MB';
    return (n / 1024 / 1024 / 1024).toFixed(2) + ' GB';
  },

  fmtTime(ms) {
    if (!ms) return '';
    var d = new Date(Number(ms));
    function p(x) { return x < 10 ? '0' + x : '' + x; }
    return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }
});
