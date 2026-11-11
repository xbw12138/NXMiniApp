// 直播电视 · 小程序入口
// 数据来源：M3U 直播源（内置采集源 + 用户自定义源），播放交给 <video> / MPV。
// 容器里每个页面是独立的 QuickJS 实例，globalData 不跨页共享，
// 所以所有状态都以本地存储为准，方法里每次重新读取。

// 内置源带多个镜像：第一个拉不动就自动换下一个，避免单点 CDN 慢或不通时卡住。
var DEFAULT_SOURCES = [
  {
    id: 'auto',
    name: '每日采集源',
    url: 'https://cdn.jsdelivr.net/gh/Guovin/iptv-api@gd/output/result.m3u',
    mirrors: [
      'https://gcore.jsdelivr.net/gh/Guovin/iptv-api@gd/output/result.m3u',
      'https://gh-proxy.com/https://raw.githubusercontent.com/Guovin/iptv-api/gd/output/result.m3u',
      'https://ghproxy.net/https://raw.githubusercontent.com/Guovin/iptv-api/gd/output/result.m3u'
    ],
    builtin: true,
    enabled: true
  },
  {
    id: 'iptvorg',
    name: 'iptv-org 中国源（备用）',
    url: 'https://cdn.jsdelivr.net/gh/iptv-org/iptv@master/streams/cn.m3u',
    mirrors: [
      'https://gcore.jsdelivr.net/gh/iptv-org/iptv@master/streams/cn.m3u'
    ],
    builtin: true,
    enabled: false
  }
];

