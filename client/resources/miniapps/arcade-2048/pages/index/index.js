// 2048 · 掌上合成 —— 单 canvas 渲染版
// 操作：方向键 / 左摇杆 / 屏幕滑动移动方块，X 撤销，Y 重开，B 由容器返回

var W = 1280;
var H = 720;
var BOARD_X = 64;
var BOARD_Y = 140;
var CELL = 116;
var GAP = 10;
var PAD = 14;
var BOARD_SIZE = PAD * 2 + CELL * 4 + GAP * 3;
var SPAWN_DUR = 0.2;
var MERGE_DUR = 0.18;

var BTN_UNDO = { x: 660, y: 212, w: 250, h: 80 };
var BTN_NEW = { x: 946, y: 212, w: 250, h: 80 };
var CARD = { x: 380, y: 196, w: 520, h: 330 };
var CARD_A = { x: 406, y: 404, w: 210, h: 76 };
var CARD_B = { x: 664, y: 404, w: 210, h: 76 };
var CARD_SOLO = { x: 530, y: 404, w: 220, h: 76 };

function emptyBoard() {
  var a = [];
  for (var i = 0; i < 16; i++) a.push(0);
  return a;
}

function inRect(r, x, y) {
  return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

function highest(board) {
  var m = 0;
  for (var i = 0; i < 16; i++) {
    if (board[i] > m) m = board[i];
  }
  return m;
}

function hasMoves(board) {
  var r, c, i;
  for (i = 0; i < 16; i++) {
    if (board[i] === 0) return true;
  }
  for (r = 0; r < 4; r++) {
    for (c = 0; c < 4; c++) {
      var v = board[r * 4 + c];
      if (c < 3 && v === board[r * 4 + c + 1]) return true;
      if (r < 3 && v === board[(r + 1) * 4 + c]) return true;
    }
  }
  return false;
}

function slideLine(vals) {
  var vals2 = [];
  var i;
  for (i = 0; i < vals.length; i++) {
    if (vals[i]) vals2.push(vals[i]);
  }
  var out = [];
  var merged = [];
  var gained = 0;
  i = 0;
  while (i < vals2.length) {
    if (i + 1 < vals2.length && vals2[i] === vals2[i + 1]) {
      out.push(vals2[i] * 2);
      gained += vals2[i] * 2;
      merged.push(out.length - 1);
      i += 2;
    } else {
      out.push(vals2[i]);
      i += 1;
    }
  }
  while (out.length < vals.length) out.push(0);
  return { line: out, gained: gained, merged: merged };
}

function tileBg(v) {
  if (v === 2) return '#2a3242';
  if (v === 4) return '#36415a';
  if (v === 8) return '#f2b179';
  if (v === 16) return '#f59563';
  if (v === 32) return '#f67c5f';
  if (v === 64) return '#f65e3b';
  if (v === 128) return '#edcf72';
  if (v === 256) return '#edcc61';
  if (v === 512) return '#edc850';
  if (v === 1024) return '#edc53f';
  if (v === 2048) return '#edc22e';
  return '#3c3a32';
}

function tileFg(v) {
  if (v === 2 || v === 4) return '#dfe8f5';
  if (v === 8) return '#40260f';
  if (v >= 128 && v <= 2048) return '#3b2c07';
  if (v > 2048) return '#f7f2e4';
  return '#ffffff';
}

function tileFontSize(v) {
  var n = ('' + v).length;
  var fs;
  if (n <= 1) fs = 56;
  else if (n === 2) fs = 50;
  else if (n === 3) fs = 44;
  else if (n === 4) fs = 36;
  else fs = 30;
  while (n * fs * 0.58 > CELL - 18 && fs > 16) fs -= 2;
  return fs;
}

Page({
  data: {},

  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.anim = { spawns: [], merges: [], shake: 0, toastT: 0, toastText: '', winT: 0 };
    this.undoStack = [];
    this.mode = 'play';
    this.over = false;
    this.prevPad = {};
    this.lastTs = 0;
    this.touch = null;
    this.flash = { undo: 0, restart: 0 };

    this.best = Number(wx.getStorageSync('best') || 0) || 0;
    this.board = emptyBoard();
    this.score = 0;
    this.won = false;

    var saved = this.loadGame();
    if (saved && hasMoves(saved.board)) {
      this.board = saved.board;
      this.score = saved.score;
      this.won = saved.won;
      if (this.score > this.best) this.best = this.score;
    } else {
      this.newGame();
    }

    var loop = function (ts) {
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  // ---------------- 存档 ----------------

  saveGame: function () {
    var data = {
      board: this.board,
      score: this.score,
      won: this.won,
      over: this.over
    };
    wx.setStorageSync('save', JSON.stringify(data));
    if (this.score > this.best) {
      this.best = this.score;
      wx.setStorageSync('best', this.best);
    }
  },

  loadGame: function () {
    var raw = wx.getStorageSync('save');
    if (!raw) return null;
    var d = raw;
    if (typeof raw === 'string') {
      try {
        d = JSON.parse(raw);
      } catch (e) {
        return null;
      }
    }
    if (!d || !d.board || d.board.length !== 16) return null;
    if (d.over) return null;
    var b = [];
    var filled = 0;
    for (var i = 0; i < 16; i++) {
      var v = Number(d.board[i]);
      if (!isFinite(v) || v < 0) return null;
      v = Math.floor(v);
      if (v > 0) filled += 1;
      b.push(v);
    }
    if (!filled) return null;
    return {
      board: b,
      score: Number(d.score) || 0,
      won: !!d.won
    };
  },

  // ---------------- 游戏流程 ----------------

  newGame: function () {
    this.board = emptyBoard();
    this.score = 0;
    this.won = false;
    this.over = false;
    this.mode = 'play';
    this.undoStack = [];
    this.anim.merges = [];
    this.anim.spawns = [];
    this.anim.winT = 0;
    this.anim.shake = 0;
    this.flash.restart = 0.2;
    this.spawnTile();
    this.spawnTile();
    this.saveGame();
  },

  spawnTile: function () {
    var zeros = [];
    var i;
    for (i = 0; i < 16; i++) {
      if (this.board[i] === 0) zeros.push(i);
    }
    if (!zeros.length) return -1;
    var idx = zeros[Math.floor(Math.random() * zeros.length)];
    this.board[idx] = Math.random() < 0.9 ? 2 : 4;
    this.anim.spawns.push({ i: idx, t: 0 });
    return idx;
  },

  move: function (dir) {
    if (this.over || this.mode !== 'play') return;
    var old = this.board.slice();
    var next = emptyBoard();
    var gained = 0;
    var merges = [];
    var n, i;
    for (n = 0; n < 4; n++) {
      var idxs;
      if (dir === 'left') idxs = [n * 4, n * 4 + 1, n * 4 + 2, n * 4 + 3];
      else if (dir === 'right') idxs = [n * 4 + 3, n * 4 + 2, n * 4 + 1, n * 4];
      else if (dir === 'up') idxs = [n, 4 + n, 8 + n, 12 + n];
      else idxs = [12 + n, 8 + n, 4 + n, n];

      var vals = [];
      for (i = 0; i < 4; i++) vals.push(this.board[idxs[i]]);
      var res = slideLine(vals);
      gained += res.gained;
      for (i = 0; i < 4; i++) next[idxs[i]] = res.line[i];
      for (i = 0; i < res.merged.length; i++) {
        merges.push({ i: idxs[res.merged[i]], t: 0 });
      }
    }

    var changed = false;
    for (i = 0; i < 16; i++) {
      if (next[i] !== old[i]) changed = true;
    }
    if (!changed) {
      this.showToast('这个方向动不了');
      return;
    }

    this.undoStack.push({ board: old, score: this.score, won: this.won });
    if (this.undoStack.length > 8) this.undoStack.shift();

    this.board = next;
    this.score += gained;
    this.anim.merges = merges;
    this.spawnTile();

    if (gained > 0) {
      this.anim.shake = 0.7;
      wx.vibrateShort({ type: 'light' });
    }
    if (!this.won && this.board.indexOf(2048) >= 0) {
      this.won = true;
      this.anim.winT = 2.8;
      wx.vibrateShort({ type: 'heavy' });
    }

    this.checkOver();
    this.saveGame();
  },

  checkOver: function () {
    if (hasMoves(this.board)) return;
    this.over = true;
    this.mode = 'play';
    wx.vibrateShort({ type: 'heavy' });
  },

  undo: function () {
    var snap = this.undoStack.pop();
    if (!snap) {
      this.showToast('没有可撤销的步骤');
      return;
    }
    this.board = snap.board.slice();
    this.score = snap.score;
    this.won = snap.won;
    this.over = false;
    this.mode = 'play';
    this.anim.merges = [];
    this.anim.spawns = [];
    this.anim.winT = 0;
    this.flash.undo = 0.2;
    this.saveGame();
  },

  requestRestart: function () {
    if (this.score === 0) {
      this.newGame();
      return;
    }
    this.mode = 'confirm';
    this.flash.restart = 0.2;
  },

  showToast: function (text) {
    this.anim.toastText = text;
    this.anim.toastT = 1.1;
  },

  // ---------------- 输入 ----------------

  onTouchStart: function (e) {
    this.touch = { x: e.detail.x, y: e.detail.y };
  },

  onTouchMove: function () {},

  onTouchEnd: function (e) {
    var t = this.touch;
    this.touch = null;
    if (!t) return;
    var x = e.detail.x;
    var y = e.detail.y;
    var dx = x - t.x;
    var dy = y - t.y;
    if (Math.abs(dx) < 36 && Math.abs(dy) < 36) {
      this.tap(x, y);
      return;
    }
    if (this.over || this.mode !== 'play') return;
    if (Math.abs(dx) > Math.abs(dy)) {
      this.move(dx > 0 ? 'right' : 'left');
    } else {
      this.move(dy > 0 ? 'down' : 'up');
    }
  },

  tap: function (x, y) {
    if (this.over) {
      if (this.undoStack.length) {
        if (inRect(CARD_A, x, y)) {
          this.undo();
          return;
        }
        if (inRect(CARD_B, x, y)) {
          this.newGame();
          return;
        }
      } else if (inRect(CARD_SOLO, x, y)) {
        this.newGame();
      }
      return;
    }
    if (this.mode === 'confirm') {
      if (inRect(CARD_A, x, y)) this.newGame();
      else if (inRect(CARD_B, x, y)) this.mode = 'play';
      return;
    }
    if (inRect(BTN_UNDO, x, y)) {
      this.undo();
    } else if (inRect(BTN_NEW, x, y)) {
      this.requestRestart();
    }
  },

  readPad: function () {
    var pad = wx.getGamepadState();
    var b = (pad && pad.buttons) || {};
    var axes = (pad && pad.axes) || [0, 0];
    var lx = axes[0] || 0;
    var ly = axes[1] || 0;
    var cur = {
      up: !!b.up || ly < -0.55,
      down: !!b.down || ly > 0.55,
      left: !!b.left || lx < -0.55,
      right: !!b.right || lx > 0.55,
      x: !!b.x,
      y: !!b.y
    };
    var prev = this.prevPad || {};
    var dir = null;
    if (cur.up && !prev.up) dir = 'up';
    else if (cur.down && !prev.down) dir = 'down';
    else if (cur.left && !prev.left) dir = 'left';
    else if (cur.right && !prev.right) dir = 'right';
    var pressX = cur.x && !prev.x;
    var pressY = cur.y && !prev.y;
    this.prevPad = cur;

    if (this.over) {
      if (pressX) this.undo();
      if (pressY) this.newGame();
      return;
    }
    if (this.mode === 'confirm') {
      if (pressY) this.newGame();
      else if (pressX) this.mode = 'play';
      return;
    }
    if (dir) this.move(dir);
    if (pressX) this.undo();
    if (pressY) this.requestRestart();
  },

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
    var a = this.anim;
    var i;
    if (a.shake > 0) a.shake = Math.max(0, a.shake - dt * 3.2);
    if (a.toastT > 0) a.toastT = Math.max(0, a.toastT - dt);
    if (a.winT > 0) a.winT = Math.max(0, a.winT - dt);
    if (this.flash.undo > 0) this.flash.undo = Math.max(0, this.flash.undo - dt);
    if (this.flash.restart > 0) this.flash.restart = Math.max(0, this.flash.restart - dt);

    for (i = a.spawns.length - 1; i >= 0; i--) {
      a.spawns[i].t += dt;
      if (a.spawns[i].t > SPAWN_DUR + 0.1) a.spawns.splice(i, 1);
    }
    for (i = a.merges.length - 1; i >= 0; i--) {
      a.merges[i].t += dt;
      if (a.merges[i].t > MERGE_DUR + 0.1) a.merges.splice(i, 1);
    }

    this.readPad();
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;
    ctx.setGlobalAlpha(1);
    ctx.setFillStyle('#0f1117');
    ctx.fillRect(0, 0, W, H);

    ctx.setFillStyle('rgba(255,255,255,0.022)');
    var gx, gy;
    for (gx = 0; gx < W; gx += 80) ctx.fillRect(gx, 0, 1, H);
    for (gy = 0; gy < H; gy += 80) ctx.fillRect(0, gy, W, 1);

    this.drawHeader(ctx);
    this.drawBoard(ctx);
    this.drawPanel(ctx);
    this.drawHints(ctx);
    this.drawWinBanner(ctx);
    if (this.mode === 'confirm') this.drawConfirm(ctx);
    if (this.over) this.drawOver(ctx);
    this.drawToast(ctx);
    ctx.draw();
  },

  drawHeader: function (ctx) {
    ctx.setTextAlign('left');
    ctx.setFontSize(66);
    ctx.setFillStyle('#f5b642');
    ctx.fillText('2048', 648, 100);
    ctx.setFontSize(20);
    ctx.setFillStyle('#7d8698');
    ctx.fillText('掌上合成 · 越合越大', 650, 130);
    this.drawScoreCard(ctx, 880, 44, 150, '得分', this.score);
    this.drawScoreCard(ctx, 1046, 44, 170, '最高分', this.best);
  },

  drawScoreCard: function (ctx, x, y, w, label, value) {
    var text = '' + value;
    var fs = text.length <= 4 ? 32 : (text.length <= 6 ? 26 : 22);
    ctx.setFillStyle('#181c26');
    ctx.fillRect(x, y, w, 88);
    ctx.setStrokeStyle('#252b38');
    ctx.setLineWidth(2);
    ctx.strokeRect(x, y, w, 88);
    ctx.setTextAlign('center');
    ctx.setFontSize(18);
    ctx.setFillStyle('#7d8698');
    ctx.fillText(label, x + w / 2, y + 32);
    ctx.setFontSize(fs);
    ctx.setFillStyle('#f5b642');
    ctx.fillText(text, x + w / 2, y + 72);
    ctx.setTextAlign('left');
  },

  drawBoard: function (ctx) {
    var a = this.anim;
    var off = a.shake > 0 ? Math.sin(Date.now() / 22) * 3 * a.shake : 0;
    ctx.save();
    ctx.translate(off, 0);

    ctx.setFillStyle('rgba(0,0,0,0.35)');
    ctx.fillRect(BOARD_X + 5, BOARD_Y + 6, BOARD_SIZE, BOARD_SIZE);
    ctx.setFillStyle('#151924');
    ctx.fillRect(BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE);
    ctx.setStrokeStyle('#232b3a');
    ctx.setLineWidth(2);
    ctx.strokeRect(BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE);

    var r, c;
    for (r = 0; r < 4; r++) {
      for (c = 0; c < 4; c++) {
        var x = BOARD_X + PAD + c * (CELL + GAP);
        var y = BOARD_Y + PAD + r * (CELL + GAP);
        ctx.setFillStyle('#1b2029');
        ctx.fillRect(x, y, CELL, CELL);
        var v = this.board[r * 4 + c];
        if (v) this.drawTile(ctx, x, y, v, r * 4 + c);
      }
    }
    ctx.restore();
  },

  drawTile: function (ctx, x, y, v, idx) {
    var sp = null;
    var mg = null;
    var i;
    for (i = 0; i < this.anim.spawns.length; i++) {
      if (this.anim.spawns[i].i === idx) {
        sp = this.anim.spawns[i];
        break;
      }
    }
    if (!sp) {
      for (i = 0; i < this.anim.merges.length; i++) {
        if (this.anim.merges[i].i === idx) {
          mg = this.anim.merges[i];
          break;
        }
      }
    }

    var scale = 1;
    var alpha = 1;
    var p;
    if (sp) {
      p = sp.t / SPAWN_DUR;
      if (p > 1) p = 1;
      scale = 0.4 + 0.6 * (1 - (1 - p) * (1 - p));
      alpha = 0.25 + 0.75 * p;
    } else if (mg) {
      p = mg.t / MERGE_DUR;
      if (p > 1) p = 1;
      scale = 1 + 0.18 * (1 - p);
    }

    var fs = tileFontSize(v);
    var cx = x + CELL / 2;
    var cy = y + CELL / 2;

    ctx.save();
    if (alpha < 1) ctx.setGlobalAlpha(alpha);
    if (scale !== 1) {
      ctx.translate(cx, cy);
      ctx.scale(scale, scale);
      ctx.translate(-cx, -cy);
    }
    ctx.setFillStyle(tileBg(v));
    ctx.fillRect(x, y, CELL, CELL);
    ctx.setFillStyle('rgba(255,255,255,0.10)');
    ctx.fillRect(x, y, CELL, 3);
    ctx.setTextAlign('center');
    ctx.setFontSize(fs);
    ctx.setFillStyle(tileFg(v));
    ctx.fillText('' + v, cx, cy + fs * 0.34);
    ctx.setTextAlign('left');
    ctx.restore();
    ctx.setGlobalAlpha(1);
  },

  drawPanel: function (ctx) {
    ctx.setFillStyle('#1b2230');
    ctx.fillRect(660, 172, 536, 2);

    var undoLabel = '撤销一步';
    if (this.undoStack.length) undoLabel += ' (' + this.undoStack.length + ')';
    this.drawButton(ctx, BTN_UNDO, undoLabel, false, this.flash.undo > 0, 26);
    this.drawButton(ctx, BTN_NEW, '重新开始', true, this.flash.restart > 0, 26);

    if (this.won) {
      ctx.setFillStyle('#2a2410');
      ctx.fillRect(660, 316, 536, 50);
      ctx.setStrokeStyle('#6b5520');
      ctx.setLineWidth(2);
      ctx.strokeRect(660, 316, 536, 50);
      ctx.setTextAlign('center');
      ctx.setFontSize(22);
      ctx.setFillStyle('#f5d06a');
      ctx.fillText('已达成 2048 · 继续冲更高分', 928, 347);
      ctx.setTextAlign('left');
    }

    ctx.setTextAlign('left');
    ctx.setFontSize(20);
    ctx.setFillStyle('#5b6b7d');
    ctx.fillText('操作说明', 660, 404);

    var lines = [
      '方向键 / 左摇杆 移动方块',
      '屏幕滑动 移动方块',
      'X 撤销一步',
      'Y 重新开始',
      'B 返回列表'
    ];
    ctx.setFontSize(22);
    ctx.setFillStyle('#9aa4b5');
    for (var i = 0; i < lines.length; i++) {
      ctx.fillText(lines[i], 660, 444 + i * 36);
    }

    ctx.setFontSize(22);
    ctx.setFillStyle('#7d8698');
    ctx.fillText('最大方块 ' + highest(this.board) + ' · 目标 2048', 660, 630);
  },

  drawHints: function (ctx) {
    ctx.setTextAlign('center');
    ctx.setFontSize(20);
    ctx.setFillStyle('#6f7787');
    ctx.fillText('方向键 / 左摇杆 / 滑动屏幕 移动   ·   X 撤销   ·   Y 重开', 640, 702);
    ctx.setTextAlign('left');
  },

  drawToast: function (ctx) {
    if (this.anim.toastT <= 0 || !this.anim.toastText) return;
    var alpha = this.anim.toastT / 0.3;
    if (alpha > 1) alpha = 1;
    ctx.save();
    ctx.setGlobalAlpha(alpha);
    ctx.setFillStyle('rgba(20,26,38,0.95)');
    ctx.fillRect(115, 94, 420, 44);
    ctx.setStrokeStyle('#3a465c');
    ctx.setLineWidth(2);
    ctx.strokeRect(115, 94, 420, 44);
    ctx.setTextAlign('center');
    ctx.setFontSize(22);
    ctx.setFillStyle('#ffd166');
    ctx.fillText(this.anim.toastText, 325, 123);
    ctx.setTextAlign('left');
    ctx.restore();
    ctx.setGlobalAlpha(1);
  },

  drawButton: function (ctx, r, label, main, active, fs) {
    var fill;
    var border;
    var color;
    if (main) {
      fill = active ? '#ffd166' : '#f5b642';
      border = fill;
      color = '#241a05';
    } else {
      fill = active ? '#2b3446' : '#202634';
      border = active ? '#3a465c' : '#2c3546';
      color = '#cfd8e6';
    }
    ctx.setFillStyle(fill);
    ctx.fillRect(r.x, r.y, r.w, r.h);
    ctx.setStrokeStyle(border);
    ctx.setLineWidth(2);
    ctx.strokeRect(r.x, r.y, r.w, r.h);
    ctx.setTextAlign('center');
    ctx.setFontSize(fs);
    ctx.setFillStyle(color);
    ctx.fillText(label, r.x + r.w / 2, r.y + r.h / 2 + fs * 0.34);
    ctx.setTextAlign('left');
  },

  drawWinBanner: function (ctx) {
    var t = this.anim.winT;
    if (t <= 0) return;
    var alpha = 1;
    if (t > 2.3) alpha = (2.8 - t) / 0.5;
    else if (t < 0.5) alpha = t / 0.5;
    if (alpha > 1) alpha = 1;
    if (alpha < 0) alpha = 0;

    ctx.save();
    ctx.setGlobalAlpha(alpha);
    ctx.setFillStyle('rgba(10,14,20,0.88)');
    ctx.fillRect(84, 300, 482, 92);
    ctx.setStrokeStyle('#edc22e');
    ctx.setLineWidth(2);
    ctx.strokeRect(84, 300, 482, 92);
    ctx.setTextAlign('center');
    ctx.setFontSize(40);
    ctx.setFillStyle('#ffd166');
    ctx.fillText('2048 达成！', 325, 342);
    ctx.setFontSize(20);
    ctx.setFillStyle('#c3ccdb');
    ctx.fillText('可以继续冲更高分', 325, 374);
    ctx.setTextAlign('left');
    ctx.restore();
    ctx.setGlobalAlpha(1);
  },

  drawModalBg: function (ctx) {
    ctx.setFillStyle('rgba(6,8,14,0.76)');
    ctx.fillRect(0, 0, W, H);
    ctx.setFillStyle('#181d28');
    ctx.fillRect(CARD.x, CARD.y, CARD.w, CARD.h);
    ctx.setStrokeStyle('#2c3546');
    ctx.setLineWidth(2);
    ctx.strokeRect(CARD.x, CARD.y, CARD.w, CARD.h);
  },

  drawOver: function (ctx) {
    this.drawModalBg(ctx);
    ctx.setTextAlign('center');
    ctx.setFontSize(44);
    ctx.setFillStyle('#ff7a6b');
    ctx.fillText('无路可走了', 640, 286);
    ctx.setFontSize(24);
    ctx.setFillStyle('#c3ccdb');
    ctx.fillText('本局得分 ' + this.score + ' · 最高分 ' + this.best, 640, 334);
    ctx.setFontSize(20);
    ctx.setFillStyle('#7d8698');
    ctx.fillText('最大方块 ' + highest(this.board), 640, 372);
    ctx.setTextAlign('left');

    if (this.undoStack.length) {
      this.drawButton(ctx, CARD_A, '撤销一步 (X)', false, false, 24);
      this.drawButton(ctx, CARD_B, '再来一局 (Y)', true, false, 24);
    } else {
      this.drawButton(ctx, CARD_SOLO, '再来一局 (Y)', true, false, 26);
    }
  },

  drawConfirm: function (ctx) {
    this.drawModalBg(ctx);
    ctx.setTextAlign('center');
    ctx.setFontSize(40);
    ctx.setFillStyle('#f5b642');
    ctx.fillText('重新开始？', 640, 290);
    ctx.setFontSize(22);
    ctx.setFillStyle('#c3ccdb');
    ctx.fillText('当前进度会清空，确定重开一局吗？', 640, 340);
    ctx.setFontSize(18);
    ctx.setFillStyle('#6f7787');
    ctx.fillText('按 Y 确认 · 按 X 取消', 640, 508);
    ctx.setTextAlign('left');
    this.drawButton(ctx, CARD_A, '确认重开 (Y)', true, false, 24);
    this.drawButton(ctx, CARD_B, '取消 (X)', false, false, 24);
  }
});
