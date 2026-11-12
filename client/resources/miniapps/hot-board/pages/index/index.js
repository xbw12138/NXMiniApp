// 今日热榜 · 大屏看板
// 热榜聚合（知乎/微博/百度/抖音）+ 指数与自选行情，支持自动翻页。

var PAGE_SIZE = 9;
var HOT_API = {
  zhihu: { name: '知乎热榜', url: 'https://api.vvhan.com/api/hotlist/zhihuHot' },
  weibo: { name: '微博热搜', url: 'https://api.vvhan.com/api/hotlist/wbHot' },
  baidu: { name: '百度热搜', url: 'https://api.vvhan.com/api/hotlist/baiduRD' },
  douyin: { name: '抖音热点', url: 'https://api.vvhan.com/api/hotlist/douyinHot' }
};
var INDEX_SECIDS = ['1.000001', '0.399001', '0.399006', '1.000300', '1.000016', '1.000688'];
var CODES_KEY = 'hb_codes';
var AUTO_KEY = 'hb_auto';

function pad(n) {
  return n < 10 ? '0' + n : '' + n;
}

function truncate(text, max) {
  var s = text == null ? '' : String(text);
  return s.length > max ? s.slice(0, max) + '…' : s;
}

function fmtHot(hot) {
  if (hot == null || hot === '') return '';
  if (typeof hot === 'string') {
    if (/万|亿/.test(hot)) return hot;
    var n = Number(hot);
    if (!isFinite(n)) return hot;
    hot = n;
  }
  if (typeof hot !== 'number' || !isFinite(hot)) return '';
  if (hot >= 100000000) return (hot / 100000000).toFixed(1) + '亿';
  if (hot >= 10000) return (hot / 10000).toFixed(1) + '万';
  return String(Math.round(hot));
}

function secidOf(code) {
  var c = String(code || '').replace(/\D/g, '');
  if (c.length === 6) {
    if (c.charAt(0) === '6' || c.charAt(0) === '9') return '1.' + c;
    return '0.' + c;
  }
  if (c.length === 5) return '116.' + c;
  return '';
}

function fmtPrice(v) {
  if (v === '-' || v == null || v === '') return '--';
  var n = Number(v);
  if (!isFinite(n)) return '--';
  return n.toFixed(2);
}

function parseHotBody(body) {
  var list = null;
  if (body) {
    if (Array.isArray(body)) list = body;
    else if (Array.isArray(body.data)) list = body.data;
    else if (body.data && Array.isArray(body.data.list)) list = body.data.list;
  }
  if (!list) return null;
  var out = [];
  for (var i = 0; i < list.length; i++) {
    var item = list[i] || {};
    var title = item.title || item.name || item.word || '';
    if (!title) continue;
    out.push({
      title: String(title),
      hot: fmtHot(item.hot || item.hotValue || item.num),
      url: item.url || item.mobil_url || ''
    });
  }
  return out;
}

function buildQuoteRows(raw, secids, customCodes) {
  var diff = raw && raw.data && raw.data.diff ? raw.data.diff : [];
  var map = {};
  for (var i = 0; i < diff.length; i++) {
    var item = diff[i] || {};
    if (item.f12) map[String(item.f12)] = item;
  }
  var rows = [];
  var j;
  for (j = 0; j < secids.length; j++) {
    var code = secids[j].split('.')[1];
    var hit = map[code];
    if (!hit) continue;
    rows.push(quoteRow(hit, j));
  }
  for (j = 0; j < customCodes.length; j++) {
    var hit2 = map[customCodes[j]];
    if (!hit2) continue;
    rows.push(quoteRow(hit2, secids.length + j));
  }
  return rows;
}

function quoteRow(hit, order) {
  var price = fmtPrice(hit.f2);
  var pct = Number(hit.f3);
  var cls = '';
  var text = price;
  if (isFinite(pct)) {
    cls = pct > 0 ? 'up' : (pct < 0 ? 'down' : '');
    text = price + '   ' + (pct > 0 ? '+' : '') + pct.toFixed(2) + '%';
  }
  return {
    key: 'q' + order,
    rank: order + 1,
    top: false,
    title: (hit.f14 || hit.f12 || '') + '  ' + (hit.f12 || ''),
    value: text,
    cls: cls,
    code: hit.f12 || '',
    name: hit.f14 || ''
  };
}