// 冷启动（本地还没有任何缓存）时先渲染这份内置频道，让首屏立刻有内容，
// 联网拿到完整直播源后自动替换。它只存在于内存里，不会写进本地缓存。
var SEED_M3U = [
  "#EXTM3U",
  "#EXTM3U",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-1.png\",CCTV-1",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv1hd",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-2.png\",CCTV-2",
  "http://204.12.221.218:8181/3m1080p/cctv2.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-3.png\",CCTV-3",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv3hd",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-4.png\",CCTV-4",
  "http://74.91.26.218:82/live/cctv4hd.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-5.png\",CCTV-5",
  "http://107.150.60.122/live/cctv5hd.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-5+.png\",CCTV-5+",
  "http://38.75.136.137:98/gslb/dsdqbv/cctv5p.m3u8?auth=test20251009",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-6.png\",CCTV-6",
  "http://69.30.245.50/live/cctv6.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://i.imgur.com/CKcPMQC.png\",CCTV-7",
  "http://74.91.26.218:82/live/cctv7hd.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-8.png\",CCTV-8",
  "http://bztv.tvbus.cc:8081/cdnlive/cctv8.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-9.png\",CCTV-9",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv9hd",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-10.png\",CCTV-10",
  "http://38.75.136.137:98/gslb/dsdqbv/cctv10hd.m3u8?auth=test20251009",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-11.png\",CCTV-11",
  "http://74.91.26.218:82/live/cctv11hd.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-12.png\",CCTV-12",
  "http://107.150.60.122/live/cctv12hd.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-13.png\",CCTV-13",
  "http://38.75.136.137:98/gslb/dsdqbv/cctv13hd.m3u8?auth=test20251009",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-14.png\",CCTV-14",
  "http://198.204.228.26/live/cctv14hd.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-15.png\",CCTV-15",
  "http://204.12.221.218:8181/3m1080p/cctv15.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-16.png\",CCTV-16",
  "http://207.56.13.146:81/cdnlive/cctv16.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CCTV-17.png\",CCTV-17",
  "https://live.264788.xyz/channel/cctv17?streamid=348c8c0c04a6d2b900e5a3bcb4af8ce1&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CETV-1.png\",CETV-1",
  "http://198.204.228.26/live/cetv1hd.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CETV-2.png\",CETV-2",
  "http://gslbmgsplive.miguvideo.com/migu/kailu/cetv2/51/20240709/index.m3u8?msisdn=b637d3083747642f29b310fc5ba99367&mdspid=&spid=699004&netType=0&sid=5910022152&pid=2028597139&timestamp=20260827062127&Channel_ID=0116_2600034600-99000-201600010010028&ProgramID=923287211&ParentNodeID=-99&assertID=5910022152&client_ip=47.92.23.141&SecurityKey=20260827062127&promotionId=&mvid=5900006638&mcid=500020&playurlVersion=WX-A1-9.8.1-SNAPSHOT&userid=&jmhm=&videocodec=h264&appCode=miguvideo_android&bean=mgspad&tid=android&conFee=0&puData=6d02a32cc3b54abfae11971b15d053b8&ddCalcu=86bdv30a52a0aad3521cbc137b95141aebaf&sv=10004&ct=android",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/CETV-3.png\",CETV-3",
  "http://107.150.60.122/live/cetv3.m3u8",
  "#EXTINF:-1 group-title=\"央视频道\" tvg-logo=\"https://live.fanmingming.cn/tv/CETV4.png\",CETV-4",
  "http://rrs01.hw.gmcc.net:8088/PLTV/81/224/3221226724/1.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/广东卫视.png\",广东卫视",
  "https://txmov2.a.kwimgs.com/upic/2023/01/26/09/BMjAyMzAxMjYwOTE3NDVfNzM5MzQzNzIyXzk0NjI1OTUwNTA3XzBfMw==_b_B81ddda927d33445d544befd008e60109.mp4",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/香港卫视.png\",香港卫视",
  "http://cctvtxyh5c.liveplay.myqcloud.com/cstv/xianggangweishi_2/index.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/浙江卫视.png\",浙江卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=zjwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/湖南卫视.png\",湖南卫视",
  "https://txmov2.a.kwimgs.com/bs3/video-hls/5199687338554291078_hlsb.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/北京卫视.png\",北京卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=bjwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/湖北卫视.png\",湖北卫视",
  "http://107.150.60.122/live/hbwshd.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/黑龙江卫视.png\",黑龙江卫视",
  "http://107.150.60.122/live/hljwshd.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/安徽卫视.png\",安徽卫视",
  "http://107.150.60.122/live/ahwshd.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/重庆卫视.png\",重庆卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cqwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/东方卫视.png\",东方卫视",
  "http://38.75.136.137:98/gslb/dsdqbv/dfwshd.m3u8?auth=test20251009",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/东南卫视.png\",东南卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=dnwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/甘肃卫视.png\",甘肃卫视",
  "http://38.75.136.137:98/gslb/dsdqpub/gswshd.m3u8?auth=testpub",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/广西卫视.png\",广西卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gxwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/贵州卫视.png\",贵州卫视",
  "https://live.264788.xyz/channel/guizhouweishi?streamid=091bd59ea031989839c23459bb8cc9d4&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://gitee.com/suxuang/logo/raw/master/mylogo/海南卫视.png\",海南卫视",
  "http://120.76.248.139/live/bfgd/4200000473.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/河北卫视.png\",河北卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hewshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/河南卫视.png\",河南卫视",
  "http://107.150.60.122/live/hawshd.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/吉林卫视.png\",吉林卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jlwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/江苏卫视.png\",江苏卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jswshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/江西卫视.png\",江西卫视",
  "https://live.264788.xyz/channel/jiangxiweishi?streamid=555198f31caa33506ac6abb70d788959&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/辽宁卫视.png\",辽宁卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=lnwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/内蒙古卫视.png\",内蒙古卫视",
  "https://live.264788.xyz/channel/neimengguweishi?streamid=81a015dbecf3031d7d847b7a9b059f67&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/宁夏卫视.png\",宁夏卫视",
  "https://live.264788.xyz/channel/ningxiaweishi?streamid=e1236348f815c36a0429357e5b5c5f10&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/青海卫视.png\",青海卫视",
  "http://107.150.60.122/live/qhws.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/山东卫视.png\",山东卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=sdwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/山西卫视.png\",山西卫视",
  "https://live.264788.xyz/channel/shanxiweishi?streamid=eeb7d60ca6e0db131ea7a092a3cd28d2&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/陕西卫视.png\",陕西卫视",
  "https://live.264788.xyz/channel/shaanxiweishi?streamid=9dc887dba8ecb4e4143baf4f2373800c&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/四川卫视.png\",四川卫视",
  "https://live.264788.xyz/channel/sichuanweishi4k?streamid=21be5901ee81559c827dc84e46f26452&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/深圳卫视.png\",深圳卫视",
  "http://107.150.60.122/live/szwshd.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/三沙卫视.png\",三沙卫视",
  "http://107.150.60.122/live/ssws.m3u8",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/天津卫视.png\",天津卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=tjwshd",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/西藏卫视.png\",西藏卫视",
  "https://live.264788.xyz/channel/xizangweishi?streamid=4b4e8ea919f1cfd5e43deed1d1675251&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/新疆卫视.png\",新疆卫视",
  "https://live.264788.xyz/channel/xinjiangweishi?streamid=4f481f56ae39ce5d24203471de9df635&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk",
  "#EXTINF:-1 group-title=\"卫视频道\" tvg-logo=\"https://www.xn--rgv465a.top/tvlogo/云南卫视.png\",云南卫视",
  "http://63.141.230.178:82/gslb/zbdq5.m3u8?id=ynwshd"
].join('\n');

