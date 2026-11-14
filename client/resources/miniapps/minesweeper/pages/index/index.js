// 扫雷 · Canvas 单画布版
// 操作：方向键 / 左摇杆移动光标，A 执行；X 切换「挖开 / 插旗」；
//       Y / Start 重开，LB / RB 切换难度；触摸可直接点格子与屏幕按钮。

var W = 1280;
var H = 720;
var SIZE = 10;
var CELL = 48;
var PITCH = 52;
var BOARD_X = 76;
var BOARD_Y = 152;
var BOARD_W = SIZE * PITCH - (PITCH - CELL);
var PANEL_X = 640;
var PANEL_W = 564;
var PANEL_CARD = 180;
var PANEL_GAP = 12;
var DIFF_X = 844;
var DIFF_W = 124;
var DIFF_STEP = 132;
var DIFF_Y = 40;
var DIFF_H = 54;
var MODE_Y = 272;
var MODE_H = 76;
var RESTART_Y = 368;
var RESTART_H = 72;
var OVERLAY_BTN = { x: 520, y: 430, w: 240, h: 68 };
var REPEAT_FIRST = 0.2;
var REPEAT_NEXT = 0.14;

var LEVELS = [
  { key: 'easy', label: '初级', mines: 10 },
  { key: 'mid', label: '中级', mines: 20 },
  { key: 'hard', label: '高级', mines: 32 }
];

var NUM_COLORS = [
  '#6aa9ff', '#5fd39a', '#ff8a8a', '#b48cff',
  '#ffb066', '#5fd9d3', '#d9dee6', '#9aa5b5'
];

