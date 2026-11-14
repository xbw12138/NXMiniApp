// 数独：单 canvas 渲染 + requestAnimationFrame
// 触摸直接点格子与按钮；手柄方向键移动焦点，A 确认，X 擦除，Y 提示，Start 新开一局。

var W = 1280;
var H = 720;

var BX = 40;
var BY = 112;
var CELL = 64;
var BOARD = CELL * 9;

var PX = 656;

var DIFFS = [
  { key: 'easy', label: '简单', removals: 40 },
  { key: 'medium', label: '中等', removals: 48 },
  { key: 'hard', label: '困难', removals: 54 }
];

function shuffle(a) {
  for (var i = a.length - 1; i > 0; i--) {
    var j = Math.floor(Math.random() * (i + 1));
    var t = a[i]; a[i] = a[j]; a[j] = t;
  }
  return a;
}

function canPlace(b, pos, v) {
  var r = Math.floor(pos / 9);
  var c = pos % 9;
  var i;
  for (i = 0; i < 9; i++) {
    if (b[r * 9 + i] === v) return false;
    if (b[i * 9 + c] === v) return false;
  }
  var br = Math.floor(r / 3) * 3;
  var bc = Math.floor(c / 3) * 3;
  for (i = 0; i < 3; i++) {
    for (var j = 0; j < 3; j++) {
      if (b[(br + i) * 9 + bc + j] === v) return false;
    }
  }
  return true;
}

function fillSolution(b, pos) {
  if (pos >= 81) return true;
  if (b[pos] !== 0) return fillSolution(b, pos + 1);
  var nums = shuffle([1, 2, 3, 4, 5, 6, 7, 8, 9]);
  for (var i = 0; i < 9; i++) {
    if (canPlace(b, pos, nums[i])) {
      b[pos] = nums[i];
      if (fillSolution(b, pos + 1)) return true;
      b[pos] = 0;
    }
  }
  return false;
}

function countSolutions(b, limit) {
  var pos = -1;
  for (var i = 0; i < 81; i++) {
    if (b[i] === 0) { pos = i; break; }
  }
  if (pos < 0) return 1;
  var total = 0;
  for (var v = 1; v <= 9; v++) {
    if (!canPlace(b, pos, v)) continue;
    b[pos] = v;
    total += countSolutions(b, limit);
    b[pos] = 0;
    if (total >= limit) return total;
  }
  return total;
}

function makePuzzle(removals) {
  var sol = [];
  for (var i = 0; i < 81; i++) sol.push(0);
  fillSolution(sol, 0);
  var puzzle = sol.slice();
  var order = [];
  for (var k = 0; k < 81; k++) order.push(k);
  shuffle(order);
  var removed = 0;
  for (var j = 0; j < order.length; j++) {
    if (removed >= removals) break;
    var idx = order[j];
    var old = puzzle[idx];
    puzzle[idx] = 0;
    if (countSolutions(puzzle.slice(), 2) !== 1) {
      puzzle[idx] = old;
    } else {
      removed++;
    }
  }
  return { puzzle: puzzle, solution: sol };
}

function indexOfDiff(key) {
  for (var i = 0; i < DIFFS.length; i++) {
    if (DIFFS[i].key === key) return i;
  }
  return 0;
}

