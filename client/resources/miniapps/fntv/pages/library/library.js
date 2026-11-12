var COLS = 5;

function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) rows.push(list.slice(i, i + cols));
  return rows;
}

Page({
  data: {
    id: '',
    name: '媒体库',
    items: [],
    rows: [],
    page: 1,
    pageCount: 1,
    total: 0,
    loading: true,
    errMsg: '',
    sortIndex: 0,
    sorts: [
      { key: 'SortName', label: '名称', order: 'Ascending' },
      { key: 'DateCreated', label: '最近添加', order: 'Descending' },
      { key: 'ProductionYear', label: '年份', order: 'Descending' },
      { key: 'CommunityRating', label: '评分', order: 'Descending' }
    ]
  },

  onLoad(query) {
    var id = query.id || '';
    var name = '媒体库';
    try { name = decodeURIComponent(query.name || '') || name; } catch (e) { name = query.name || name; }
    this.setData({ id: id, name: name });
    wx.setNavigationBarTitle({ title: name });
    if (!id) {
      this.setData({ loading: false, errMsg: '媒体库参数缺失' });
      return;
    }
    this.load(1);
  },

  load(page) {
    var self = this;
    var app = getApp();
    var size = app.pageSize();
    var sort = self.data.sorts[self.data.sortIndex] || self.data.sorts[0];
    self.setData({ loading: true, errMsg: '' });

    app.getLibraryItems(self.data.id, (page - 1) * size, size, sort.key, sort.order || 'Ascending')
      .then(function (result) {
        var items = result.items || [];
        var total = result.total || 0;
        self.setData({
          loading: false,
          items: items,
          rows: chunkRows(items, COLS),
          page: page,
          total: total,
          pageCount: Math.max(1, Math.ceil(total / size))
        });
      })
      .catch(function (err) {
        self.setData({
          loading: false,
          errMsg: (err && err.message) || '加载失败'
        });
      });
  },

  onRefresh() {
    this.load(this.data.page || 1);
  },

  onSort(e) {
    var index = Number(e.currentTarget.dataset.index);
    if (index === this.data.sortIndex) return;
    this.setData({ sortIndex: index });
    this.load(1);
  },

  prevPage() {
    if (this.data.page > 1) this.load(this.data.page - 1);
  },

  nextPage() {
    if (this.data.page < this.data.pageCount) this.load(this.data.page + 1);
  },

  onItemTap(e) {
    var id = e.currentTarget.dataset.id;
    if (!id) return;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + encodeURIComponent(id) });
  },

  goLogin() {
    getApp().clearConfig();
    wx.reLaunch({ url: '/pages/login/login' });
  }
});