Page({
  data: {},

  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');

    this.lastTs = 0;
    this.flagMode = false;
    this.cursor = { r: 4, c: 4 };
    this.toastText = '';
    this.toastT = 0;
    this.moveDir = '';
    this.moveCd = 0;
    this.prevPad = {
      a: false, x: false, y: false,
      up: false, down: false, left: false, right: false,
      lb: false, rb: false, start: false
    };

    this.reset('easy');

    var loop = function (ts) {
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  // ---------------- 游戏状态 ----------------

  levelOf: function (key) {
    for (var i = 0; i < LEVELS.length; i++) {
      if (LEVELS[i].key === key) return LEVELS[i];
    }
    return LEVELS[0];
  },

  fmt: function (sec) {
    var s = Math.floor(sec);
    var m = Math.floor(s / 60);
    var ss = s % 60;
    return (m < 10 ? '0' + m : '' + m) + ':' + (ss < 10 ? '0' + ss : '' + ss);
  },

  reset: function (key) {
    this.level = key;
    this.mineCount = this.levelOf(key).mines;
    this.grid = [];
    for (var i = 0; i < SIZE * SIZE; i++) {
      this.grid.push({ mine: false, open: false, flag: false, n: 0 });
    }
    this.started = false;
    this.over = false;
    this.win = false;
    this.seconds = 0;
    this.flagTotal = 0;
    this.boomIndex = -1;
    this.cursor = { r: 4, c: 4 };
    this.moveDir = '';
    this.moveCd = 0;
    this.toastText = '';
    this.toastT = 0;

    var best = Number(wx.getStorageSync('best_' + key) || 0) || 0;
    this.best = best;
    this.bestText = best ? this.fmt(best) : '--';
  },

  seedMines: function (safe) {
    var pool = [];
    var sr = Math.floor(safe / SIZE);
    var sc = safe % SIZE;
    for (var i = 0; i < SIZE * SIZE; i++) {
      if (Math.abs(Math.floor(i / SIZE) - sr) <= 1 &&
          Math.abs((i % SIZE) - sc) <= 1) continue;
      pool.push(i);
    }
    for (var k = pool.length - 1; k > 0; k--) {
      var j = Math.floor(Math.random() * (k + 1));
      var t = pool[k];
      pool[k] = pool[j];
      pool[j] = t;
    }
    var n = Math.min(this.mineCount, pool.length);
    for (var m = 0; m < n; m++) this.grid[pool[m]].mine = true;
    for (var p = 0; p < SIZE * SIZE; p++) {
      this.grid[p].n = this.neighborMines(p);
    }
  },

  neighborMines: function (i) {
    var r = Math.floor(i / SIZE);
    var c = i % SIZE;
    var count = 0;
    for (var dr = -1; dr <= 1; dr++) {
      for (var dc = -1; dc <= 1; dc++) {
        if (dr === 0 && dc === 0) continue;
        var nr = r + dr;
        var nc = c + dc;
        if (nr < 0 || nc < 0 || nr >= SIZE || nc >= SIZE) continue;
        if (this.grid[nr * SIZE + nc].mine) count++;
      }
    }
    return count;
  },

  showToast: function (text) {
    this.toastText = text;
    this.toastT = 1.2;
  },

  toggleMode: function () {
    if (this.over) return;
    this.flagMode = !this.flagMode;
    this.showToast(this.flagMode ? '插旗模式' : '挖开模式');
    wx.vibrateShort({ type: 'light' });
  },

  switchLevel: function (key) {
    if (key === this.level) return;
    this.reset(key);
    wx.vibrateShort({ type: 'light' });
  },

  cycleLevel: function (dir) {
    var idx = 0;
    for (var i = 0; i < LEVELS.length; i++) {
      if (LEVELS[i].key === this.level) idx = i;
    }
    idx = (idx + dir + LEVELS.length) % LEVELS.length;
    this.reset(LEVELS[idx].key);
    wx.vibrateShort({ type: 'light' });
  },

  restart: function () {
    this.reset(this.level);
  },

  digCell: function (i) {
    if (this.over) return;
    var g = this.grid[i];
    if (!g || g.open) return;
    if (g.flag) {
      this.showToast('这里有旗子');
      return;
    }
    if (!this.started) {
      this.seedMines(i);
      this.started = true;
    }
    if (g.mine) {
      this.boomIndex = i;
      this.lose();
      return;
    }
    this.reveal(i);
    wx.vibrateShort({ type: 'light' });
    this.checkWin();
  },

  toggleFlag: function (i) {
    if (this.over) return;
    var g = this.grid[i];
    if (!g || g.open) return;
    if (g.flag) {
      g.flag = false;
      this.flagTotal--;
    } else {
      if (this.flagTotal >= this.mineCount) {
        this.showToast('旗子用完了');
        return;
      }
      g.flag = true;
      this.flagTotal++;
    }
    wx.vibrateShort({ type: 'light' });
  },

  reveal: function (start) {
    var stack = [start];
    while (stack.length) {
      var i = stack.pop();
      var g = this.grid[i];
      if (g.open || g.flag || g.mine) continue;
      g.open = true;
      g.n = this.neighborMines(i);
      if (g.n === 0) {
        var r = Math.floor(i / SIZE);
        var c = i % SIZE;
        for (var dr = -1; dr <= 1; dr++) {
          for (var dc = -1; dc <= 1; dc++) {
            if (dr === 0 && dc === 0) continue;
            var nr = r + dr;
            var nc = c + dc;
            if (nr < 0 || nc < 0 || nr >= SIZE || nc >= SIZE) continue;
            var ni = nr * SIZE + nc;
            if (!this.grid[ni].open && !this.grid[ni].flag) stack.push(ni);
          }
        }
      }
    }
  },

  checkWin: function () {
    var opened = 0;
    for (var i = 0; i < SIZE * SIZE; i++) {
      if (this.grid[i].open && !this.grid[i].mine) opened++;
    }
    if (opened !== SIZE * SIZE - this.mineCount) return;

    this.over = true;
    this.win = true;
    for (var k = 0; k < SIZE * SIZE; k++) {
      if (this.grid[k].mine) this.grid[k].flag = true;
    }
    this.flagTotal = this.mineCount;

    var t = Math.floor(this.seconds);
    if (t < 1) t = 1;
    if (!this.best || t < this.best) {
      this.best = t;
      wx.setStorageSync('best_' + this.level, t);
    }
    this.bestText = this.fmt(this.best);
    this.boomIndex = -1;
    wx.vibrateShort({ type: 'light' });
  },

  lose: function () {
    this.over = true;
    this.win = false;
    for (var i = 0; i < SIZE * SIZE; i++) {
      if (this.grid[i].mine) this.grid[i].open = true;
    }
    wx.vibrateShort({ type: 'heavy' });
  },

  // ---------------- 输入 ----------------

  readPad: function () {
    var pad = wx.getGamepadState() || {};
    var b = pad.buttons || {};
    var ax = pad.axes || [0, 0];
    return {
      a: !!b.a,
      x: !!b.x,
      y: !!b.y,
      up: !!b.up,
      down: !!b.down,
      left: !!b.left,
      right: !!b.right,
      lb: !!b.lb,
      rb: !!b.rb,
      start: !!b.start,
      lx: ax[0] || 0,
      ly: ax[1] || 0
    };
  },

  edge: function (pad, name) {
    var now = !!pad[name];
    var was = !!this.prevPad[name];
    this.prevPad[name] = now;
    return now && !was;
  },

  handlePad: function (dt) {
    var pad = this.readPad();

    var pressA = this.edge(pad, 'a');
    var pressX = this.edge(pad, 'x');
    var pressY = this.edge(pad, 'y');
    this.edge(pad, 'up');
    this.edge(pad, 'down');
    this.edge(pad, 'left');
    this.edge(pad, 'right');
    var pressLb = this.edge(pad, 'lb');
    var pressRb = this.edge(pad, 'rb');
    var pressStart = this.edge(pad, 'start');

    if (pressA) {
      if (this.over) this.restart();
      else this.actAtCursor();
    }
    if (pressX) this.toggleMode();
    if (pressY && this.over) this.restart();
    if (pressStart) this.restart();
    if (pressLb) this.cycleLevel(-1);
    if (pressRb) this.cycleLevel(1);

    var dx = 0;
    var dy = 0;
    if (pad.left) dx = -1;
    else if (pad.right) dx = 1;
    if (pad.up) dy = -1;
    else if (pad.down) dy = 1;

    if (dx === 0 && dy === 0) {
      var ax = pad.lx;
      var ay = pad.ly;
      if (Math.abs(ax) > 0.55 || Math.abs(ay) > 0.55) {
        if (Math.abs(ax) >= Math.abs(ay)) dx = ax < 0 ? -1 : 1;
        else dy = ay < 0 ? -1 : 1;
      }
    }

    if (dx !== 0 || dy !== 0) {
      var dirKey = dx + ',' + dy;
      if (dirKey !== this.moveDir) {
        this.moveDir = dirKey;
        this.moveCursor(dx, dy);
        this.moveCd = REPEAT_FIRST;
      } else {
        this.moveCd -= dt;
        if (this.moveCd <= 0) {
          this.moveCursor(dx, dy);
          this.moveCd = REPEAT_NEXT;
        }
      }
    } else {
      this.moveDir = '';
      this.moveCd = 0;
    }
  },

  moveCursor: function (dx, dy) {
    this.cursor.r = Math.max(0, Math.min(SIZE - 1, this.cursor.r + dy));
    this.cursor.c = Math.max(0, Math.min(SIZE - 1, this.cursor.c + dx));
  },

  actAtCursor: function () {
    var i = this.cursor.r * SIZE + this.cursor.c;
    if (this.flagMode) this.toggleFlag(i);
    else this.digCell(i);
  },

  onTouchStart: function (e) {
    var x = e.detail.x;
    var y = e.detail.y;

    if (this.over) {
      var ob = OVERLAY_BTN;
      if (x >= ob.x && x <= ob.x + ob.w && y >= ob.y && y <= ob.y + ob.h) {
        this.restart();
      }
      return;
    }

    // 难度按钮
    for (var i = 0; i < LEVELS.length; i++) {
      var bx = DIFF_X + i * DIFF_STEP;
      if (x >= bx && x <= bx + DIFF_W && y >= DIFF_Y && y <= DIFF_Y + DIFF_H) {
        this.switchLevel(LEVELS[i].key);
        return;
      }
    }

    // 模式按钮
    if (x >= PANEL_X && x <= PANEL_X + PANEL_W &&
        y >= MODE_Y && y <= MODE_Y + MODE_H) {
      this.toggleMode();
      return;
    }

    // 重新开始按钮
    if (x >= PANEL_X && x <= PANEL_X + PANEL_W &&
        y >= RESTART_Y && y <= RESTART_Y + RESTART_H) {
      this.restart();
      return;
    }

    // 雷区
    if (x >= BOARD_X && x <= BOARD_X + BOARD_W &&
        y >= BOARD_Y && y <= BOARD_Y + BOARD_W) {
      var c = Math.floor((x - BOARD_X) / PITCH);
      var r = Math.floor((y - BOARD_Y) / PITCH);
      c = Math.max(0, Math.min(SIZE - 1, c));
      r = Math.max(0, Math.min(SIZE - 1, r));
      this.cursor = { r: r, c: c };
      this.actAtCursor();
    }
  },

  onTouchMove: function () {},

  onTouchEnd: function () {},

  // ---------------- 循环 ----------------

  tick: function (ts) {
    var dt = this.lastTs ? (ts - this.lastTs) / 1000 : 0.016;
    this.lastTs = ts;
    if (dt > 0.05) dt = 0.05;
    if (dt <= 0) dt = 0.016;

    this.update(dt);
    this.draw();
  },

  update: function (dt) {
    if (this.toastT > 0) this.toastT -= dt;
    this.handlePad(dt);
    if (this.started && !this.over) this.seconds += dt;
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;

    ctx.setFillStyle('#0f1117');
    ctx.fillRect(0, 0, W, H);

    this.drawHeader(ctx);
    this.drawBoard(ctx);
    this.drawPanel(ctx);
    this.drawToast(ctx);
    if (this.over) this.drawOverlay(ctx);

    ctx.draw();
  },

  drawHeader: function (ctx) {
    ctx.setTextAlign('left');
    ctx.setFillStyle('#f4f7fb');
    ctx.setFontSize(54);
    ctx.fillText('扫雷', 64, 82);

    ctx.setFillStyle('#6f798a');
    ctx.setFontSize(22);
    ctx.fillText('小心每一步', 66, 112);

    for (var i = 0; i < LEVELS.length; i++) {
      var x = DIFF_X + i * DIFF_STEP;
      var on = LEVELS[i].key === this.level;
      ctx.setFillStyle(on ? '#2b3f63' : '#191e28');
      ctx.fillRect(x, DIFF_Y, DIFF_W, DIFF_H);
      ctx.setLineWidth(2);
      ctx.setStrokeStyle(on ? '#4d7cff' : '#2a3342');
      ctx.strokeRect(x, DIFF_Y, DIFF_W, DIFF_H);
      ctx.setTextAlign('center');
      ctx.setFillStyle(on ? '#ffffff' : '#a7b1c1');
      ctx.setFontSize(24);
      ctx.fillText(LEVELS[i].label + ' ' + LEVELS[i].mines,
                   x + DIFF_W / 2, DIFF_Y + 36);
    }
    ctx.setTextAlign('left');
  },

  drawBoard: function (ctx) {
    ctx.setFillStyle('#141a24');
    ctx.fillRect(BOARD_X - 8, BOARD_Y - 8, BOARD_W + 16, BOARD_W + 16);
    ctx.setLineWidth(2);
    ctx.setStrokeStyle('#232c3a');
    ctx.strokeRect(BOARD_X - 8, BOARD_Y - 8, BOARD_W + 16, BOARD_W + 16);

    for (var r = 0; r < SIZE; r++) {
      for (var c = 0; c < SIZE; c++) {
        this.drawCell(ctx, r, c);
      }
    }

    if (!this.over) {
      var x = BOARD_X + this.cursor.c * PITCH;
      var y = BOARD_Y + this.cursor.r * PITCH;
      ctx.setFillStyle('rgba(77,124,255,0.14)');
      ctx.fillRect(x, y, CELL, CELL);
      ctx.setLineWidth(3);
      ctx.setStrokeStyle('#4d7cff');
      ctx.strokeRect(x - 1, y - 1, CELL + 2, CELL + 2);
    }
  },

  drawCell: function (ctx, r, c) {
    var i = r * SIZE + c;
    var g = this.grid[i];
    var x = BOARD_X + c * PITCH;
    var y = BOARD_Y + r * PITCH;
    var cx = x + CELL / 2;
    var cy = y + CELL / 2;

    if (g.open) {
      if (g.mine) {
        ctx.setFillStyle(i === this.boomIndex ? '#a8323a' : '#3a2028');
      } else {
        ctx.setFillStyle('#182029');
      }
      ctx.fillRect(x, y, CELL, CELL);

      if (g.mine) {
        var mineColor = i === this.boomIndex ? '#ffffff' : '#ff8a8a';
        ctx.setFillStyle('#120a0e');
        ctx.beginPath();
        ctx.arc(cx, cy, 11, 0, Math.PI * 2);
        ctx.fill();

        ctx.setStrokeStyle(mineColor);
        ctx.setLineWidth(2);
        ctx.beginPath();
        ctx.moveTo(cx - 16, cy);
        ctx.lineTo(cx + 16, cy);
        ctx.moveTo(cx, cy - 16);
        ctx.lineTo(cx, cy + 16);
        ctx.moveTo(cx - 11, cy - 11);
        ctx.lineTo(cx + 11, cy + 11);
        ctx.moveTo(cx - 11, cy + 11);
        ctx.lineTo(cx + 11, cy - 11);
        ctx.stroke();

        ctx.setFillStyle(mineColor);
        ctx.beginPath();
        ctx.arc(cx, cy, 3, 0, Math.PI * 2);
        ctx.fill();
      } else if (g.n > 0) {
        ctx.setTextAlign('center');
        ctx.setFontSize(28);
        ctx.setFillStyle(NUM_COLORS[g.n - 1]);
        ctx.fillText('' + g.n, cx, y + 34);
        ctx.setTextAlign('left');
      }
    } else {
      ctx.setFillStyle('#273040');
      ctx.fillRect(x, y, CELL, CELL);
      ctx.setFillStyle('#33405a');
      ctx.fillRect(x, y, CELL, 4);
      ctx.setFillStyle('#1e2634');
      ctx.fillRect(x, y + CELL - 5, CELL, 5);

      if (g.flag) {
        ctx.setStrokeStyle('#e9edf6');
        ctx.setLineWidth(3);
        ctx.beginPath();
        ctx.moveTo(cx - 5, cy - 13);
        ctx.lineTo(cx - 5, cy + 13);
        ctx.stroke();

        ctx.setFillStyle('#ffb066');
        ctx.beginPath();
        ctx.moveTo(cx - 5, cy - 13);
        ctx.lineTo(cx + 13, cy - 5);
        ctx.lineTo(cx - 5, cy + 3);
        ctx.closePath();
        ctx.fill();

        ctx.setStrokeStyle('#e9edf6');
        ctx.setLineWidth(3);
        ctx.beginPath();
        ctx.moveTo(cx - 12, cy + 13);
        ctx.lineTo(cx + 3, cy + 13);
        ctx.stroke();
      }
    }
  },

  drawPanel: function (ctx) {
    var values = [
      '' + (this.mineCount - this.flagTotal),
      this.fmt(this.seconds),
      this.bestText
    ];
    var labels = ['剩余雷', '用时', '最佳'];

    for (var i = 0; i < 3; i++) {
      var x = PANEL_X + i * (PANEL_CARD + PANEL_GAP);
      ctx.setFillStyle('#181e28');
      ctx.fillRect(x, 152, PANEL_CARD, 92);
      ctx.setLineWidth(2);
      ctx.setStrokeStyle('#252d3b');
      ctx.strokeRect(x, 152, PANEL_CARD, 92);

      ctx.setTextAlign('center');
      ctx.setFontSize(20);
      ctx.setFillStyle('#727d8f');
      ctx.fillText(labels[i], x + PANEL_CARD / 2, 182);

      ctx.setFontSize(38);
      ctx.setFillStyle('#f2f5fa');
      ctx.fillText(values[i], x + PANEL_CARD / 2, 224);
    }

    // 模式切换
    ctx.setFillStyle(this.flagMode ? '#3a2a18' : '#1c2433');
    ctx.fillRect(PANEL_X, MODE_Y, PANEL_W, MODE_H);
    ctx.setLineWidth(2);
    ctx.setStrokeStyle(this.flagMode ? '#ff9d3d' : '#2b3648');
    ctx.strokeRect(PANEL_X, MODE_Y, PANEL_W, MODE_H);
    ctx.setTextAlign('center');
    ctx.setFontSize(28);
    ctx.setFillStyle(this.flagMode ? '#ffd9b0' : '#cfd8e6');
    ctx.fillText(this.flagMode ? '插旗模式 · 按 X 切换' : '挖开模式 · 按 X 切换',
                 PANEL_X + PANEL_W / 2, MODE_Y + 48);

    // 重新开始
    ctx.setFillStyle('#4d7cff');
    ctx.fillRect(PANEL_X, RESTART_Y, PANEL_W, RESTART_H);
    ctx.setFontSize(28);
    ctx.setFillStyle('#ffffff');
    ctx.fillText('重新开始', PANEL_X + PANEL_W / 2, RESTART_Y + 46);

    // 操作提示
    ctx.setTextAlign('left');
    ctx.setFontSize(24);
    ctx.setFillStyle('#8f9aab');
    ctx.fillText('操作提示', PANEL_X, 486);

    var tips = [
      '方向键 / 左摇杆：移动光标',
      'A：挖开当前格 / 放旗',
      'X：切换挖开与插旗模式',
      'Start：随时重开一局',
      'LB / RB：切换难度',
      '触摸：直接点格子与按钮'
    ];
    ctx.setFontSize(22);
    ctx.setFillStyle('#6b7587');
    for (var t = 0; t < tips.length; t++) {
      ctx.fillText(tips[t], PANEL_X, 518 + t * 34);
    }

    // 底部说明
    ctx.setFillStyle('#6b7587');
    ctx.setFontSize(22);
    ctx.fillText('数字表示周围 8 格的雷数 · 首次点击必定安全', 76, 706);
  },

  drawToast: function (ctx) {
    if (this.toastT <= 0) return;
    var alpha = this.toastT > 0.4 ? 1 : this.toastT / 0.4;
    ctx.setGlobalAlpha(alpha);
    ctx.setTextAlign('center');
    ctx.setFontSize(24);
    ctx.setFillStyle('#ffb066');
    ctx.fillText(this.toastText, W / 2, 136);
    ctx.setGlobalAlpha(1);
    ctx.setTextAlign('left');
  },

  drawOverlay: function (ctx) {
    ctx.setFillStyle('rgba(6,8,12,0.82)');
    ctx.fillRect(0, 0, W, H);

    ctx.setTextAlign('center');
    ctx.setFontSize(64);
    ctx.setFillStyle(this.win ? '#6ee7a8' : '#ff7b7b');
    ctx.fillText(this.win ? '全部扫清！' : '踩到雷了', W / 2, 300);

    ctx.setFontSize(28);
    ctx.setFillStyle('#cfd8e6');
    ctx.fillText('用时 ' + this.fmt(this.seconds) + ' · 最佳 ' + this.bestText,
                 W / 2, 352);

    ctx.setFontSize(24);
    ctx.setFillStyle('#9fb3c8');
    ctx.fillText('按 A / Y 重新开始', W / 2, 396);

    var ob = OVERLAY_BTN;
    ctx.setFillStyle('#4d7cff');
    ctx.fillRect(ob.x, ob.y, ob.w, ob.h);
    ctx.setFontSize(30);
    ctx.setFillStyle('#ffffff');
    ctx.fillText('再来一局', ob.x + ob.w / 2, ob.y + 44);

    ctx.setTextAlign('left');
  }
});
