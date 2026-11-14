// 五子棋：单 canvas 渲染 + requestAnimationFrame
// 操作：方向键 / 左摇杆移动光标，A 落子；X 切换模式，Y 悔棋，Start 重开；触摸棋盘直接落子。

var N = 15;
var W = 1280;
var H = 720;
var CELL = 40;
var OX = 70;
var OY = 70;
var R = 16;
var REPEAT = 0.14;
var PANEL_X = 692;
var PANEL_W = 528;

var DIRS = [[1, 0], [0, 1], [1, 1], [1, -1]];
var STAR = [[3, 3], [11, 3], [3, 11], [11, 11], [7, 7]];

Page({
  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.time = 0;
    this.lastTs = 0;
    this.mode = 'pve';
    this.padPrev = {};
    this.repeatKey = '';
    this.repeatTimer = 0;
    this.reset();
    this.say('人机对战 · 你执黑先行');

    var loop = function (ts) {
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  // ---------------- 状态 ----------------

  reset: function () {
    var b = [];
    for (var i = 0; i < N * N; i++) b.push(0);
    this.board = b;
    this.cur = 1;
    this.moves = 0;
    this.over = false;
    this.winner = 0;
    this.lastIndex = -1;
    this.winCells = [];
    this.history = [];
    this.aiIn = -1;
    this.dropAt = -10;
    this.cursor = { r: 7, c: 7 };
    this.repeatKey = '';
    this.repeatTimer = 0;
  },

  say: function (text) {
    this.notice = text;
    this.noticeAt = this.time;
  },

  countStones: function (p) {
    var n = 0;
    for (var i = 0; i < N * N; i++) {
      if (this.board[i] === p) n++;
    }
    return n;
  },

  // ---------------- 输入 ----------------

  hitBoard: function (x, y) {
    if (x === undefined || y === undefined) return null;
    var c = Math.round((x - OX) / CELL);
    var r = Math.round((y - OY) / CELL);
    if (r < 0 || r >= N || c < 0 || c >= N) return null;
    var dx = x - (OX + c * CELL);
    var dy = y - (OY + r * CELL);
    if (dx * dx + dy * dy > 34 * 34) return null;
    return { r: r, c: c };
  },

  hitButton: function (x, y) {
    if (x === undefined || y === undefined) return '';
    if (x < PANEL_X || x > PANEL_X + PANEL_W) return '';
    if (y >= 338 && y <= 402) return 'mode';
    if (y >= 418 && y <= 482) return x < PANEL_X + 256 ? 'undo' : 'restart';
    return '';
  },

  onTouchStart: function (e) {
    var d = e && e.detail ? e.detail : {};
    var btn = this.hitButton(d.x, d.y);
    if (btn === 'mode') {
      this.toggleMode();
      return;
    }
    if (btn === 'undo') {
      this.undo();
      return;
    }
    if (btn === 'restart') {
      this.restart();
      return;
    }
    var hit = this.hitBoard(d.x, d.y);
    if (!hit) return;
    this.cursor = { r: hit.r, c: hit.c };
    this.tryPlace(hit.r * N + hit.c);
  },

  onTouchMove: function (e) {
    var d = e && e.detail ? e.detail : {};
    var hit = this.hitBoard(d.x, d.y);
    if (!hit) return;
    this.cursor = { r: hit.r, c: hit.c };
  },

  onTouchEnd: function () {
    // 落子在按下时完成，抬手无需处理
  },

  moveCursor: function (dx, dy) {
    var r = this.cursor.r + dy;
    var c = this.cursor.c + dx;
    if (r < 0) r = 0;
    if (r >= N) r = N - 1;
    if (c < 0) c = 0;
    if (c >= N) c = N - 1;
    this.cursor = { r: r, c: c };
  },

  handlePad: function (dt) {
    var pad = wx.getGamepadState ? wx.getGamepadState() : null;
    var buttons = (pad && pad.buttons) || {};
    var axes = (pad && pad.axes) || [];
    var lx = axes.length > 0 ? axes[0] : 0;
    var ly = axes.length > 1 ? axes[1] : 0;

    var now = {
      a: !!buttons.a,
      x: !!buttons.x,
      y: !!buttons.y,
      start: !!buttons.start,
      up: !!buttons.up || ly < -0.55,
      down: !!buttons.down || ly > 0.55,
      left: !!buttons.left || lx < -0.55,
      right: !!buttons.right || lx > 0.55
    };
    var prev = this.padPrev;

    if (now.a && !prev.a) this.tryPlace(this.cursor.r * N + this.cursor.c);
    if (now.x && !prev.x) this.toggleMode();
    if (now.y && !prev.y) this.undo();
    if (now.start && !prev.start) this.restart();

    var dx = (now.right ? 1 : 0) - (now.left ? 1 : 0);
    var dy = (now.down ? 1 : 0) - (now.up ? 1 : 0);
    var key = dx + ',' + dy;
    if (key === '0,0') {
      this.repeatKey = '';
      this.repeatTimer = 0;
    } else if (key !== this.repeatKey) {
      this.repeatKey = key;
      this.repeatTimer = REPEAT;
      this.moveCursor(dx, dy);
    } else {
      this.repeatTimer -= dt;
      if (this.repeatTimer <= 0) {
        this.repeatTimer += REPEAT;
        this.moveCursor(dx, dy);
      }
    }

    this.padPrev = now;
  },

  // ---------------- 规则 ----------------

  tryPlace: function (index) {
    if (this.over) {
      this.say('本局已结束 · 按 Start 重开');
      return;
    }
    if (this.mode === 'pve' && this.cur !== 1) {
      this.say('等电脑落子…');
      return;
    }
    if (this.board[index] !== 0) {
      this.say('这里已经有棋子了');
      return;
    }
    this.place(index, false);
  },

  place: function (index, byAi) {
    this.board[index] = this.cur;
    this.moves++;
    this.history.push(index);
    this.lastIndex = index;
    this.dropAt = this.time;
    if (!byAi) wx.vibrateShort({ type: 'light' });

    var line = this.findWin(index, this.cur);
    if (line.length >= 5) {
      this.over = true;
      this.winner = this.cur;
      this.winCells = line;
      this.aiIn = -1;
      this.say((this.cur === 1 ? '黑棋' : '白棋') + '五子连珠');
      wx.vibrateShort({ type: 'heavy' });
      return;
    }
    if (this.moves >= N * N) {
      this.over = true;
      this.winner = 0;
      this.aiIn = -1;
      this.say('棋盘已满 · 平局');
      wx.vibrateShort({ type: 'heavy' });
      return;
    }

    this.cur = this.cur === 1 ? 2 : 1;
    if (this.mode === 'pve' && this.cur === 2) this.aiIn = 0.38;
  },

  findWin: function (i, p) {
    var r = Math.floor(i / N);
    var c = i % N;
    for (var d = 0; d < 4; d++) {
      var dx = DIRS[d][0];
      var dy = DIRS[d][1];
      var line = [i];
      var nr = r + dx;
      var nc = c + dy;
      while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
        line.push(nr * N + nc);
        nr += dx;
        nc += dy;
      }
      nr = r - dx;
      nc = c - dy;
      while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
        line.unshift(nr * N + nc);
        nr -= dx;
        nc -= dy;
      }
      if (line.length >= 5) return line;
    }
    return [];
  },

  isNear: function (i) {
    var r = Math.floor(i / N);
    var c = i % N;
    for (var dr = -2; dr <= 2; dr++) {
      for (var dc = -2; dc <= 2; dc++) {
        var nr = r + dr;
        var nc = c + dc;
        if (nr < 0 || nc < 0 || nr >= N || nc >= N) continue;
        if (this.board[nr * N + nc] !== 0) return true;
      }
    }
    return false;
  },

  patternScore: function (cnt, open) {
    if (open === 0) return 0;
    if (cnt >= 5) return 500000;
    if (cnt === 4) return open === 2 ? 50000 : 4200;
    if (cnt === 3) return open === 2 ? 3200 : 320;
    if (cnt === 2) return open === 2 ? 210 : 40;
    if (cnt === 1) return open === 2 ? 16 : 5;
    return 0;
  },

  dirScore: function (idx, p, dx, dy) {
    var r = Math.floor(idx / N);
    var c = idx % N;
    var cnt = 1;
    var open = 0;
    var nr = r + dx;
    var nc = c + dy;
    while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
      cnt++;
      nr += dx;
      nc += dy;
    }
    if (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === 0) open++;
    nr = r - dx;
    nc = c - dy;
    while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
      cnt++;
      nr -= dx;
      nc -= dy;
    }
    if (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === 0) open++;
    return this.patternScore(cnt, open);
  },

  pointScore: function (idx, p) {
    var total = 0;
    for (var d = 0; d < 4; d++) {
      total += this.dirScore(idx, p, DIRS[d][0], DIRS[d][1]);
    }
    return total;
  },

  pickMove: function (me, opp) {
    var best = -1;
    var bestScore = -1;
    var any = false;
    for (var i = 0; i < N * N; i++) {
      if (this.board[i] !== 0) continue;
      if (!this.isNear(i)) continue;
      any = true;
      var s = this.pointScore(i, me) * 1.15 + this.pointScore(i, opp);
      var cr = Math.floor(i / N);
      var cc = i % N;
      s += (14 - Math.abs(cr - 7) - Math.abs(cc - 7)) * 2;
      if (s > bestScore) {
        bestScore = s;
        best = i;
      }
    }
    if (!any) return Math.floor((N * N - 1) / 2);
    return best;
  },

  // ---------------- 操作 ----------------

  undo: function () {
    if (this.over) {
      this.say('本局已结束 · 按 Start 重开');
      return;
    }
    if (this.moves === 0) {
      this.say('还没有落子');
      return;
    }
    this.aiIn = -1;
    var count = this.mode === 'pve' ? 2 : 1;
    while (count > 0 && this.history.length > 0) {
      var idx = this.history.pop();
      this.board[idx] = 0;
      this.moves--;
      count--;
    }
    this.lastIndex = this.history.length ? this.history[this.history.length - 1] : -1;
    this.cur = this.moves % 2 === 0 ? 1 : 2;
    this.winCells = [];
    this.dropAt = -10;
    this.say('已悔棋');
    wx.vibrateShort({ type: 'light' });
    if (this.mode === 'pve' && this.cur === 2) this.aiIn = 0.38;
  },

  toggleMode: function () {
    this.mode = this.mode === 'pve' ? 'pvp' : 'pve';
    this.reset();
    this.say(this.mode === 'pve' ? '人机对战 · 你执黑先行' : '双人同屏轮流对战');
    wx.vibrateShort({ type: 'light' });
  },

  restart: function () {
    this.reset();
    this.say('新的一局 · 黑棋先行');
    wx.vibrateShort({ type: 'light' });
  },

  // ---------------- 循环 ----------------

  tick: function (ts) {
    var dt = this.lastTs ? (ts - this.lastTs) / 1000 : 0.016;
    this.lastTs = ts;
    if (!(dt > 0)) dt = 0.016;
    if (dt > 0.05) dt = 0.05;

    this.time += dt;
    this.handlePad(dt);
    this.update(dt);
    this.draw();
  },

  update: function (dt) {
    if (this.over || this.aiIn < 0) return;
    this.aiIn -= dt;
    if (this.aiIn > 0) return;
    this.aiIn = -1;
    if (this.mode !== 'pve' || this.cur !== 2) return;
    var mv = this.pickMove(2, 1);
    if (mv >= 0 && this.board[mv] === 0) this.place(mv, true);
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;
    ctx.setGlobalAlpha(1);
    ctx.setTextAlign('left');
    ctx.setFillStyle('#0e1117');
    ctx.fillRect(0, 0, W, H);

    this.drawBoard(ctx);
    this.drawStones(ctx);
    this.drawLast(ctx);
    if (!this.over) this.drawCursor(ctx);

    if (this.over) {
      ctx.setFillStyle('rgba(8,10,14,0.55)');
      ctx.fillRect(40, 40, 620, 620);
      this.drawWinRings(ctx);
    }

    this.drawPanel(ctx);
    this.drawFooter(ctx);
    ctx.draw();
  },

  drawBoard: function (ctx) {
    var bx = 40;
    var by = 40;
    var bs = 620;

    ctx.setFillStyle('rgba(0,0,0,0.5)');
    ctx.fillRect(bx + 9, by + 11, bs, bs);
    ctx.setFillStyle('#6f4a29');
    ctx.fillRect(bx, by, bs, bs);
    ctx.setFillStyle('#d8b27a');
    ctx.fillRect(bx + 11, by + 11, bs - 22, bs - 22);

    ctx.setFillStyle('rgba(120,78,36,0.10)');
    for (var g = 0; g < 10; g++) {
      ctx.fillRect(bx + 16, by + 34 + g * 58, bs - 32, 2);
    }
    ctx.setFillStyle('rgba(255,255,255,0.16)');
    ctx.fillRect(bx + 11, by + 11, bs - 22, 2);
    ctx.setFillStyle('rgba(80,48,18,0.35)');
    ctx.fillRect(bx + 11, by + bs - 13, bs - 22, 2);

    ctx.setStrokeStyle('#8b6234');
    ctx.setLineWidth(1.4);
    for (var k = 0; k < N; k++) {
      var gx = OX + k * CELL;
      var gy = OY + k * CELL;
      ctx.beginPath();
      ctx.moveTo(gx, OY);
      ctx.lineTo(gx, OY + (N - 1) * CELL);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(OX, gy);
      ctx.lineTo(OX + (N - 1) * CELL, gy);
      ctx.stroke();
    }

    ctx.setStrokeStyle('#7a5227');
    ctx.setLineWidth(3);
    ctx.strokeRect(OX - 1.5, OY - 1.5, (N - 1) * CELL + 3, (N - 1) * CELL + 3);

    ctx.setFillStyle('#6b4a26');
    for (var s = 0; s < STAR.length; s++) {
      ctx.beginPath();
      ctx.arc(OX + STAR[s][0] * CELL, OY + STAR[s][1] * CELL, s === 4 ? 4.5 : 3.5, 0, Math.PI * 2);
      ctx.fill();
    }
  },

  drawStone: function (ctx, x, y, p, radius) {
    ctx.setFillStyle('rgba(60,38,14,0.35)');
    ctx.beginPath();
    ctx.arc(x + radius * 0.12, y + radius * 0.2, radius, 0, Math.PI * 2);
    ctx.fill();

    if (p === 1) {
      ctx.setFillStyle('#16191f');
      ctx.beginPath();
      ctx.arc(x, y, radius, 0, Math.PI * 2);
      ctx.fill();
      ctx.setStrokeStyle('#3a4250');
      ctx.setLineWidth(2);
      ctx.beginPath();
      ctx.arc(x, y, Math.max(1, radius - 1), 0, Math.PI * 2);
      ctx.stroke();
      ctx.setFillStyle('rgba(255,255,255,0.55)');
      ctx.beginPath();
      ctx.arc(x - radius * 0.34, y - radius * 0.38, radius * 0.17, 0, Math.PI * 2);
      ctx.fill();
    } else {
      ctx.setFillStyle('#f4efe2');
      ctx.beginPath();
      ctx.arc(x, y, radius, 0, Math.PI * 2);
      ctx.fill();
      ctx.setStrokeStyle('#c4b9a4');
      ctx.setLineWidth(2);
      ctx.beginPath();
      ctx.arc(x, y, Math.max(1, radius - 1), 0, Math.PI * 2);
      ctx.stroke();
      ctx.setFillStyle('rgba(150,130,100,0.28)');
      ctx.beginPath();
      ctx.arc(x + radius * 0.18, y + radius * 0.22, radius * 0.62, 0, Math.PI * 2);
      ctx.fill();
      ctx.setFillStyle('rgba(255,255,255,0.95)');
      ctx.beginPath();
      ctx.arc(x - radius * 0.3, y - radius * 0.34, radius * 0.2, 0, Math.PI * 2);
      ctx.fill();
    }
  },

  drawStones: function (ctx) {
    for (var i = 0; i < N * N; i++) {
      var v = this.board[i];
      if (!v) continue;
      var r = Math.floor(i / N);
      var c = i % N;
      var scale = 1;
      if (i === this.lastIndex) {
        var t = this.time - this.dropAt;
        if (t < 0.18) scale = 0.72 + (t / 0.18) * 0.28;
      }
      this.drawStone(ctx, OX + c * CELL, OY + r * CELL, v, R * scale);
    }
  },

  drawLast: function (ctx) {
    if (this.over || this.lastIndex < 0) return;
    var r = Math.floor(this.lastIndex / N);
    var c = this.lastIndex % N;
    ctx.setFillStyle('#e5484d');
    ctx.beginPath();
    ctx.arc(OX + c * CELL, OY + r * CELL, 5, 0, Math.PI * 2);
    ctx.fill();
  },

  drawCursor: function (ctx) {
    var x = OX + this.cursor.c * CELL;
    var y = OY + this.cursor.r * CELL;
    var occupied = this.board[this.cursor.r * N + this.cursor.c] !== 0;
    var color = occupied ? '#ffb454' : '#6f9bff';

    ctx.setGlobalAlpha(0.6 + 0.4 * Math.abs(Math.sin(this.time * 3)));
    ctx.setStrokeStyle(color);
    ctx.setLineWidth(2.5);
    ctx.strokeRect(x - 18, y - 18, 36, 36);
    ctx.setLineWidth(3);
    ctx.beginPath();
    ctx.moveTo(x - 25, y - 18);
    ctx.lineTo(x - 25, y - 25);
    ctx.lineTo(x - 18, y - 25);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(x + 18, y - 25);
    ctx.lineTo(x + 25, y - 25);
    ctx.lineTo(x + 25, y - 18);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(x + 25, y + 18);
    ctx.lineTo(x + 25, y + 25);
    ctx.lineTo(x + 18, y + 25);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(x - 18, y + 25);
    ctx.lineTo(x - 25, y + 25);
    ctx.lineTo(x - 25, y + 18);
    ctx.stroke();
    ctx.setGlobalAlpha(1);
  },

  drawWinRings: function (ctx) {
    var pulse = 1 + Math.sin(this.time * 5) * 0.05;
    for (var i = 0; i < this.winCells.length; i++) {
      var r = Math.floor(this.winCells[i] / N);
      var c = this.winCells[i] % N;
      var x = OX + c * CELL;
      var y = OY + r * CELL;
      ctx.setStrokeStyle('rgba(46,240,138,0.35)');
      ctx.setLineWidth(8);
      ctx.beginPath();
      ctx.arc(x, y, 26 * pulse, 0, Math.PI * 2);
      ctx.stroke();
      ctx.setStrokeStyle('#2ef08a');
      ctx.setLineWidth(3.5);
      ctx.beginPath();
      ctx.arc(x, y, 22 * pulse, 0, Math.PI * 2);
      ctx.stroke();
    }
  },

  resultTitle: function () {
    var pve = this.mode === 'pve';
    if (this.winner === 0) return '平局';
    if (pve) return this.winner === 1 ? '你赢了' : '电脑获胜';
    return this.winner === 1 ? '黑棋获胜' : '白棋获胜';
  },

  drawTurnCard: function (ctx, x, y, w, h, player, active) {
    ctx.setFillStyle(active ? '#1a2130' : '#141922');
    ctx.fillRect(x, y, w, h);
    ctx.setStrokeStyle(active ? '#4d7cff' : '#232b38');
    ctx.setLineWidth(active ? 2.5 : 1.5);
    ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);

    this.drawStone(ctx, x + 44, y + h / 2, player, 22);

    ctx.setTextAlign('left');
    ctx.setFillStyle(active ? '#eef3ff' : '#7c879a');
    ctx.setFontSize(28);
    ctx.fillText(player === 1 ? '黑棋' : '白棋', x + 80, y + 44);

    var pve = this.mode === 'pve';
    var small;
    if (pve) small = player === 1 ? '你' : '电脑';
    else small = '玩家';
    if (active) small += ' · 当前';
    ctx.setFillStyle(active ? '#7fa6ff' : '#55606f');
    ctx.setFontSize(18);
    ctx.fillText(small, x + 80, y + 72);
  },

  drawKeyButton: function (ctx, x, y, w, h, label, key, accent) {
    ctx.setFillStyle(accent ? '#16281d' : '#181e29');
    ctx.fillRect(x, y, w, h);
    ctx.setStrokeStyle(accent ? '#3f9e6a' : '#2a3341');
    ctx.setLineWidth(2);
    ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);

    ctx.setTextAlign('left');
    ctx.setFillStyle(accent ? '#a8ecc4' : '#dbe3ef');
    ctx.setFontSize(26);
    ctx.fillText(label, x + 24, y + h / 2 + 9);

    if (key) {
      var kw = key.length > 1 ? 74 : 38;
      var kx = x + w - kw - 18;
      var ky = y + h / 2 - 17;
      ctx.setFillStyle(accent ? 'rgba(63,158,106,0.28)' : '#232c3a');
      ctx.fillRect(kx, ky, kw, 34);
      ctx.setTextAlign('center');
      ctx.setFillStyle(accent ? '#c8f5dc' : '#8e9bb0');
      ctx.setFontSize(key.length > 1 ? 18 : 20);
      ctx.fillText(key, kx + kw / 2, ky + 24);
      ctx.setTextAlign('left');
    }
  },

  drawPanel: function (ctx) {
    var pve = this.mode === 'pve';

    ctx.setFillStyle('#121722');
    ctx.fillRect(672, 40, 568, 620);
    ctx.setStrokeStyle('#1c2431');
    ctx.setLineWidth(1.5);
    ctx.strokeRect(672.75, 40.75, 566.5, 618.5);

    ctx.setTextAlign('left');
    ctx.setFillStyle('#f4f7fc');
    ctx.setFontSize(58);
    ctx.fillText('五子棋', PANEL_X, 118);
    ctx.setFillStyle('#6b7689');
    ctx.setFontSize(21);
    ctx.fillText('15 路棋盘 · 五子连珠获胜', PANEL_X + 2, 152);

    var chipW = 216;
    var chipX = PANEL_X + PANEL_W - chipW;
    var chipY = 74;
    var chipH = 54;
    ctx.setFillStyle(pve ? '#16281d' : '#1b2436');
    ctx.fillRect(chipX, chipY, chipW, chipH);
    ctx.setStrokeStyle(pve ? '#3f9e6a' : '#3d5da8');
    ctx.setLineWidth(2);
    ctx.strokeRect(chipX + 1, chipY + 1, chipW - 2, chipH - 2);
    ctx.setTextAlign('center');
    ctx.setFillStyle(pve ? '#a8ecc4' : '#a9c4ff');
    ctx.setFontSize(22);
    ctx.fillText(pve ? '人机对战' : '双人对战', chipX + chipW / 2, chipY + 35);
    ctx.setTextAlign('left');

    this.drawTurnCard(ctx, PANEL_X, 182, 256, 100, 1, this.cur === 1 && !this.over);
    this.drawTurnCard(ctx, PANEL_X + 272, 182, 256, 100, 2, this.cur === 2 && !this.over);

    ctx.setFillStyle('#7c879a');
    ctx.setFontSize(22);
    ctx.fillText('已落 ' + this.moves + ' 手 · 黑 ' + this.countStones(1) + ' / 白 ' + this.countStones(2), PANEL_X + 2, 320);

    this.drawKeyButton(ctx, PANEL_X, 338, PANEL_W, 64,
      pve ? '人机对战 · 你执黑先行' : '双人同屏轮流对战', 'X', pve);
    this.drawKeyButton(ctx, PANEL_X, 418, 256, 64, '悔棋', 'Y', false);
    this.drawKeyButton(ctx, PANEL_X + 272, 418, 256, 64, '重新开始', 'Start', false);

    var boxY = 500;
    var boxH = 92;
    if (this.over) {
      var acc = this.winner === 0 ? '#e6c15a' : '#2ef08a';
      ctx.setFillStyle('#16211c');
      ctx.fillRect(PANEL_X, boxY, PANEL_W, boxH);
      ctx.setStrokeStyle('#2f4a3c');
      ctx.setLineWidth(2);
      ctx.strokeRect(PANEL_X + 1, boxY + 1, PANEL_W - 2, boxH - 2);
      ctx.setTextAlign('center');
      ctx.setFillStyle(acc);
      ctx.setFontSize(30);
      ctx.fillText(this.resultTitle(), PANEL_X + PANEL_W / 2, boxY + 38);
      ctx.setFillStyle('#8aa99a');
      ctx.setFontSize(19);
      ctx.fillText(this.winner === 0 ? '棋盘已满 · 共 ' + this.moves + ' 手' : '五子连珠 · 共 ' + this.moves + ' 手 · Start 重开', PANEL_X + PANEL_W / 2, boxY + 70);
      ctx.setTextAlign('left');
    } else {
      var main;
      var sub;
      var col = '#e8eef8';
      if (pve && this.cur === 2) {
        var dots = '...'.substring(0, 1 + Math.floor(this.time * 2) % 3);
        main = '电脑思考中' + dots;
        sub = '白棋 · 已落 ' + this.moves + ' 手';
      } else if (pve) {
        main = '轮到你落子';
        sub = '执黑 · 已落 ' + this.moves + ' 手';
      } else {
        main = this.cur === 1 ? '黑棋行棋' : '白棋行棋';
        sub = '双人对战 · 已落 ' + this.moves + ' 手';
      }
      if (this.time - this.noticeAt < 1.8) {
        main = this.notice;
        col = '#ffd166';
      }

      ctx.setFillStyle('#141a24');
      ctx.fillRect(PANEL_X, boxY, PANEL_W, boxH);
      ctx.setStrokeStyle('#232c3a');
      ctx.setLineWidth(2);
      ctx.strokeRect(PANEL_X + 1, boxY + 1, PANEL_W - 2, boxH - 2);
      ctx.setTextAlign('center');
      ctx.setFillStyle(col);
      ctx.setFontSize(29);
      ctx.fillText(main, PANEL_X + PANEL_W / 2, boxY + 38);
      ctx.setFillStyle('#717d90');
      ctx.setFontSize(19);
      ctx.fillText(sub, PANEL_X + PANEL_W / 2, boxY + 70);
      ctx.setTextAlign('left');
    }

    ctx.setFillStyle('#5d6879');
    ctx.setFontSize(19);
    ctx.fillText('方向键 / 左摇杆 移动光标 · A 落子', PANEL_X + 2, 626);
    ctx.fillText('X 切换模式 · Y 悔棋 · Start 重开', PANEL_X + 2, 652);
  },

  drawFooter: function (ctx) {
    ctx.setFillStyle('#090c11');
    ctx.fillRect(0, 676, W, H - 676);
    ctx.setFillStyle('#1b2330');
    ctx.fillRect(0, 676, W, 1);
    ctx.setTextAlign('center');
    if (this.over) {
      var acc = this.winner === 0 ? '#e6c15a' : '#2ef08a';
      ctx.setFillStyle(acc);
      ctx.setFontSize(24);
      ctx.fillText(this.resultTitle() + ' · 共 ' + this.moves + ' 手 · 按 Start 重开', W / 2, 706);
    } else {
      ctx.setFillStyle('#79849a');
      ctx.setFontSize(20);
      ctx.fillText('方向键 / 左摇杆 移动光标 · A 落子 · X 切换模式 · Y 悔棋 · Start 重开 · 触摸棋盘落子', W / 2, 704);
    }
    ctx.setTextAlign('left');
  }
});
