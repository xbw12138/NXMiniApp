// 搜索页：默认展示精选小程序（优先用首页缓存秒开），输入后展示搜索结果。
const HOME_CACHE_KEY = 'home.cache.v1';
const DEBOUNCE_MS = 250;
const FEATURED_COLS = 2;

// 容器的 Yoga 不支持 flex-wrap，按行切好再渲染。
function chunkRows(list, cols) {
  const rows = [];
  for (let i = 0; i < list.length; i += cols) {
    rows.push(list.slice(i, i + cols));
  }
  return rows;
}

Page({
  data: {
    keyword: '',
    list: [],
    featuredRows: [],
    mode: 'featured',  // featured | result
    status: ''
  },

  onLoad() {
    this.loadFeatured();
  },

  onUnload() {
    if (this.timer) clearTimeout(this.timer);
  },

  // 精选列表和首页共用一份缓存，进来先出内容，再后台校准。
  loadFeatured() {
    let cached = null;
    try {
      cached = wx.getStorageSync(HOME_CACHE_KEY);
    } catch (err) {
      cached = null;
    }
    const cachedList = (cached && cached.featured) || [];
    this.setData({
      mode: 'featured',
      list: cachedList,
      featuredRows: chunkRows(cachedList, FEATURED_COLS),
      status: cachedList.length ? '精选 ' + cachedList.length + ' 个' : '正在加载…'
    });

    wx.request({
      url: '/api/v1/miniapp-home',
      success: (res) => {
        // 用户已经开始搜索，别再拿精选覆盖结果。
        if (this.data.keyword.trim()) return;
        const featured = (res.data && res.data.featured) || [];
        this.setData({
          list: featured,
          featuredRows: chunkRows(featured, FEATURED_COLS),
          status: featured.length ? '精选 ' + featured.length + ' 个' : '暂无精选小程序'
        });
      },
      fail: () => {
        if (this.data.keyword.trim()) return;
        this.setData({ status: this.data.list.length ? '' : '无法连接服务端' });
      }
    });
  },

  onInput(e) {
    const kw = e.detail.value;
    this.setData({ keyword: kw });
    if (this.timer) clearTimeout(this.timer);
    if (!kw.trim()) {
      this.loadFeatured();
      return;
    }
    // 边打边搜，250ms 防抖，避免每个字符都打一次接口。
    this.timer = setTimeout(() => {
      this.search(kw);
    }, DEBOUNCE_MS);
  },

  onConfirm(e) {
    if (this.timer) clearTimeout(this.timer);
    const kw = e.detail.value;
    this.setData({ keyword: kw });
    this.search(kw);
  },

  onSubmit() {
    if (this.timer) clearTimeout(this.timer);
    this.search(this.data.keyword);
  },

  search(keyword) {
    const kw = (keyword || '').trim();
    if (!kw) {
      this.loadFeatured();
      return;
    }
    this.setData({ mode: 'result', status: '搜索中…' });
    wx.request({
      url: '/api/v1/miniapps',
      data: { q: kw },
      success: (res) => {
        // 输入框已经改了，丢弃过期结果。
        if (this.data.keyword.trim() !== kw) return;
        const items = (res.data && res.data.items) || [];
        this.setData({
          list: items,
          status: items.length ? '共 ' + items.length + ' 个' : '没有找到「' + kw + '」'
        });
      },
      fail: () => {
        if (this.data.keyword.trim() !== kw) return;
        this.setData({ list: [], status: '无法连接服务端，请检查网络' });
      }
    });
  },

  onLaunch(e) {
    const ds = e.currentTarget.dataset;
    if (ds.id) wx.miniapp.launch(ds.id, ds.title || '');
  }
});
