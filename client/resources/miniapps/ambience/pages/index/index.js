// 客厅时钟 · 大屏环境屏
// 大字号时间 + 农历节气 + 实时天气 + 节日倒计时，纯原生绘制。

var WEEK = ['周日', '周一', '周二', '周三', '周四', '周五', '周六'];
var GAN = '甲乙丙丁戊己庚辛壬癸';
var ZHI = '子丑寅卯辰巳午未申酉戌亥';
var ZODIAC = '鼠牛虎兔龙蛇马羊猴鸡狗猪';
var LUNAR_MONTH = ['正', '二', '三', '四', '五', '六', '七', '八', '九', '十', '冬', '腊'];
var LUNAR_DAY = [
  '初一', '初二', '初三', '初四', '初五', '初六', '初七', '初八', '初九', '初十',
  '十一', '十二', '十三', '十四', '十五', '十六', '十七', '十八', '十九', '二十',
  '廿一', '廿二', '廿三', '廿四', '廿五', '廿六', '廿七', '廿八', '廿九', '三十'
];

// 24 节气（从小寒开始），值为相对 1900-01-06 02:05 的分钟偏移
var TERM_NAMES = ['小寒', '大寒', '立春', '雨水', '惊蛰', '春分', '清明', '谷雨',
  '立夏', '小满', '芒种', '夏至', '小暑', '大暑', '立秋', '处暑',
  '白露', '秋分', '寒露', '霜降', '立冬', '小雪', '大雪', '冬至'];
var TERM_INFO = [0, 21208, 42467, 63836, 85337, 107014, 128867, 150921,
  173149, 195551, 218072, 240693, 263343, 285989, 308563, 331033,
  353350, 375494, 397447, 419210, 440795, 462224, 483532, 504758];

// 农历年编码表：1900-2100
var LUNAR_INFO = [
  0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
  0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
  0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
  0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
  0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
  0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,
  0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
  0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,
  0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
  0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x055c0, 0x0ab60, 0x096d5, 0x092e0,
  0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
  0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
  0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
  0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
  0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
  0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
  0x0a2e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
  0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
  0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
  0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252,
  0x0d520
];

var CITIES = [
  { name: '北京', lat: 39.9042, lon: 116.4074 },
  { name: '上海', lat: 31.2304, lon: 121.4737 },
  { name: '广州', lat: 23.1291, lon: 113.2644 },
  { name: '深圳', lat: 22.5431, lon: 114.0579 },
  { name: '杭州', lat: 30.2741, lon: 120.1551 },
  { name: '南京', lat: 32.0603, lon: 118.7969 },
  { name: '成都', lat: 30.5728, lon: 104.0668 },
  { name: '重庆', lat: 29.5630, lon: 106.5516 },
  { name: '武汉', lat: 30.5928, lon: 114.3055 },
  { name: '西安', lat: 34.3416, lon: 108.9398 },
  { name: '天津', lat: 39.3434, lon: 117.3616 },
  { name: '苏州', lat: 31.2989, lon: 120.5853 },
  { name: '长沙', lat: 28.2282, lon: 112.9388 },
  { name: '郑州', lat: 34.7466, lon: 113.6254 },
  { name: '青岛', lat: 36.0671, lon: 120.3826 },
  { name: '厦门', lat: 24.4798, lon: 118.0894 },
  { name: '沈阳', lat: 41.8057, lon: 123.4315 },
  { name: '昆明', lat: 25.0389, lon: 102.7183 }
];

var CITY_KEY = 'amb_city';
var WEATHER_KEY = 'amb_weather';
var CACHE_MS = 30 * 60 * 1000;

function pad(n) {
  return n < 10 ? '0' + n : '' + n;
}

function lYearDays(y) {
  var sum = 348;
  for (var i = 0x8000; i > 0x8; i >>= 1) sum += (LUNAR_INFO[y - 1900] & i) ? 1 : 0;
  return sum + leapDays(y);
}

function leapMonth(y) {
  return LUNAR_INFO[y - 1900] & 0xf;
}

function leapDays(y) {
  if (leapMonth(y)) return (LUNAR_INFO[y - 1900] & 0x10000) ? 30 : 29;
  return 0;
}

function monthDays(y, m) {
  return (LUNAR_INFO[y - 1900] & (0x10000 >> m)) ? 30 : 29;
}

