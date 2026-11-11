// 全部小程序：一次拉全量做前端分页，一行四个；点卡片先弹详情，确认后再启动。
const PAGE_SIZE = 12;
const COLS = 4;

// 容器的 Yoga 不支持 flex-wrap，按行切好再渲染。
function chunkRows(list, cols) {
  const rows = [];
  for (let i = 0; i < list.length; i += cols) {
    rows.push(list.slice(i, i + cols));
  }
  return rows;
}

function fmtSize(bytes) {
  const size = Number(bytes) || 0;
  if (size >= 1048576) return (size / 1048576).toFixed(1) + ' MB';
  if (size >= 1024) return Math.round(size / 1024) + ' KB';
  if (size > 0) return size + ' B';
  return '未知大小';
}

function fmtDate(ms) {
  const value = Number(ms) || 0;
  if (!value) return '';
  const d = new Date(value);
  const month = d.getMonth() + 1;
  const day = d.getDate();
  return d.getFullYear() + '-' + (month < 10 ? '0' + month : month) + '-' + (day < 10 ? '0' + day : day);
}

// 列表里只读卡片需要的展示字段，模板里不做字符串拼装。
function toCard(m) {
  const opens = Number(m.opens) || 0;
  return {
    id: m.id,
    title: m.title,
    icon_url: m.icon_url || '',
    category: m.category || '其他',
    versionText: m.version ? 'v' + m.version : '',
    opensText: '使用 ' + opens + ' 次',
    metaLine: (m.category || '其他') + (m.version ? ' · v' + m.version : ''),
    statLine: '使用 ' + opens + ' 次',
    developer: m.developer || '',
    description: m.description || m.summary || '暂无简介',
    statsText: '使用 ' + opens + ' 次 · 下载 ' + (Number(m.downloads) || 0) + ' 次 · ' + fmtSize(m.size),
    updateText: fmtDate(m.updated_at) ? '更新于 ' + fmtDate(m.updated_at) : ''
  };
}

Page({
  data: {
    rows: [],
    page: 1,
    totalPages: 1,
    status: '正在加载…',
    modal: null
  },

  onLoad() {
    this.all = [];
    this.loading = false;
    this.load(1);
  },

  // 优先走服务端分页（page/page_size）；老服务端会忽略参数一次返回全部，
  // 此时退回本地切片，保证两种服务端都能用。
  load(page) {
    if (this.loading) return;
    this.loading = true;
    this.setData({ status: '正在加载…' });
    wx.request({
      url: '/api/v1/miniapps',
      data: { page: page, page_size: PAGE_SIZE },
      success: (res) => {
        this.loading = false;
        const data = res.data || {};
        const cards = (data.items || []).map(toCard);
        if (typeof data.total === 'number') {
          this.serverPaged = true;
          this.all = cards;
          const totalPages = data.total_pages || Math.max(1, Math.ceil(data.total / PAGE_SIZE));
          this.show(cards, data.page || page, totalPages, data.total);
        } else {
          this.serverPaged = false;
          this.all = cards;
          this.showLocal(page);
        }
      },
      fail: () => {
        this.loading = false;
        this.setData({ status: '无法连接服务端，请检查网络' });
      }
    });
  },

  showLocal(page) {
    const total = this.all.length;
    const totalPages = Math.max(1, Math.ceil(total / PAGE_SIZE));
    const current = Math.min(Math.max(1, page), totalPages);
    this.show(this.all.slice((current - 1) * PAGE_SIZE, current * PAGE_SIZE), current, totalPages, total);
  },

  show(cards, page, totalPages, total) {
    this.cards = cards;

    // 不足一行的位置补占位块，保证每张卡片等宽。
    const rows = [];
    for (let i = 0; i < cards.length; i += COLS) {
      const items = cards.slice(i, i + COLS);
      const blanks = [];
      for (let b = items.length; b < COLS; b += 1) blanks.push(b);
      rows.push({ items: items, blanks: blanks });
    }

    this.setData({
      rows: rows,
      page: page,
      totalPages: totalPages,
      modal: null,
      status: total ? '共 ' + total + ' 个' : '暂无小程序'
    });
  },

  goto(page) {
    if (page < 1 || page > this.data.totalPages) return;
    if (this.serverPaged) {
      this.load(page);
    } else {
      this.showLocal(page);
    }
  },

  onPrev() {
    this.goto(this.data.page - 1);
  },

  onNext() {
    this.goto(this.data.page + 1);
  },

  // 先弹详情确认，避免误触直接启动。
  onCard(e) {
    const id = e.currentTarget.dataset.id;
    const app = (this.cards || []).filter((a) => a.id === id)[0];
    if (app) this.setData({ modal: app });
  },

  onCloseModal() {
    this.setData({ modal: null });
  },

  onConfirmLaunch() {
    const modal = this.data.modal;
    if (!modal) return;
    this.setData({ modal: null });
    wx.miniapp.launch(modal.id, modal.title || '');
  }
});