// 直播源内容缓存 6 小时：过期后先用旧数据秒开，再后台静默刷新。
var CACHE_TTL = 6 * 3600 * 1000;

var GROUP_ALIAS = {
  '央视频道': '央视',
  '卫视频道': '卫视',
  '体育频道': '体育',
  '电影频道': '电影',
  '电视剧频道': '电视剧',
  '动画频道': '动画',
  '少儿频道': '少儿',
  '音乐频道': '音乐',
  '纪录片频道': '纪录片',
  '4K频道': '4K',
  '港·澳·台': '港澳台',
  '港澳台': '港澳台',
  '更新时间': ''
};

var GROUP_ORDER = ['央视', '卫视', '港澳台', '体育', '电影', '电视剧', '动画', '少儿', '音乐', '纪录片', '4K'];

var SOURCE_KEY = 'ltv_sources';
var FAV_KEY = 'ltv_fav';
var RECENT_KEY = 'ltv_recent';
var DIRTY_KEY = 'ltv_dirty';

// 单个镜像最多等这么久：正常情况 396KB 在 1~3 秒内下完，
// 超时就换下一个镜像，而不是干等 30 秒。
var FETCH_TIMEOUT = 7000;
// 每个频道最多保留几条备用线路（同时决定缓存体积）。
var MAX_LINES = 4;
// 首个镜像起最多尝试几个地址。
var MAX_ATTEMPTS = 4;