function solar2lunar(y, m, d) {
  var offset = Math.floor((Date.UTC(y, m - 1, d) - Date.UTC(1900, 0, 31)) / 86400000);
  var i;
  var temp = 0;
  for (i = 1900; i < 2101 && offset > 0; i++) {
    temp = lYearDays(i);
    offset -= temp;
  }
  if (offset < 0) {
    offset += temp;
    i--;
  }
  var year = i;
  var leap = leapMonth(year);
  var isLeap = false;
  for (i = 1; i < 13 && offset > 0; i++) {
    if (leap > 0 && i === leap + 1 && !isLeap) {
      --i;
      isLeap = true;
      temp = leapDays(year);
    } else {
      temp = monthDays(year, i);
    }
    if (isLeap && i === leap + 1) isLeap = false;
    offset -= temp;
  }
  if (offset === 0 && leap > 0 && i === leap + 1) {
    if (isLeap) {
      isLeap = false;
    } else {
      isLeap = true;
      --i;
    }
  }
  if (offset < 0) {
    offset += temp;
    --i;
  }
  return { year: year, month: i, day: offset + 1, leap: isLeap };
}

function lunar2solar(y, m, d, isLeap) {
  var offset = 0;
  var i;
  for (i = 1900; i < y; i++) offset += lYearDays(i);
  var leap = leapMonth(y);
  for (i = 1; i < m; i++) {
    offset += monthDays(y, i);
    if (leap > 0 && i === leap) offset += leapDays(y);
  }
  if (isLeap && leap > 0 && m === leap) offset += monthDays(y, m);
  offset += d - 1;
  var date = new Date(1900, 0, 31);
  date.setDate(date.getDate() + offset);
  return date;
}

function ganzhiYear(y) {
  var idx = (y - 1900 + 36) % 60;
  if (idx < 0) idx += 60;
  return GAN.charAt(idx % 10) + ZHI.charAt(idx % 12);
}

function zodiacOf(y) {
  var idx = (y - 4) % 12;
  if (idx < 0) idx += 12;
  return ZODIAC.charAt(idx);
}

function lunarText(y, m, d) {
  var l = solar2lunar(y, m, d);
  var month = (l.leap ? '闰' : '') + LUNAR_MONTH[l.month - 1] + '月';
  return ganzhiYear(l.year) + '年（' + zodiacOf(l.year) + '） ' + month + LUNAR_DAY[l.day - 1];
}

function termDate(y, n) {
  var ms = 31556925974.7 * (y - 1900) + TERM_INFO[n] * 60000 + Date.UTC(1900, 0, 6, 2, 5);
  return new Date(y, Math.floor(n / 2), new Date(ms).getUTCDate());
}

function dayStart(date) {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate());
}

function daysBetween(target, base) {
  return Math.round((dayStart(target) - dayStart(base)) / 86400000);
}

function termText(now) {
  var y = now.getFullYear();
  var n;
  for (n = 0; n < 24; n++) {
    var t = termDate(y, n);
    if (t.getMonth() === now.getMonth() && t.getDate() === now.getDate()) {
      return '今日 ' + TERM_NAMES[n];
    }
  }
  var next = null;
  var nextName = '';
  for (n = 0; n < 48; n++) {
    var yy = y + Math.floor(n / 24);
    var idx = n % 24;
    var d = termDate(yy, idx);
    if (d > dayStart(now)) {
      next = d;
      nextName = TERM_NAMES[idx];
      break;
    }
  }
  if (!next) return '';
  return '距 ' + nextName + ' 还有 ' + daysBetween(next, now) + ' 天';
}

function buildCountdowns(now) {
  var y = now.getFullYear();
  var l = solar2lunar(now.getFullYear(), now.getMonth() + 1, now.getDate());
  var ly = l.year;

  var newYear = new Date(y, 0, 1);
  if (daysBetween(newYear, now) < 0) newYear = new Date(y + 1, 0, 1);

  var spring = lunar2solar(ly, 1, 1, false);
  if (daysBetween(spring, now) < 0) spring = lunar2solar(ly + 1, 1, 1, false);

  var mid = lunar2solar(ly, 8, 15, false);
  if (daysBetween(mid, now) < 0) mid = lunar2solar(ly + 1, 8, 15, false);

  var wd = now.getDay();
  var weekend = wd === 0 || wd === 6 ? 0 : 6 - wd;

  return [
    { label: '距离元旦', days: daysBetween(newYear, now) },
    { label: '距离春节', days: daysBetween(spring, now) },
    { label: '距离中秋', days: daysBetween(mid, now) },
    { label: '距离周末', days: weekend }
  ];
}