function hasCell(x, y, rx, ry, rw, rh) {
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

Page({
  data: {},

  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.prevPad = {};
    this.repeatDir = null;
    this.repeatTimer = 0;
    this.lastTs = 0;
    this.clock = 0;
    this.msg = '';
    this.msgUntil = 0;
    this.overlay = null;
    this.errFlags = [];
    this.grid = null;
    this.focusArea = 'board';
    this.padIndex = 4;
    this.toolIndex = 0;
    this.diffIndex = 0;
    this.selected = 40;
    this.curR = 4;
    this.curC = 4;

    var save = this.loadSave();
    if (save && !save.done && this.isValidSave(save)) {
      this.restore(save);
    } else {
      this.startNew(save && save.difficulty ? save.difficulty : 'easy');
    }

    var loop = function (ts) {
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  onUnload: function () {
    this.persist();
  },

  // ---------------- 存档 ----------------

  loadSave: function () {
    var raw = null;
    try {
      raw = wx.getStorageSync('save');
    } catch (e) {
      raw = null;
    }
    if (!raw) return null;
    if (typeof raw === 'string') {
      try {
        return JSON.parse(raw);
      } catch (e2) {
        return null;
      }
    }
    return raw;
  },

  isValidSave: function (s) {
    return !!(s && s.grid && s.solution && s.given &&
      s.grid.length === 81 && s.solution.length === 81 && s.given.length === 81);
  },

  persist: function () {
    if (!this.grid || this.grid.length !== 81) return;
    wx.setStorageSync('save', JSON.stringify({
      grid: this.grid,
      given: this.given,
      solution: this.solution,
      hintCells: this.hintCells,
      seconds: Math.floor(this.seconds),
      mistakes: this.mistakes,
      hintsLeft: this.hintsLeft,
      difficulty: this.difficulty,
      done: !!this.done
    }));
  },

  restore: function (save) {
    this.grid = save.grid.slice();
    this.solution = save.solution.slice();
    this.given = [];
    for (var i = 0; i < 81; i++) this.given.push(!!save.given[i]);
    this.hintCells = save.hintCells ? save.hintCells.slice() : [];
    this.seconds = Number(save.seconds) || 0;
    this.mistakes = Number(save.mistakes) || 0;
    this.hintsLeft = save.hintsLeft === undefined ? 3 : Number(save.hintsLeft);
    this.difficulty = save.difficulty || 'easy';
    this.done = false;
    this.selected = 40;
    this.curR = 4;
    this.curC = 4;
    this.focusArea = 'board';
    this.overlay = null;
    this.lastSaveAt = Math.floor(this.seconds);
    this.computeErrors();
    this.say('已恢复上次的进度');
  },

  // ---------------- 棋局 ----------------

  startNew: function (diffKey) {
    var di = indexOfDiff(diffKey);
    var p = makePuzzle(DIFFS[di].removals);
    this.difficulty = DIFFS[di].key;
    this.solution = p.solution;
    this.grid = p.puzzle.slice();
    this.given = [];
    for (var i = 0; i < 81; i++) this.given.push(p.puzzle[i] !== 0);
    this.hintCells = [];
    this.selected = 40;
    this.curR = 4;
    this.curC = 4;
    this.focusArea = 'board';
    this.padIndex = 4;
    this.toolIndex = 0;
    this.diffIndex = indexOfDiff(diffKey);
    this.seconds = 0;
    this.mistakes = 0;
    this.hintsLeft = 3;
    this.done = false;
    this.overlay = null;
    this.lastSaveAt = 0;
    this.computeErrors();
    this.say('新棋局开始');
    this.persist();
  },

  switchDifficulty: function (key) {
    if (this.difficulty === key && !this.done) return;
    if (this.done || !this.hasProgress()) {
      this.overlay = null;
      this.startNew(key);
      return;
    }
    this.overlay = { kind: 'confirm', action: 'diff', diff: key, sel: 0 };
  },

  newGame: function () {
    if (this.done || !this.hasProgress()) {
      this.overlay = null;
      this.startNew(this.difficulty);
      return;
    }
    this.overlay = { kind: 'confirm', action: 'new', diff: this.difficulty, sel: 0 };
  },

  hasProgress: function () {
    if (!this.grid) return false;
    for (var i = 0; i < 81; i++) {
      if (!this.given[i] && this.grid[i] !== 0) return true;
    }
    return false;
  },

  selectCell: function (i) {
    this.selected = i;
    this.curR = Math.floor(i / 9);
    this.curC = i % 9;
    this.focusArea = 'board';
  },

  enterNumber: function (n) {
    if (this.done) {
      this.say('本局已完成，按 Start 再来一局');
      return;
    }
    var i = this.selected;
    if (i < 0) {
      this.say('先选一个空格');
      return;
    }
    if (this.given[i]) {
      this.say('题目数字不可修改');
      return;
    }
    var old = this.grid[i];
    this.grid[i] = n;
    if (n !== this.solution[i] && n !== old) {
      this.mistakes++;
      wx.vibrateShort({ type: 'heavy' });
      this.say('这个数字不对，再想想');
    } else if (n === this.solution[i]) {
      wx.vibrateShort({ type: 'light' });
    }
    this.computeErrors();
    this.persist();
    this.checkWin();
  },

  erase: function () {
    if (this.done) {
      this.say('本局已完成，按 Start 再来一局');
      return;
    }
    var i = this.selected;
    if (i < 0) {
      this.say('先选一个可编辑格');
      return;
    }
    if (this.given[i]) {
      this.say('题目数字不可修改');
      return;
    }
    if (this.grid[i] === 0) return;
    this.grid[i] = 0;
    this.computeErrors();
    this.persist();
  },

  hint: function () {
    if (this.done) {
      this.say('本局已完成，按 Start 再来一局');
      return;
    }
    if (this.hintsLeft <= 0) {
      this.say('提示已用完');
      return;
    }
    var target = this.selected;
    if (target < 0 || this.given[target] || this.grid[target] === this.solution[target]) {
      target = -1;
      for (var i = 0; i < 81; i++) {
        if (this.grid[i] !== this.solution[i]) { target = i; break; }
      }
    }
    if (target < 0) {
      this.say('已经填完了');
      return;
    }
    this.grid[target] = this.solution[target];
    this.given[target] = true;
    this.hintCells.push(target);
    this.selected = target;
    this.curR = Math.floor(target / 9);
    this.curC = target % 9;
    this.hintsLeft--;
    this.computeErrors();
    wx.vibrateShort({ type: 'light' });
    this.say('已填入一个提示数字');
    this.persist();
    this.checkWin();
  },

  computeErrors: function () {
    var flags = [];
    for (var i = 0; i < 81; i++) {
      flags.push(!this.given[i] && this.grid[i] !== 0 && this.grid[i] !== this.solution[i]);
    }
    this.errFlags = flags;
  },

  checkWin: function () {
    for (var i = 0; i < 81; i++) {
      if (this.grid[i] !== this.solution[i]) return;
    }
    this.done = true;
    this.winTime = Math.floor(this.seconds);
    this.overlay = { kind: 'win', sel: 0 };
    this.persist();
    wx.vibrateShort({ type: 'heavy' });
  },

  // ---------------- 循环 ----------------

  tick: function (ts) {
    var dt = this.lastTs ? (ts - this.lastTs) / 1000 : 0.016;
    this.lastTs = ts;
    if (dt > 0.05) dt = 0.05;
    if (dt <= 0) dt = 0.016;
    this.handlePad(dt);
    this.update(dt);
    this.draw();
  },

  update: function (dt) {
    this.clock += dt;
    if (!this.done) {
      this.seconds += dt;
      if (this.seconds - this.lastSaveAt >= 10) {
        this.lastSaveAt = this.seconds;
        this.persist();
      }
    }
  },

  // ---------------- 手柄 ----------------

  handlePad: function (dt) {
    if (!wx.getGamepadState) return;
    var pad = wx.getGamepadState();
    if (!pad) return;
    var b = pad.buttons || {};
    var axes = pad.axes || [0, 0];

    var dir = null;
    if (b.up) dir = 'up';
    else if (b.down) dir = 'down';
    else if (b.left) dir = 'left';
    else if (b.right) dir = 'right';

    if (!dir) {
      var ax = axes[0] || 0;
      var ay = axes[1] || 0;
      if (ax < -0.6) dir = 'left';
      else if (ax > 0.6) dir = 'right';
      else if (ay < -0.7) dir = 'up';
      else if (ay > 0.7) dir = 'down';
    }

    if (dir && dir !== this.repeatDir) {
      this.moveFocus(dir);
      this.repeatDir = dir;
      this.repeatTimer = 0.30;
    } else if (dir) {
      if (!this.overlay) {
        this.repeatTimer -= dt;
        if (this.repeatTimer <= 0) {
          this.moveFocus(dir);
          this.repeatTimer = 0.14;
        }
      }
    } else {
      this.repeatDir = null;
      this.repeatTimer = 0;
    }

    if (b.a && !this.prevPad.a) this.onA();
    if (b.x && !this.prevPad.x) this.onX();
    if (b.y && !this.prevPad.y) this.onY();
    if (b.start && !this.prevPad.start) this.onStart();

    this.prevPad = { a: !!b.a, x: !!b.x, y: !!b.y, start: !!b.start };
  },

  moveFocus: function (dir) {
    if (this.overlay) {
      if (dir === 'left' || dir === 'right') {
        this.overlay.sel = this.overlay.sel === 0 ? 1 : 0;
      }
      return;
    }

    if (this.focusArea === 'board') {
      if (dir === 'left') {
        if (this.curC > 0) this.selectCell(this.curR * 9 + this.curC - 1);
      } else if (dir === 'right') {
        if (this.curC < 8) {
          this.selectCell(this.curR * 9 + this.curC + 1);
        } else {
          this.focusArea = 'pad';
          this.padIndex = Math.min(2, Math.floor(this.curR / 3)) * 3;
        }
      } else if (dir === 'up') {
        if (this.curR > 0) this.selectCell((this.curR - 1) * 9 + this.curC);
      } else if (dir === 'down') {
        if (this.curR < 8) this.selectCell((this.curR + 1) * 9 + this.curC);
      }
      return;
    }

    if (this.focusArea === 'pad') {
      var row = Math.floor(this.padIndex / 3);
      var col = this.padIndex % 3;
      if (dir === 'left') {
        if (col > 0) this.padIndex = row * 3 + col - 1;
        else {
          this.focusArea = 'board';
          this.selectCell((row * 3 + 1) * 9 + 8);
        }
      } else if (dir === 'right') {
        if (col < 2) this.padIndex = row * 3 + col + 1;
      } else if (dir === 'up') {
        if (row > 0) this.padIndex = (row - 1) * 3 + col;
        else {
          this.focusArea = 'diff';
          this.diffIndex = col;
        }
      } else if (dir === 'down') {
        if (row < 2) this.padIndex = (row + 1) * 3 + col;
        else {
          this.focusArea = 'tools';
          this.toolIndex = col;
        }
      }
      return;
    }

    if (this.focusArea === 'tools') {
      if (dir === 'left') {
        if (this.toolIndex > 0) this.toolIndex--;
      } else if (dir === 'right') {
        if (this.toolIndex < 2) this.toolIndex++;
      } else if (dir === 'up') {
        this.focusArea = 'pad';
        this.padIndex = 6 + this.toolIndex;
      }
      return;
    }

    if (this.focusArea === 'diff') {
      if (dir === 'left') {
        if (this.diffIndex > 0) this.diffIndex--;
      } else if (dir === 'right') {
        if (this.diffIndex < 2) this.diffIndex++;
      } else if (dir === 'down') {
        this.focusArea = 'pad';
        this.padIndex = (this.curR >= 3 && this.curR <= 5 ? 3 : (this.curR > 5 ? 6 : 0)) + this.diffIndex;
      }
    }
  },

  onA: function () {
    if (this.overlay) {
      if (this.overlay.kind === 'confirm') {
        if (this.overlay.sel === 1) this.confirmAccept();
        else this.overlay = null;
        return;
      }
      if (this.overlay.kind === 'win') {
        if (this.overlay.sel === 0) {
          this.overlay = null;
          this.startNew(this.difficulty);
        } else {
          this.overlay = null;
        }
        return;
      }
    }

    if (this.focusArea === 'board') {
      this.selectCell(this.curR * 9 + this.curC);
      this.focusArea = 'pad';
    } else if (this.focusArea === 'pad') {
      this.enterNumber(this.padIndex + 1);
      this.focusArea = 'board';
    } else if (this.focusArea === 'tools') {
      if (this.toolIndex === 0) this.erase();
      else if (this.toolIndex === 1) this.hint();
      else this.newGame();
    } else if (this.focusArea === 'diff') {
      this.switchDifficulty(DIFFS[this.diffIndex].key);
    }
  },

  onX: function () {
    if (this.overlay) {
      this.overlay = null;
      return;
    }
    this.erase();
  },

  onY: function () {
    if (this.overlay) return;
    this.hint();
  },

  onStart: function () {
    this.overlay = null;
    this.startNew(this.difficulty);
  },

  confirmAccept: function () {
    var ov = this.overlay;
    this.overlay = null;
    if (!ov) return;
    if (ov.action === 'diff') this.startNew(ov.diff);
    else this.startNew(this.difficulty);
  },

  // ---------------- 触摸 ----------------

  onTouchStart: function (e) {
    var p = this.pointOf(e);
    if (!p) return;
    this.handleTap(p.x, p.y);
  },

  onTouchMove: function () {},

  onTouchEnd: function () {},

  pointOf: function (e) {
    if (e.detail && e.detail.x !== undefined && e.detail.y !== undefined) {
      return { x: e.detail.x, y: e.detail.y };
    }
    if (e.x !== undefined && e.y !== undefined) return { x: e.x, y: e.y };
    var t = (e.touches && e.touches[0]) || (e.changedTouches && e.changedTouches[0]);
    if (t && t.clientX !== undefined) return { x: t.clientX, y: t.clientY };
    return null;
  },

  handleTap: function (x, y) {
    var i;
    if (this.overlay) {
      if (this.overlay.kind === 'confirm') {
        if (hasCell(x, y, 412, 408, 210, 58)) {
          this.overlay.sel = 0;
          this.overlay = null;
        } else if (hasCell(x, y, 658, 408, 210, 58)) {
          this.overlay.sel = 1;
          this.confirmAccept();
        }
      } else if (this.overlay.kind === 'win') {
        if (hasCell(x, y, 402, 420, 220, 62)) {
          this.overlay = null;
          this.startNew(this.difficulty);
        } else if (hasCell(x, y, 658, 420, 220, 62)) {
          this.overlay = null;
        }
      }
      return;
    }

    for (i = 0; i < 3; i++) {
      if (hasCell(x, y, 854 + i * 130, 24, 118, 48)) {
        this.focusArea = 'diff';
        this.diffIndex = i;
        this.switchDifficulty(DIFFS[i].key);
        return;
      }
    }

    for (i = 0; i < 9; i++) {
      if (hasCell(x, y, PX + (i % 3) * 200, 204 + Math.floor(i / 3) * 92, 176, 78)) {
        this.focusArea = 'pad';
        this.padIndex = i;
        this.enterNumber(i + 1);
        return;
      }
    }

    for (i = 0; i < 3; i++) {
      if (hasCell(x, y, PX + i * 200, 486, 176, 68)) {
        this.focusArea = 'tools';
        this.toolIndex = i;
        if (i === 0) this.erase();
        else if (i === 1) this.hint();
        else this.newGame();
        return;
      }
    }

    if (x >= BX && x < BX + BOARD && y >= BY && y < BY + BOARD) {
      var c = Math.floor((x - BX) / CELL);
      var r = Math.floor((y - BY) / CELL);
      this.selectCell(r * 9 + c);
    }
  },

  say: function (text) {
    this.msg = text;
    this.msgUntil = this.clock + 2.4;
  },

  fmt: function (s) {
    var m = Math.floor(s / 60);
    var sec = Math.floor(s % 60);
    return (m < 10 ? '0' + m : '' + m) + ':' + (sec < 10 ? '0' + sec : '' + sec);
  },

  diffLabel: function () {
    return DIFFS[indexOfDiff(this.difficulty)].label;
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;

    ctx.setFillStyle('#0f1117');
    ctx.fillRect(0, 0, W, H);
    ctx.setFillStyle('#121722');
    ctx.fillRect(0, 0, W, 88);
    ctx.setFillStyle('#1c2433');
    ctx.fillRect(0, 88, W, 2);

    ctx.setTextAlign('left');
    ctx.setFontSize(52);
    ctx.setFillStyle('#f3f6fb');
    ctx.fillText('数独', 40, 66);
    ctx.setFontSize(20);
    ctx.setFillStyle('#6f7a8d');
    ctx.fillText('九宫格 · 静心推理', 158, 64);

    this.drawDiff(ctx);
    this.drawBoard(ctx);
    this.drawStats(ctx);
    this.drawPad(ctx);
    this.drawTools(ctx);
    this.drawInfo(ctx);
    this.drawBottom(ctx);

    if (this.overlay) this.drawOverlay(ctx);

    ctx.draw();
  },

  drawBtn: function (ctx, x, y, w, h, label, fill, border, color, font, focused) {
    ctx.setFillStyle(fill);
    ctx.fillRect(x, y, w, h);
    ctx.setLineWidth(focused ? 3 : 2);
    ctx.setStrokeStyle(focused ? '#ffd166' : border);
    ctx.strokeRect(x, y, w, h);
    ctx.setFillStyle(color);
    ctx.setFontSize(font);
    ctx.setTextAlign('center');
    ctx.fillText(label, x + w / 2, y + h / 2 + font * 0.35);
    ctx.setTextAlign('left');
  },

  drawDiff: function (ctx) {
    for (var i = 0; i < DIFFS.length; i++) {
      var d = DIFFS[i];
      var on = this.difficulty === d.key;
      var focused = this.focusArea === 'diff' && this.diffIndex === i && !this.overlay;
      this.drawBtn(ctx, 854 + i * 130, 24, 118, 48, d.label,
        on ? '#2c4270' : '#181d28',
        on ? '#4d7cff' : '#262f40',
        on ? '#ffffff' : '#aeb8c8',
        26, focused);
    }
  },

  drawBoard: function (ctx) {
    var sel = this.selected;
    var sr = sel >= 0 ? Math.floor(sel / 9) : -1;
    var sc = sel >= 0 ? sel % 9 : -1;
    var selVal = sel >= 0 ? this.grid[sel] : 0;
    var r;
    var c;
    var i;
    var x;
    var y;
    var v;

    ctx.setFillStyle('#141822');
    ctx.fillRect(BX - 10, BY - 10, BOARD + 20, BOARD + 20);

    for (r = 0; r < 9; r++) {
      for (c = 0; c < 9; c++) {
        i = r * 9 + c;
        x = BX + c * CELL;
        y = BY + r * CELL;
        ctx.setFillStyle(((Math.floor(r / 3) + Math.floor(c / 3)) % 2) ? '#1d2433' : '#1a2030');
        ctx.fillRect(x, y, CELL, CELL);
        if (sel >= 0 && i !== sel) {
          var sameBox = Math.floor(r / 3) === Math.floor(sr / 3) &&
                        Math.floor(c / 3) === Math.floor(sc / 3);
          if (r === sr || c === sc || sameBox) {
            ctx.setFillStyle('#242f42');
            ctx.fillRect(x, y, CELL, CELL);
          }
        }
        v = this.grid[i];
        if (selVal && i !== sel && v === selVal) {
          ctx.setFillStyle('#31465f');
          ctx.fillRect(x, y, CELL, CELL);
        }
        if (this.errFlags[i]) {
          ctx.setFillStyle('#3a2028');
          ctx.fillRect(x, y, CELL, CELL);
        }
        if (i === sel) {
          ctx.setFillStyle('#2b3f63');
          ctx.fillRect(x, y, CELL, CELL);
        }
      }
    }

    for (i = 0; i <= 9; i++) {
      var thick = i % 3 === 0;
      ctx.setLineWidth(thick ? 3 : 1);
      ctx.setStrokeStyle(thick ? '#4a5c7a' : '#2a3344');
      x = BX + i * CELL;
      ctx.beginPath();
      ctx.moveTo(x, BY);
      ctx.lineTo(x, BY + BOARD);
      ctx.stroke();
      y = BY + i * CELL;
      ctx.beginPath();
      ctx.moveTo(BX, y);
      ctx.lineTo(BX + BOARD, y);
      ctx.stroke();
    }

    ctx.setLineWidth(4);
    ctx.setStrokeStyle('#56688a');
    ctx.strokeRect(BX, BY, BOARD, BOARD);

    ctx.setTextAlign('center');
    ctx.setFontSize(38);
    for (r = 0; r < 9; r++) {
      for (c = 0; c < 9; c++) {
        i = r * 9 + c;
        v = this.grid[i];
        if (!v) continue;
        if (this.hintCells.indexOf(i) >= 0) ctx.setFillStyle('#6ee7a8');
        else if (this.errFlags[i]) ctx.setFillStyle('#ff6b6b');
        else if (this.given[i]) ctx.setFillStyle('#93a0b5');
        else ctx.setFillStyle('#eef3fb');
        ctx.fillText('' + v, BX + c * CELL + CELL / 2, BY + r * CELL + CELL / 2 + 13);
      }
    }
    ctx.setTextAlign('left');

    if (sel >= 0) {
      ctx.setLineWidth(4);
      ctx.setStrokeStyle(this.focusArea === 'board' && !this.overlay ? '#7ad7ff' : '#4d7cff');
      ctx.strokeRect(BX + sc * CELL + 2, BY + sr * CELL + 2, CELL - 4, CELL - 4);
    }
  },

  drawStats: function (ctx) {
    var filled = 0;
    for (var i = 0; i < 81; i++) {
      if (this.grid[i] !== 0) filled++;
    }
    var values = [
      this.fmt(this.seconds),
      '' + this.mistakes,
      '' + this.hintsLeft,
      Math.round(filled / 81 * 100) + '%'
    ];
    var labels = ['时间', '错误', '提示', '进度'];
    for (var k = 0; k < 4; k++) {
      var x = PX + k * 147;
      var y = 92;
      ctx.setFillStyle('#181d28');
      ctx.fillRect(x, y, 135, 62);
      ctx.setLineWidth(1);
      ctx.setStrokeStyle('#252d3c');
      ctx.strokeRect(x, y, 135, 62);
      ctx.setTextAlign('center');
      ctx.setFontSize(18);
      ctx.setFillStyle('#737f92');
      ctx.fillText(labels[k], x + 67, y + 24);
      ctx.setFontSize(26);
      ctx.setFillStyle(labels[k] === '错误' && this.mistakes > 0 ? '#ff8f8f' : '#e9eef8');
      ctx.fillText(values[k], x + 67, y + 51);
      ctx.setTextAlign('left');
    }
  },

  drawPad: function (ctx) {
    ctx.setTextAlign('left');
    ctx.setFontSize(22);
    ctx.setFillStyle('#738095');
    ctx.fillText('数字键盘', PX, 190);

    for (var i = 0; i < 9; i++) {
      var focused = this.focusArea === 'pad' && this.padIndex === i && !this.overlay;
      this.drawBtn(ctx, PX + (i % 3) * 200, 204 + Math.floor(i / 3) * 92, 176, 78,
        '' + (i + 1),
        focused ? '#22304a' : '#1a2130',
        '#283247',
        '#eef3fb',
        36, focused);
    }
  },

  drawTools: function (ctx) {
    var labels = ['擦除', '提示 ' + this.hintsLeft, '新棋局'];
    for (var i = 0; i < 3; i++) {
      var focused = this.focusArea === 'tools' && this.toolIndex === i && !this.overlay;
      var main = i === 2;
      this.drawBtn(ctx, PX + i * 200, 486, 176, 68, labels[i],
        main ? '#4d7cff' : '#202836',
        main ? '#6d94ff' : '#2b3648',
        main ? '#ffffff' : '#cdd7e6',
        26, focused);
    }
  },

  drawInfo: function (ctx) {
    ctx.setTextAlign('left');
    ctx.setFontSize(20);
    ctx.setFillStyle('#6f7a8d');
    ctx.fillText('方向键移动光标 · A 确认 · X 擦除', PX, 592);
    ctx.fillText('Y 提示 · Start 新开一局', PX, 622);
    ctx.fillText('切换难度会开始一盘新棋局', PX, 652);
    ctx.setFillStyle('#7c8799');
    ctx.fillText('绿色数字为提示填入，红色为错误', PX, 682);
  },

  drawBottom: function (ctx) {
    ctx.setTextAlign('center');
    if (this.clock < this.msgUntil && this.msg) {
      ctx.setFontSize(22);
      ctx.setFillStyle('#ffd166');
      ctx.fillText(this.msg, W / 2, 714);
    } else if (this.done) {
      ctx.setFontSize(21);
      ctx.setFillStyle('#6ee7a8');
      ctx.fillText('本局已完成 · 按 Start 或点「新棋局」再来一局', W / 2, 714);
    } else {
      ctx.setFontSize(20);
      ctx.setFillStyle('#4f5a6c');
      ctx.fillText('触摸可直接点格子与按钮 · 进度每 10 秒自动保存', W / 2, 714);
    }
    ctx.setTextAlign('left');
  },

  drawOverlay: function (ctx) {
    ctx.setFillStyle('rgba(6,8,12,0.78)');
    ctx.fillRect(0, 0, W, H);

    if (this.overlay.kind === 'confirm') {
      ctx.setFillStyle('#141a26');
      ctx.fillRect(330, 230, 620, 250);
      ctx.setLineWidth(2);
      ctx.setStrokeStyle('#3a4a68');
      ctx.strokeRect(330, 230, 620, 250);

      ctx.setTextAlign('center');
      ctx.setFontSize(36);
      ctx.setFillStyle('#f3f6fb');
      ctx.fillText('开始新棋局？', 640, 294);

      ctx.setFontSize(22);
      ctx.setFillStyle('#aab6c8');
      if (this.overlay.action === 'diff') {
        ctx.fillText('切换到「' + DIFFS[indexOfDiff(this.overlay.diff)].label + '」会开始一盘新棋局。', 640, 340);
      } else {
        ctx.fillText('将重新生成一盘同难度的棋局。', 640, 340);
      }
      ctx.fillText('当前进度会清空。', 640, 372);

      this.drawBtn(ctx, 412, 408, 210, 58, '取消', '#1a2130', '#2b3648', '#cdd7e6', 26,
        this.overlay.sel === 0);
      this.drawBtn(ctx, 658, 408, 210, 58, '确定', '#2c4270', '#4d7cff', '#ffffff', 26,
        this.overlay.sel === 1);

      ctx.setFontSize(20);
      ctx.setFillStyle('#6f7a8d');
      ctx.fillText('A 确认选择 · X 取消', 640, 502);
      ctx.setTextAlign('left');
      return;
    }

    var timeText = this.fmt(this.winTime || this.seconds);
    ctx.setFillStyle('#141a26');
    ctx.fillRect(300, 200, 680, 320);
    ctx.setLineWidth(2);
    ctx.setStrokeStyle('#2c4270');
    ctx.strokeRect(300, 200, 680, 320);

    ctx.setTextAlign('center');
    ctx.setFontSize(54);
    ctx.setFillStyle('#6ee7a8');
    ctx.fillText('完成！', 640, 290);

    ctx.setFontSize(26);
    ctx.setFillStyle('#dbe4f2');
    ctx.fillText('用时 ' + timeText + ' · 错误 ' + this.mistakes + ' 次', 640, 344);

    ctx.setFontSize(22);
    ctx.setFillStyle('#8b96a8');
    ctx.fillText('难度 ' + this.diffLabel() + ' · 进度 100%', 640, 380);

    this.drawBtn(ctx, 402, 420, 220, 62, '再来一局', '#4d7cff', '#6d94ff', '#ffffff', 26,
      this.overlay.sel === 0);
    this.drawBtn(ctx, 658, 420, 220, 62, '欣赏一下', '#202836', '#2b3648', '#cdd7e6', 26,
      this.overlay.sel === 1);

    ctx.setFontSize(20);
    ctx.setFillStyle('#6f7a8d');
    ctx.fillText('A 确认 · Start 再来一局', 640, 500);
    ctx.setTextAlign('left');
  }
});