Page({
  data: {
    clock: '--:--',
    dateText: '',
    mode: 'hot',
    source: 'zhihu',
    sources: [],
    rows: [],
    page: 0,
    pageCount: 1,
    pageText: '1 / 1',
    headTitle: '知乎热榜',
    headSub: '',
    status: '',
    auto: true,
    quoteTip: ''
  },

  onLoad() {
    var sources = [];
    for (var key in HOT_API) sources.push({ key: key, name: HOT_API[key].name });

    var codes = this.readCodes();
    var auto = true;
    try {
      var saved = wx.getStorageSync(AUTO_KEY);
      if (saved === '0') auto = false;
    } catch (e) {}
    this.codes = codes;
    this.items = [];
    this.pageCount = 1;

    this.setData({
      sources: sources,
      auto: auto,
      quoteTip: codes.length ? '自选：' + codes.join('、') : '还没有自选股'
    });

    this.tickClock();
    this.fetchHot();

    var self = this;
    this.clockTimer = setInterval(function () {
      self.tickClock();
    }, 1000);
    this.flipTimer = setInterval(function () {
      self.autoFlip();
    }, 15000);
    this.refreshTimer = setInterval(function () {
      self.autoRefresh();
    }, 60000);
  },

  onUnload() {
    if (this.clockTimer) clearInterval(this.clockTimer);
    if (this.flipTimer) clearInterval(this.flipTimer);
    if (this.refreshTimer) clearInterval(this.refreshTimer);
    this.clockTimer = null;
    this.flipTimer = null;
    this.refreshTimer = null;
  },

  readCodes() {
    try {
      var raw = wx.getStorageSync(CODES_KEY);
      if (!raw) return [];
      var list = typeof raw === 'string' ? JSON.parse(raw) : raw;
      return Array.isArray(list) ? list.slice(0, 6) : [];
    } catch (e) {
      return [];
    }
  },

  saveCodes() {
    try {
      wx.setStorageSync(CODES_KEY, JSON.stringify(this.codes));
    } catch (e) {}
    this.setData({
      quoteTip: this.codes.length ? '自选：' + this.codes.join('、') : '还没有自选股'
    });
  },

  tickClock() {
    var now = new Date();
    this.setData({
      clock: pad(now.getHours()) + ':' + pad(now.getMinutes()),
      dateText: now.getFullYear() + '-' + pad(now.getMonth() + 1) + '-' + pad(now.getDate()) +
        ' ' + ['周日', '周一', '周二', '周三', '周四', '周五', '周六'][now.getDay()]
    });
  },

  autoRefresh() {
    if (this.data.mode === 'hot') {
      this.refreshTick = (this.refreshTick || 0) + 1;
      if (this.refreshTick % 3 === 0) this.fetchHot();
    } else {
      this.fetchQuotes();
    }
  },

  autoFlip() {
    if (!this.data.auto || this.pageCount <= 1) return;
    var next = (this.data.page + 1) % this.pageCount;
    this.gotoPage(next);
  },

  fetchHot() {
    var self = this;
    var source = this.data.source;
    var api = HOT_API[source] || HOT_API.zhihu;
    this.setData({ status: '正在加载 ' + api.name + '…', headTitle: api.name });
    wx.request({
      url: api.url,
      method: 'GET',
      success(res) {
        var body = res.data;
        if (typeof body === 'string') {
          try {
            body = JSON.parse(body);
          } catch (e) {
            body = null;
          }
        }
        var list = res.statusCode === 200 ? parseHotBody(body) : null;
        if (!list || !list.length) {
          self.setData({ status: '热榜加载失败，稍后自动重试', headSub: '' });
          return;
        }
        self.items = list;
        self.renderItems(list, 'hot');
        self.setData({ status: list.length + ' 条 · 每 3 分钟刷新' });
      },
      fail() {
        self.setData({ status: '网络不可用，稍后自动重试' });
      }
    });
  },

  fetchQuotes() {
    var self = this;
    var secids = INDEX_SECIDS.slice();
    for (var i = 0; i < this.codes.length; i++) {
      var sid = secidOf(this.codes[i]);
      if (sid) secids.push(sid);
    }
    var url = 'https://push2.eastmoney.com/api/qt/ulist.np/get?fltt=2&invt=2&fields=f2,f3,f4,f12,f14&secids=' +
      secids.join(',');
    this.setData({ status: '正在拉取行情…', headTitle: '指数与自选' });
    wx.request({
      url: url,
      method: 'GET',
      success(res) {
        var body = res.data;
        if (typeof body === 'string') {
          try {
            body = JSON.parse(body);
          } catch (e) {
            body = null;
          }
        }
        var rows = res.statusCode === 200 ? buildQuoteRows(body, INDEX_SECIDS, self.codes) : [];
        if (!rows.length) {
          self.setData({ status: '行情加载失败，稍后自动重试', headSub: '' });
          return;
        }
        self.items = rows;
        self.renderItems(rows, 'quote');
        self.setData({ status: rows.length + ' 条 · 每分钟刷新' });
      },
      fail() {
        self.setData({ status: '网络不可用，稍后自动重试' });
      }
    });
  },

  renderItems(items, mode) {
    this.pageCount = Math.max(1, Math.ceil(items.length / PAGE_SIZE));
    var page = Math.min(this.data.page, this.pageCount - 1);
    this.renderPage(page, mode);
    this.setData({ headSub: '共 ' + items.length + ' 条' });
  },

  renderPage(page, mode) {
    var items = this.items || [];
    var start = page * PAGE_SIZE;
    var slice = items.slice(start, start + PAGE_SIZE);
    var rows = [];
    for (var i = 0; i < slice.length; i++) {
      var src = slice[i];
      rows.push({
        key: (mode === 'quote' ? 'q' : 'h') + (start + i),
        rank: start + i + 1,
        top: start + i < 3,
        title: truncate(src.title, mode === 'quote' ? 22 : 30),
        value: src.value || src.hot || '',
        valueCls: src.cls ? 'row-value ' + src.cls : 'row-value',
        cls: src.cls || '',
        url: src.url || '',
        code: src.code || '',
        name: src.name || ''
      });
    }
    this.setData({
      rows: rows,
      page: page,
      pageCount: this.pageCount,
      pageText: (page + 1) + ' / ' + this.pageCount
    });
  },

  gotoPage(page) {
    this.renderPage(page, this.data.mode);
  },

  onPrev() {
    if (this.pageCount <= 1) return;
    this.setData({ auto: false });
    this.saveAuto(false);
    this.gotoPage((this.data.page - 1 + this.pageCount) % this.pageCount);
  },

  onNext() {
    if (this.pageCount <= 1) return;
    this.setData({ auto: false });
    this.saveAuto(false);
    this.gotoPage((this.data.page + 1) % this.pageCount);
  },

  onToggleAuto() {
    var next = !this.data.auto;
    this.setData({ auto: next });
    this.saveAuto(next);
  },

  saveAuto(on) {
    try {
      wx.setStorageSync(AUTO_KEY, on ? '1' : '0');
    } catch (e) {}
  },

  onModeHot() {
    if (this.data.mode === 'hot') return;
    this.setData({ mode: 'hot', page: 0 });
    this.fetchHot();
  },

  onModeQuote() {
    if (this.data.mode === 'quote') return;
    this.setData({ mode: 'quote', page: 0 });
    this.fetchQuotes();
  },

  onSource(e) {
    var key = e.currentTarget.dataset.key;
    if (key === this.data.source) return;
    this.setData({ source: key, page: 0 });
    this.fetchHot();
  },

  onAddCode(e) {
    var raw = e.detail && e.detail.value ? e.detail.value : '';
    var code = String(raw).replace(/\D/g, '');
    if (code.length !== 6 && code.length !== 5) {
      wx.showToast({ title: '请输入 6 位 A 股或 5 位港股代码', icon: 'none' });
      return;
    }
    if (this.codes.indexOf(code) >= 0) {
      wx.showToast({ title: '已经在自选里了', icon: 'none' });
      return;
    }
    if (this.codes.length >= 6) {
      wx.showToast({ title: '最多 6 只自选', icon: 'none' });
      return;
    }
    this.codes.push(code);
    this.saveCodes();
    wx.showToast({ title: '已添加 ' + code, icon: 'none' });
    if (this.data.mode === 'quote') this.fetchQuotes();
  },

  onClearCodes() {
    if (!this.codes.length) return;
    this.codes = [];
    this.saveCodes();
    if (this.data.mode === 'quote') this.fetchQuotes();
  },

  onRow(e) {
    var url = e.currentTarget.dataset.url;
    if (!url) return;
    try {
      wx.setClipboardData({ data: url });
      wx.showToast({ title: '链接已复制', icon: 'none' });
    } catch (err) {}
  }
});