function weatherText(code) {
  var map = {
    0: '晴', 1: '晴间多云', 2: '多云', 3: '阴',
    45: '有雾', 48: '雾凇',
    51: '小毛毛雨', 53: '毛毛雨', 55: '大毛毛雨',
    56: '冻毛毛雨', 57: '强冻毛毛雨',
    61: '小雨', 63: '中雨', 65: '大雨',
    66: '冻雨', 67: '强冻雨',
    71: '小雪', 73: '中雪', 75: '大雪', 77: '雪粒',
    80: '阵雨', 81: '强阵雨', 82: '暴雨',
    85: '阵雪', 86: '强阵雪',
    95: '雷阵雨', 96: '雷阵雨伴冰雹', 99: '强雷暴冰雹'
  };
  return map[code] || '未知';
}

function dayLabel(text) {
  if (!text) return '';
  var parts = text.split('-');
  var d = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
  return WEEK[d.getDay()];
}

function chunk(list, size) {
  var rows = [];
  var row = [];
  for (var i = 0; i < list.length; i++) {
    row.push(list[i]);
    if (row.length === size) {
      rows.push(row);
      row = [];
    }
  }
  if (row.length) rows.push(row);
  return rows;
}

Page({
  data: {
    time: '--:--',
    sec: '--',
    dateText: '',
    weekday: '',
    lunarText: '',
    termText: '',
    shift: 's0',
    city: '北京',
    temp: '--',
    cond: '',
    feels: '--',
    humidity: '--',
    hasWeather: false,
    weatherErr: '',
    forecast: [],
    updated: '',
    countdowns: [],
    picking: false,
    cityRows: [],
    status: '',
    cityList: []
  },

  onLoad() {
    var names = [];
    for (var i = 0; i < CITIES.length; i++) names.push(CITIES[i].name);
    this.cityList = CITIES;
    this.setData({ cityRows: chunk(names, 4), cityList: names });

    var saved = this.readCity();
    this.cityInfo = saved;
    this.setData({ city: saved.name });

    this.tick();
    this.updateCalendar();
    this.applyCache();

    var self = this;
    this.clockTimer = setInterval(function () {
      self.tick();
    }, 1000);
    this.calendarTimer = setInterval(function () {
      self.updateCalendar();
    }, 60000);
    this.weatherTimer = setInterval(function () {
      self.fetchWeather();
    }, CACHE_MS);
    this.shiftTimer = setInterval(function () {
      self.nextShift();
    }, 60000);

    this.fetchWeather();
  },

  onUnload() {
    if (this.clockTimer) clearInterval(this.clockTimer);
    if (this.calendarTimer) clearInterval(this.calendarTimer);
    if (this.weatherTimer) clearInterval(this.weatherTimer);
    if (this.shiftTimer) clearInterval(this.shiftTimer);
    this.clockTimer = null;
    this.calendarTimer = null;
    this.weatherTimer = null;
    this.shiftTimer = null;
  },

  readCity() {
    try {
      var raw = wx.getStorageSync(CITY_KEY);
      if (raw) {
        var obj = typeof raw === 'string' ? JSON.parse(raw) : raw;
        if (obj && obj.name && obj.lat && obj.lon) return obj;
      }
    } catch (e) {}
    return CITIES[0];
  },

  tick() {
    var now = new Date();
    this.setData({
      time: pad(now.getHours()) + ':' + pad(now.getMinutes()),
      sec: pad(now.getSeconds())
    });
  },

  updateCalendar() {
    var now = new Date();
    var week = WEEK[now.getDay()];
    this.setData({
      dateText: now.getFullYear() + ' 年 ' + (now.getMonth() + 1) + ' 月 ' + now.getDate() + ' 日',
      weekday: week,
      lunarText: lunarText(now.getFullYear(), now.getMonth() + 1, now.getDate()),
      termText: termText(now),
      countdowns: buildCountdowns(now)
    });
  },

  nextShift() {
    var order = ['s0', 's1', 's2', 's3'];
    var idx = order.indexOf(this.data.shift);
    this.setData({ shift: order[(idx + 1) % 4] });
  },

  applyCache() {
    try {
      var raw = wx.getStorageSync(WEATHER_KEY);
      if (!raw) return;
      var cache = typeof raw === 'string' ? JSON.parse(raw) : raw;
      if (cache && cache.city === this.data.city && cache.data) {
        this.applyWeather(cache.data, cache.ts);
      }
    } catch (e) {}
  },

  fetchWeather() {
    var self = this;
    var c = this.cityInfo || CITIES[0];
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + c.lat +
      '&longitude=' + c.lon +
      '&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m' +
      '&daily=weather_code,temperature_2m_max,temperature_2m_min' +
      '&timezone=Asia%2FShanghai&forecast_days=4';
    this.setData({ status: '正在更新天气…' });
    wx.request({
      url: url,
      method: 'GET',
      success(res) {
        if (res.statusCode !== 200) {
          self.setData({ status: '天气更新失败', weatherErr: '接口返回 ' + res.statusCode });
          return;
        }
        var data = res.data;
        if (typeof data === 'string') {
          try {
            data = JSON.parse(data);
          } catch (e) {
            self.setData({ status: '天气更新失败', weatherErr: '数据解析失败' });
            return;
          }
        }
        if (!data || !data.current) {
          self.setData({ status: '天气更新失败', weatherErr: '数据不完整' });
          return;
        }
        self.applyWeather(data, Date.now());
        self.saveWeather(data);
      },
      fail() {
        self.setData({ status: '网络不可用', weatherErr: '天气需要联网获取' });
      }
    });
  },

  saveWeather(data) {
    try {
      wx.setStorageSync(WEATHER_KEY, JSON.stringify({
        city: this.data.city,
        ts: Date.now(),
        data: data
      }));
    } catch (e) {}
  },

  applyWeather(data, ts) {
    var cur = data.current || {};
    var daily = data.daily || {};
    var forecast = [];
    var times = daily.time || [];
    for (var i = 1; i < times.length && i < 4; i++) {
      forecast.push({
        day: dayLabel(times[i]),
        hi: Math.round(daily.temperature_2m_max[i]),
        lo: Math.round(daily.temperature_2m_min[i]),
        cond: weatherText(daily.weather_code[i])
      });
    }
    var when = ts ? new Date(ts) : new Date();
    this.setData({
      temp: Math.round(cur.temperature_2m),
      feels: Math.round(cur.apparent_temperature),
      humidity: Math.round(cur.relative_humidity_2m),
      cond: weatherText(cur.weather_code),
      wind: Math.round(cur.wind_speed_10m),
      forecast: forecast,
      hasWeather: true,
      weatherErr: '',
      updated: pad(when.getHours()) + ':' + pad(when.getMinutes()) + ' 更新',
      status: '天气已更新'
    });
  },

  onPickCity() {
    this.setData({ picking: true });
  },

  onClosePick() {
    this.setData({ picking: false });
  },

  onChooseCity(e) {
    var name = e.currentTarget.dataset.name;
    var picked = CITIES[0];
    for (var i = 0; i < CITIES.length; i++) {
      if (CITIES[i].name === name) {
        picked = CITIES[i];
        break;
      }
    }
    this.cityInfo = picked;
    this.setData({ city: picked.name, picking: false, hasWeather: false });
    try {
      wx.setStorageSync(CITY_KEY, JSON.stringify(picked));
    } catch (err) {}
    this.fetchWeather();
  },

  onCustomCity(e) {
    var name = (e.detail && e.detail.value ? e.detail.value : '').trim();
    if (!name) return;
    var self = this;
    wx.showLoading({ title: '查找城市' });
    wx.request({
      url: 'https://geocoding-api.open-meteo.com/v1/search?name=' + encodeURIComponent(name) +
        '&count=1&language=zh&format=json',
      method: 'GET',
      success(res) {
        wx.hideLoading();
        var data = res.data;
        if (typeof data === 'string') {
          try {
            data = JSON.parse(data);
          } catch (e) {
            data = null;
          }
        }
        var hit = data && data.results && data.results[0];
        if (!hit) {
          wx.showToast({ title: '没找到这个城市', icon: 'none' });
          return;
        }
        var picked = { name: hit.name, lat: hit.latitude, lon: hit.longitude };
        self.cityInfo = picked;
        self.setData({ city: picked.name, picking: false, hasWeather: false });
        try {
          wx.setStorageSync(CITY_KEY, JSON.stringify(picked));
        } catch (err) {}
        self.fetchWeather();
      },
      fail() {
        wx.hideLoading();
        wx.showToast({ title: '网络不可用', icon: 'none' });
      }
    });
  }
});