App({
  onLaunch() {
    this.ensureSources();
    this.dropLegacyCache();
  },

  // 1.0.2 之前会把合并结果额外存一份 ltv_library（约 190KB）。
  // 容器每次写存储都重写整个文件，这份冗余数据会让所有写操作变慢，清掉。
  dropLegacyCache() {
    try {
      if (wx.getStorageSync('ltv_library')) wx.removeStorageSync('ltv_library');
    } catch (e) {}
  },

  // ---------- 直播源管理 ----------
  ensureSources() {
    var list = null;
    try { list = wx.getStorageSync(SOURCE_KEY); } catch (e) { list = null; }
    if (!Array.isArray(list) || list.length === 0) {
      list = DEFAULT_SOURCES.slice();
      try {
        wx.setStorageSync(SOURCE_KEY, list);
        wx.setStorageSync(DIRTY_KEY, 0);
      } catch (e) {}
    }
    return list;
  },

  getSources() {
    return this.ensureSources();
  },

  saveSources(list) {
    try {
      wx.setStorageSync(SOURCE_KEY, list);
      wx.setStorageSync(DIRTY_KEY, 1);
    } catch (e) {}
  },

  getEnabledSources() {
    return this.getSources().filter(function (s) { return s.enabled !== false; });
  },

  resetSources() {
    this.saveSources(DEFAULT_SOURCES.slice());
    return this.getSources();
  },

  addSource(name, url) {
    var list = this.getSources();
    var source = {
      id: 'src_' + Date.now(),
      name: name || '自定义直播源',
      url: url,
      builtin: false,
      enabled: true
    };
    list.push(source);
    this.saveSources(list);
    return source;
  },

  updateSource(id, patch) {
    var list = this.getSources();
    for (var i = 0; i < list.length; i++) {
      if (list[i].id !== id) continue;
      for (var k in patch) {
        if (Object.prototype.hasOwnProperty.call(patch, k)) list[i][k] = patch[k];
      }
      this.saveSources(list);
      return list[i];
    }
    return null;
  },

  removeSource(id) {
    var list = this.getSources().filter(function (s) { return s.id !== id; });
    this.saveSources(list);
  },

  // ---------- M3U 解析 ----------
  // 单块解析的行数：QuickJS 没有 JIT，一次解析上千行会把容器唯一的脚本线程
  // 占住几百毫秒到几秒，摇杆/十字键在这期间完全不响应。分块 + setTimeout(0)
  // 让出时间片，返回主线程处理输入，观感上就不卡了。
  PARSE_CHUNK: 120,

  // 同步解析，只用于内置的几十条种子频道。
  parsePlaylist(text) {
    var state = { pending: null, entries: [] };
    this.parseLines(String(text || '').split(/\r?\n/), 0, Infinity, state);
    return this.flattenEntries(state.entries);
  },

  // 逐行扫描文本而不是先 split：346KB 的源 split 一次就要上百毫秒，
  // 同样会卡住输入，这里把切行也拆到每个分块里。
  parsePlaylistChunked(text, done) {
    var self = this;
    var raw = String(text || '');
    var state = { pending: null, entries: [] };
    var pos = 0;
    var step = function () {
      if (self._unloaded) return;
      var count = 0;
      while (pos < raw.length && count < self.PARSE_CHUNK) {
        var nl = raw.indexOf('\n', pos);
        var line;
        if (nl < 0) {
          line = raw.slice(pos);
          pos = raw.length;
        } else {
          line = raw.slice(pos, nl);
          pos = nl + 1;
        }
        self.parseLine(line.trim(), state);
        count++;
      }
      if (pos < raw.length) setTimeout(step, 0);
      else done(self.flattenEntries(state.entries));
    };
    step();
  },

  parseLines(lines, from, to, state) {
    var end = Math.min(to, lines.length);
    for (var i = from; i < end; i++) this.parseLine(lines[i].trim(), state);
    return end;
  },

  parseLine(line, state) {
    if (!line) return;
    if (line.indexOf('#EXTINF') === 0) {
      state.pending = this.parseEntry(line);
      if (state.pending) state.entries.push(state.pending);
      return;
    }
    if (line.charAt(0) === '#') return;
    if (!state.pending) return;
    if (!state.pending.url) state.pending.url = line;
    else if (state.pending.extra.length < 6 && state.pending.url !== line) {
      state.pending.extra.push(line);
    }
  },

  flattenEntries(entries) {
    var out = [];
    for (var j = 0; j < entries.length; j++) {
      var e = entries[j];
      if (!e.url) continue;
      out.push({ name: e.name, group: e.group, logo: e.logo, url: e.url });
      for (var k = 0; k < e.extra.length; k++) {
        out.push({ name: e.name, group: e.group, logo: e.logo, url: e.extra[k] });
      }
    }
    return out;
  },

  parseEntry(line) {
    var body = line.slice(line.indexOf(':') + 1);
    var comma = body.lastIndexOf(',');
    if (comma < 0) return null;
    var meta = body.slice(0, comma);
    var name = this.cleanName(body.slice(comma + 1));
    if (!name) return null;
    // 采集类直播源会把「更新时间」当成一个频道塞进来，过滤掉。
    if (/^\d{4}-\d{2}-\d{2}/.test(name)) return null;
    var group = this.cleanGroup(this.attr(meta, 'group-title'));
    if (group === null) return null;
    if (!group) group = '其他';
    var logo = this.attr(meta, 'tvg-logo');
    if (logo && logo.indexOf('//') < 0) logo = '';
    return { name: name, group: group, logo: logo, url: '', extra: [] };
  },

  attr(meta, name) {
    // 之前每次调用都 new RegExp(...)：上千条频道就是几千次正则编译，
    // 在 QuickJS 上很贵。属性格式固定，直接按字符串找更快。
    var needle = name + '="';
    var start = meta.indexOf(needle);
    if (start < 0) return '';
    start += needle.length;
    var end = meta.indexOf('"', start);
    return end < 0 ? '' : meta.slice(start, end);
  },

  cleanName(raw) {
    var s = String(raw || '');
    s = s.replace(/\s*\[[^\]]*\]/g, ' ');
    s = s.replace(/\s*\((?:\d{3,4}p|4k|hd|sd|geo-blocked)\)/gi, ' ');
    s = s.replace(/\s+/g, ' ').replace(/^\s+|\s+$/g, '');
    return s;
  },

  cleanGroup(raw) {
    var s = String(raw || '').replace(/^\s+|\s+$/g, '');
    // 去掉分组名前面的 emoji / 符号，只保留中英文数字开头。
    s = s.replace(/^[^\u4e00-\u9fa5A-Za-z0-9]+/, '').trim();
    var key = s.replace(/\s+/g, '');
    if (Object.prototype.hasOwnProperty.call(GROUP_ALIAS, key)) {
      // 别名映射成空串表示整个分组都不要（例如采集时间戳）。
      return GROUP_ALIAS[key] || null;
    }
    s = s.replace(/频道$/, '');
    return s;
  },

  // ---------- 拉取与缓存 ----------
  cacheKey(sourceId) { return 'ltv_cache_' + sourceId; },

  readSourceCache(sourceId) {
    try {
      var entry = wx.getStorageSync(this.cacheKey(sourceId));
      if (!entry || !entry.list || !entry.list.length) return null;
      return entry;
    } catch (e) {
      return null;
    }
  },

  writeSourceCache(sourceId, entry) {
    try { wx.setStorageSync(this.cacheKey(sourceId), entry); } catch (e) {}
  },

  // 依次尝试主地址和镜像，谁先返回有效 M3U 就用谁。
  fetchChannels(source) {
    var self = this;
    var urls = [source.url];
    var mirrors = source.mirrors || [];
    for (var i = 0; i < mirrors.length; i++) {
      if (urls.indexOf(mirrors[i]) < 0) urls.push(mirrors[i]);
    }
    if (urls.length > MAX_ATTEMPTS) urls = urls.slice(0, MAX_ATTEMPTS);

    return new Promise(function (resolve, reject) {
      var lastError = null;
      var attempt = function (index) {
        if (index >= urls.length) {
          reject(lastError || new Error('直播源不可用'));
          return;
        }
        self.requestPlaylist(urls[index]).then(resolve, function (err) {
          lastError = err;
          attempt(index + 1);
        });
      };
      attempt(0);
    });
  },

  requestPlaylist(url) {
    var self = this;
    return new Promise(function (resolve, reject) {
      wx.request({
        url: url,
        method: 'GET',
        timeout: FETCH_TIMEOUT,
        header: { 'User-Agent': 'NXMiniApp/1.0', 'Accept': '*/*' },
        success: function (res) {
          var text = res.data;
          if (typeof text !== 'string') {
            reject(new Error('直播源返回了非文本内容（' + res.statusCode + '）'));
            return;
          }
          if (res.statusCode !== 200) {
            reject(new Error('直播源响应 ' + res.statusCode));
            return;
          }
          if (text.indexOf('#EXTM3U') < 0) {
            reject(new Error('不是有效的 M3U 直播源'));
            return;
          }
          // 分块解析，避免几千行的直播源把脚本线程整段占死（摇杆会没反应）。
          self.parsePlaylistChunked(text, function (list) {
            if (!list.length) {
              reject(new Error('直播源里没有可用频道'));
              return;
            }
            resolve(list);
          });
        },
        fail: function (err) {
          reject(new Error((err && err.errMsg) || '网络请求失败'));
        }
      });
    });
  },

  // 内置首屏频道：只在完全没有缓存时用，联网成功后会被真实数据替换。
  seedLibrary() {
    if (this._seed) return this._seed;
    var lib = this.mergeEntries([{ time: 0, list: this.parsePlaylist(SEED_M3U) }], 1);
    lib.seed = true;
    lib.updated = 0;
    this._seed = lib;
    return lib;
  },

  // 读取所有已启用源的缓存并合并（同步、不联网），播放页用它秒开。
  loadLibraryCached() {
    var self = this;
    var sources = self.getEnabledSources();
    var entries = sources.map(function (src) {
      return self.readSourceCache(src.id);
    });
    var lib = self.mergeEntries(entries, sources.length);
    return lib.total ? lib : self.seedLibrary();
  },

  // 加载直播库：force 为 true 时强制联网刷新。
  loadLibrary(opts) {
    opts = opts || {};
    var self = this;
    var sources = self.getEnabledSources();
    if (!sources.length) {
      return Promise.resolve({ channels: [], groups: [], total: 0, updated: 0, failed: 0, sourceCount: 0 });
    }

    var cachedList = sources.map(function (src) { return self.readSourceCache(src.id); });
    var anyCache = false;
    for (var i = 0; i < cachedList.length; i++) {
      if (cachedList[i]) { anyCache = true; break; }
    }

    var tasks = sources.map(function (src, index) {
      var cached = cachedList[index];
      var fresh = cached && (Date.now() - (cached.time || 0) < CACHE_TTL);
      if (fresh && !opts.force) return Promise.resolve(cached);

      var request = self.fetchChannels(src).then(function (list) {
        // 缓存按「已合并」的形态存（609 条 ≈ 173KB，比原始 1618 条省 90KB），
        // 容器每次写存储都会重写整个文件，体积越小启动越快。
        var channels = self.mergeEntries([{ time: 0, list: list }], 1).channels;
        var entry = { time: Date.now(), list: channels };
        self.writeSourceCache(src.id, entry);
        return entry;
      }).catch(function (err) {
        // 网络失败时退回旧缓存，避免整屏空掉；一条源挂了不影响其他源。
        if (cached) {
          cached.stale = true;
          cached.error = (err && err.message) || '刷新失败';
          return cached;
        }
        return { time: 0, list: [], failed: true };
      });

      if (cached) {
        // 有旧缓存：先用旧的渲染，后台刷新完再通知页面。
        request.then(function (entry) {
          if (opts.onRefresh && entry && !entry.failed) opts.onRefresh();
        }, function () {});
        return Promise.resolve(cached);
      }
      return request;
    });

    if (!anyCache && !opts.force) {
      // 冷启动：立刻返回内置频道，真实数据下载完成后回调页面替换。
      Promise.all(tasks).then(function (entries) {
        var lib = self.mergeEntries(entries, sources.length);
        if (opts.onRefresh && lib.total) opts.onRefresh();
        return lib;
      }).catch(function () {});
      return Promise.resolve(this.seedLibrary());
    }

    return Promise.all(tasks).then(function (entries) {
      return self.mergeEntries(entries, sources.length);
    });
  },

  mergeEntries(entries, sourceCount) {
    var map = {};
    var order = [];
    var latest = 0;
    var failed = 0;

    entries.forEach(function (entry) {
      if (!entry) { failed++; return; }
      if (entry.failed) failed++;
      if ((entry.time || 0) > latest) latest = entry.time || 0;
      (entry.list || []).forEach(function (c) {
        var key = c.group + '|' + c.name;
        var hit = map[key];
        if (!hit) {
          hit = { key: key, name: c.name, group: c.group, logo: c.logo || '', urls: [] };
          map[key] = hit;
          order.push(hit);
        } else if (!hit.logo && c.logo) {
          hit.logo = c.logo;
        }
        // 缓存里存的是已经合并过的频道（带 urls），原始解析结果（单条 url）也兼容。
        var lines = c.urls || (c.url ? [c.url] : []);
        for (var j = 0; j < lines.length && hit.urls.length < MAX_LINES; j++) {
          if (lines[j] && hit.urls.indexOf(lines[j]) < 0) hit.urls.push(lines[j]);
        }
      });
    });

    var groupMap = {};
    var groups = [];
    order.forEach(function (c) {
      if (!groupMap[c.group]) {
        groupMap[c.group] = { name: c.group, count: 0 };
        groups.push(groupMap[c.group]);
      }
      groupMap[c.group].count++;
    });
    groups.sort(function (a, b) {
      var ia = GROUP_ORDER.indexOf(a.name);
      var ib = GROUP_ORDER.indexOf(b.name);
      if (ia < 0) ia = 100;
      if (ib < 0) ib = 100;
      if (ia !== ib) return ia - ib;
      if (a.count !== b.count) return b.count - a.count;
      return a.name < b.name ? -1 : 1;
    });

    // 不再额外落盘一份合并结果：容器每次 setStorageSync 都会重写整个存储文件，
    // 多存一份 200KB 会让每次启动都多一次全量读+写。源缓存本身已经够用。
    return {
      channels: order,
      groups: groups,
      total: order.length,
      updated: latest,
      failed: failed,
      sourceCount: sourceCount
    };
  },

  // ---------- 收藏 / 最近观看 ----------
  getFavs() {
    try {
      var list = wx.getStorageSync(FAV_KEY);
      return Array.isArray(list) ? list : [];
    } catch (e) {
      return [];
    }
  },

  isFav(key) {
    return this.getFavs().indexOf(key) >= 0;
  },

  toggleFav(key) {
    var list = this.getFavs();
    var i = list.indexOf(key);
    if (i >= 0) list.splice(i, 1);
    else list.unshift(key);
    try { wx.setStorageSync(FAV_KEY, list); } catch (e) {}
    return i < 0;
  },

  getRecent() {
    try {
      var list = wx.getStorageSync(RECENT_KEY);
      return Array.isArray(list) ? list : [];
    } catch (e) {
      return [];
    }
  },

  addRecent(key) {
    if (!key) return;
    var list = this.getRecent();
    var i = list.indexOf(key);
    if (i >= 0) list.splice(i, 1);
    list.unshift(key);
    if (list.length > 24) list = list.slice(0, 24);
    try { wx.setStorageSync(RECENT_KEY, list); } catch (e) {}
  },

  clearRecent() {
    try { wx.setStorageSync(RECENT_KEY, []); } catch (e) {}
  },

  // ---------- 列表工具 ----------
  findChannel(channels, key) {
    for (var i = 0; i < (channels || []).length; i++) {
      if (channels[i].key === key) return channels[i];
    }
    return null;
  },

  // 按首页当前的分组/搜索条件重建列表，供播放页上下台使用。
  listForScope(channels, scope, keyword) {
    var self = this;
    var all = channels || [];
    var kw = (keyword || '').toLowerCase();
    if (kw) {
      return all.filter(function (c) {
        return c.name.toLowerCase().indexOf(kw) >= 0 || c.group.indexOf(kw) >= 0;
      });
    }
    if (scope === 'fav') {
      return self.getFavs().map(function (k) {
        return self.findChannel(all, k);
      }).filter(function (c) { return !!c; });
    }
    if (scope === 'recent') {
      return self.getRecent().map(function (k) {
        return self.findChannel(all, k);
      }).filter(function (c) { return !!c; });
    }
    if (scope && scope.indexOf('g:') === 0) {
      var g = scope.slice(2);
      return all.filter(function (c) { return c.group === g; });
    }
    return all.slice();
  },

  formatUpdated(ts) {
    if (!ts) return '尚未同步';
    var d = new Date(ts);
    function p(n) { return n < 10 ? '0' + n : '' + n; }
    var now = new Date();
    var sameDay = d.getFullYear() === now.getFullYear() &&
                  d.getMonth() === now.getMonth() && d.getDate() === now.getDate();
    if (sameDay) return '今天 ' + p(d.getHours()) + ':' + p(d.getMinutes());
    return (d.getMonth() + 1) + '-' + d.getDate() + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  },

  truncate(text, max) {
    var s = String(text || '');
    return s.length > max ? s.slice(0, max - 1) + '…' : s;
  }
});
