// UNO · 单 canvas 渲染 + requestAnimationFrame
// 操作：左右选牌，A 出牌，X 摸牌，Y 过牌；万能牌方向键选色 + A 确认；触摸可直接操作。

var W = 1280;
var H = 720;
var COLORS = ['r', 'y', 'g', 'b'];
var COLOR_NAME = { r: '红', y: '黄', g: '绿', b: '蓝' };
var COLOR_HEX = { r: '#e11d48', y: '#facc15', g: '#16a34a', b: '#2563eb', w: '#1f2937' };
var REPEAT = 0.14;
var AI_THINK = 0.75;
var AI_ACT = 0.55;
var PASS_DELAY = 0.85;

function clamp(v, a, b) {
  return v < a ? a : (v > b ? b : v);
}

function inRect(x, y, r) {
  return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// ---------------- 牌与规则 ----------------

function makeCard(id, color, kind, label, num) {
  return { id: id, color: color, kind: kind, label: label, num: num };
}

function buildDeck() {
  var deck = [];
  var id = 0;
  var i;
  var n;
  for (i = 0; i < COLORS.length; i++) {
    var c = COLORS[i];
    deck.push(makeCard(id++, c, 'num', '0', 0));
    for (n = 1; n <= 9; n++) {
      deck.push(makeCard(id++, c, 'num', String(n), n));
      deck.push(makeCard(id++, c, 'num', String(n), n));
    }
    for (n = 0; n < 2; n++) {
      deck.push(makeCard(id++, c, 'skip', '禁', -1));
      deck.push(makeCard(id++, c, 'rev', '反', -1));
      deck.push(makeCard(id++, c, 'd2', '+2', -1));
    }
  }
  for (i = 0; i < 4; i++) {
    deck.push(makeCard(id++, 'w', 'wild', '变', -1));
    deck.push(makeCard(id++, 'w', 'wd4', '+4', -1));
  }
  return deck;
}

function shuffle(deck) {
  for (var i = deck.length - 1; i > 0; i--) {
    var j = Math.floor(Math.random() * (i + 1));
    var t = deck[i];
    deck[i] = deck[j];
    deck[j] = t;
  }
  return deck;
}

function canPlay(card, top, color) {
  if (!card) return false;
  if (card.kind === 'wild' || card.kind === 'wd4') return true;
  if (card.color === color) return true;
  if (!top) return false;
  if (card.kind === 'num' && top.kind === 'num') return card.num === top.num;
  if (card.kind !== 'num' && card.kind === top.kind) return true;
  return false;
}

function drawOne(g) {
  if (!g.deck.length) {
    var top = g.discard.pop();
    g.deck = shuffle(g.discard.slice());
    g.discard = [top];
    if (!g.deck.length) g.deck = shuffle(buildDeck());
  }
  return g.deck.pop();
}

function drawTo(g, player, count) {
  for (var i = 0; i < count; i++) player.hand.push(drawOne(g));
}

function pickColor(player) {
  var counts = { r: 0, y: 0, g: 0, b: 0 };
  for (var i = 0; i < player.hand.length; i++) {
    var c = player.hand[i].color;
    if (counts[c] != null) counts[c] += 1;
  }
  var best = 'r';
  var bestCount = -1;
  for (var j = 0; j < COLORS.length; j++) {
    if (counts[COLORS[j]] > bestCount) {
      bestCount = counts[COLORS[j]];
      best = COLORS[j];
    }
  }
  if (bestCount <= 0) best = COLORS[Math.floor(Math.random() * 4)];
  return best;
}

function aiPick(cards) {
  var best = null;
  var bestRank = -1;
  for (var i = 0; i < cards.length; i++) {
    var card = cards[i];
    var rank;
    if (card.kind === 'wild') rank = 0;
    else if (card.kind === 'wd4') rank = 1;
    else if (card.kind === 'num') rank = 2;
    else if (card.kind === 'rev') rank = 3;
    else if (card.kind === 'skip') rank = 4;
    else rank = 5;
    if (rank > bestRank) {
      bestRank = rank;
      best = card;
    }
  }
  return best;
}

Page({
  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.time = 0;
    this.lastTs = 0;
    this.phase = 'menu';
    this.playerCount = 2;
    this.menuIndex = 0;
    this.colorSel = 0;
    this.game = null;
    this.sel = 0;
    this.winner = -1;
    this.msg = '选择人数开始牌局';
    this.msgAt = 0;
    this.pending = null;
    this.pendingAI = null;
    this.pendingPass = false;
    this.aiTimer = 0;
    this.passTimer = 0;
    this.throwAt = -10;
    this.padPrev = {};
    this.repeatKey = '';
    this.repeatTimer = 0;

    var loop = function (ts) {
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  // ---------------- 牌局状态 ----------------

  say: function (text) {
    this.msg = text;
    this.msgAt = this.time;
  },

  startGame: function (count) {
    count = clamp(count, 2, 4);
    this.playerCount = count;
    var deck = shuffle(buildDeck());
    var players = [];
    var i;
    for (i = 0; i < count; i++) {
      players.push({
        id: i,
        name: i === 0 ? '你' : '电脑 ' + i,
        ai: i !== 0,
        hand: []
      });
    }
    for (i = 0; i < 7; i++) {
      for (var p = 0; p < players.length; p++) players[p].hand.push(deck.pop());
    }
    var first = null;
    while (deck.length) {
      var card = deck.pop();
      if (card.kind === 'num') {
        first = card;
        break;
      }
      deck.unshift(card);
    }
    if (!first) first = makeCard(999, 'r', 'num', '0', 0);

    this.game = {
      deck: deck,
      discard: [first],
      color: first.color,
      players: players,
      turn: 0,
      dir: 1,
      drawn: false
    };
    this.phase = 'play';
    this.winner = -1;
    this.sel = 0;
    this.pending = null;
    this.pendingAI = null;
    this.pendingPass = false;
    this.aiTimer = 0;
    this.passTimer = 0;
    this.throwAt = -10;
    this.colorSel = 0;
    this.say('牌局开始，你先出牌');
    wx.vibrateShort({ type: 'light' });
  },

  toMenu: function () {
    this.phase = 'menu';
    this.game = null;
    this.pending = null;
    this.pendingAI = null;
    this.pendingPass = false;
    this.aiTimer = 0;
    this.passTimer = 0;
    this.winner = -1;
    this.sel = 0;
    this.say('选择人数开始牌局');
  },

  restart: function () {
    this.startGame(this.playerCount);
  },

  cardName: function (card) {
    if (card.kind === 'wild') return '万能牌';
    if (card.kind === 'wd4') return '万能 +4';
    return COLOR_NAME[card.color] + card.label;
  },

  clampSel: function () {
    var n = this.game ? this.game.players[0].hand.length : 0;
    if (n <= 0) {
      this.sel = 0;
      return;
    }
    this.sel = clamp(this.sel, 0, n - 1);
  },

  canPass: function () {
    return !!(this.game && this.phase === 'play' && this.game.turn === 0 && this.game.drawn);
  },

  // ---------------- 玩家操作 ----------------

  moveSel: function (dx) {
    if (!this.game || this.phase !== 'play' || this.game.turn !== 0) return;
    var n = this.game.players[0].hand.length;
    if (!n) return;
    this.sel = clamp(this.sel + dx, 0, n - 1);
  },

  tryPlaySel: function () {
    var g = this.game;
    if (!g || this.phase !== 'play') return;
    if (g.turn !== 0) {
      this.say('还没轮到你出牌');
      return;
    }
    var card = g.players[0].hand[this.sel];
    if (!card) return;
    var top = g.discard[g.discard.length - 1];
    if (!canPlay(card, top, g.color)) {
      this.say('这张牌现在不能出');
      wx.vibrateShort({ type: 'light' });
      return;
    }
    if (card.kind === 'wild' || card.kind === 'wd4') {
      this.pending = card;
      this.colorSel = COLORS.indexOf(pickColor(g.players[0]));
      if (this.colorSel < 0) this.colorSel = 0;
      this.phase = 'color';
      this.say('选择万能牌的颜色');
      wx.vibrateShort({ type: 'light' });
      return;
    }
    this.playCard(card, null);
  },

  onDraw: function () {
    var g = this.game;
    if (!g || this.phase !== 'play') return;
    if (g.turn !== 0) {
      this.say('还没轮到你出牌');
      return;
    }
    if (g.drawn) {
      this.say('本回合已经抽过牌了');
      return;
    }
    var card = drawOne(g);
    g.players[0].hand.push(card);
    g.drawn = true;
    this.sel = g.players[0].hand.length - 1;
    var top = g.discard[g.discard.length - 1];
    var name = this.cardName(card);
    if (canPlay(card, top, g.color)) {
      this.say('抽到 ' + name + '，可以出牌，也可以按 Y 过牌');
      this.passTimer = 0;
    } else {
      this.say('抽到 ' + name + '，不能出，自动过牌');
      this.passTimer = PASS_DELAY;
    }
    wx.vibrateShort({ type: 'light' });
  },

  onPass: function () {
    var g = this.game;
    if (!g || this.phase !== 'play' || g.turn !== 0) return;
    if (!g.drawn) {
      this.say('先抽一张才能过牌');
      return;
    }
    this.passTimer = 0;
    this.passTurn();
    wx.vibrateShort({ type: 'light' });
  },

  passTurn: function () {
    var g = this.game;
    if (!g || this.phase !== 'play') return;
    this.advanceTurn(1);
    this.say(g.turn === 0 ? '轮到你出牌' : '等待 ' + g.players[g.turn].name);
    this.clampSel();
    this.scheduleAI();
  },

  advanceTurn: function (step) {
    var g = this.game;
    var n = g.players.length;
    g.drawn = false;
    g.turn = ((g.turn + g.dir * step) % n + n) % n;
  },

  confirmColor: function () {
    var g = this.game;
    if (!g || this.phase !== 'color' || !this.pending) return;
    var card = this.pending;
    var color = COLORS[this.colorSel];
    this.pending = null;
    this.phase = 'play';
    this.playCard(card, color);
  },

  playCard: function (card, chosenColor) {
    var g = this.game;
    if (!g || this.phase !== 'play') return;
    var player = g.players[g.turn];
    var idx = -1;
    var i;
    for (i = 0; i < player.hand.length; i++) {
      if (player.hand[i].id === card.id) {
        idx = i;
        break;
      }
    }
    if (idx < 0) return;
    player.hand.splice(idx, 1);
    g.discard.push(card);
    if (card.kind === 'wild' || card.kind === 'wd4') {
      g.color = chosenColor || 'r';
    }
    this.throwAt = this.time;

    var n = g.players.length;
    var step = 1;
    var msg = player.name + ' 打出 ' + this.cardName(card);
    if (card.kind === 'rev') {
      g.dir *= -1;
      step = n === 2 ? 2 : 1;
      msg += '，方向反转';
    } else if (card.kind === 'skip') {
      step = 2;
      msg += '，下家被跳过';
    } else if (card.kind === 'd2') {
      var victim = g.players[((g.turn + g.dir) % n + n) % n];
      drawTo(g, victim, 2);
      step = 2;
      msg += '，' + victim.name + ' 摸 2 张';
    } else if (card.kind === 'wd4') {
      var victim2 = g.players[((g.turn + g.dir) % n + n) % n];
      drawTo(g, victim2, 4);
      step = 2;
      msg += '，' + victim2.name + ' 摸 4 张';
    } else if (card.kind === 'wild') {
      msg += '，颜色变为' + COLOR_NAME[g.color];
    }
    if (player.hand.length === 1) msg += ' · UNO！';
    this.say(msg);
    g.drawn = false;
    this.passTimer = 0;

    if (player.hand.length === 0) {
      this.phase = 'over';
      this.winner = player.id;
      this.say(player.name + ' 出完了所有手牌');
      wx.vibrateShort({ type: 'heavy' });
      return;
    }

    if (player.id === 0) wx.vibrateShort({ type: 'light' });
    g.turn = ((g.turn + g.dir * step) % n + n) % n;
    this.clampSel();
    this.scheduleAI();
  },

  // ---------------- 电脑 ----------------

  scheduleAI: function () {
    this.pendingAI = null;
    this.pendingPass = false;
    this.aiTimer = 0;
    if (!this.game || this.phase !== 'play') return;
    if (!this.game.players[this.game.turn].ai) return;
    this.aiTimer = AI_THINK;
  },

  runAI: function () {
    var g = this.game;
    if (!g || this.phase !== 'play') return;

    if (this.pendingAI) {
      var held = this.pendingAI;
      this.pendingAI = null;
      var owner = g.players[g.turn];
      var heldColor = (held.kind === 'wild' || held.kind === 'wd4') ? pickColor(owner) : null;
      this.playCard(held, heldColor);
      return;
    }
    if (this.pendingPass) {
      this.pendingPass = false;
      this.advanceTurn(1);
      if (g.turn === 0) this.say(this.msg + ' · 轮到你出牌');
      else this.say(this.msg);
      this.clampSel();
      this.scheduleAI();
      return;
    }

    var p = g.players[g.turn];
    if (!p.ai) return;
    var top = g.discard[g.discard.length - 1];
    var playable = [];
    var i;
    for (i = 0; i < p.hand.length; i++) {
      if (canPlay(p.hand[i], top, g.color)) playable.push(p.hand[i]);
    }
    if (!playable.length) {
      var card = drawOne(g);
      p.hand.push(card);
      this.say(p.name + ' 摸了一张牌');
      if (canPlay(card, top, g.color)) {
        this.pendingAI = card;
        this.aiTimer = AI_ACT;
      } else {
        this.pendingPass = true;
        this.aiTimer = AI_ACT;
      }
      return;
    }
    var pick = aiPick(playable);
    var chosen = (pick.kind === 'wild' || pick.kind === 'wd4') ? pickColor(p) : null;
    this.playCard(pick, chosen);
  },

  // ---------------- 输入 ----------------

  handlePad: function (dt) {
    var pad = wx.getGamepadState ? wx.getGamepadState() : null;
    var buttons = (pad && pad.buttons) || {};
    var axes = (pad && pad.axes) || [];
    var lx = axes.length > 0 ? axes[0] : 0;

    var now = {
      a: !!buttons.a,
      x: !!buttons.x,
      y: !!buttons.y,
      start: !!buttons.start,
      left: !!buttons.left || lx < -0.55,
      right: !!buttons.right || lx > 0.55
    };
    var prev = this.padPrev;

    if (this.phase === 'menu') {
      if (now.a && !prev.a) this.confirmMenu();
    } else if (this.phase === 'color') {
      if (now.a && !prev.a) this.confirmColor();
    } else if (this.phase === 'over') {
      if ((now.a && !prev.a) || (now.y && !prev.y)) this.restart();
      if (now.x && !prev.x) this.toMenu();
    } else if (this.phase === 'play') {
      if (now.a && !prev.a) this.tryPlaySel();
      if (now.x && !prev.x) this.onDraw();
      if (now.y && !prev.y) this.onPass();
    }

    var dx = (now.right ? 1 : 0) - (now.left ? 1 : 0);
    var key = String(dx);
    if (dx === 0) {
      this.repeatKey = '';
      this.repeatTimer = 0;
    } else if (key !== this.repeatKey) {
      this.repeatKey = key;
      this.repeatTimer = REPEAT;
      this.onDir(dx);
    } else {
      this.repeatTimer -= dt;
      if (this.repeatTimer <= 0) {
        this.repeatTimer += REPEAT;
        this.onDir(dx);
      }
    }

    this.padPrev = now;
  },

  onDir: function (dx) {
    if (this.phase === 'menu') {
      this.menuIndex = clamp(this.menuIndex + dx, 0, 2);
    } else if (this.phase === 'color') {
      this.colorSel = (this.colorSel + dx + 4) % 4;
    } else if (this.phase === 'play') {
      this.moveSel(dx);
    }
  },

  confirmMenu: function () {
    this.startGame(this.menuIndex + 2);
  },

  onTouchStart: function (e) {
    var d = e && e.detail ? e.detail : {};
    var x = d.x;
    var y = d.y;
    if (x === undefined || y === undefined) return;
    var i;

    if (this.phase === 'menu') {
      var mb = this.menuRects();
      for (i = 0; i < mb.length; i++) {
        if (inRect(x, y, mb[i])) {
          this.menuIndex = i;
          this.startGame(i + 2);
          return;
        }
      }
      return;
    }

    if (this.phase === 'color') {
      var cb = this.colorRects();
      for (i = 0; i < cb.length; i++) {
        if (inRect(x, y, cb[i])) {
          this.colorSel = i;
          this.confirmColor();
          return;
        }
      }
      return;
    }

    if (this.phase === 'over') {
      var ob = this.overRects();
      if (inRect(x, y, ob[0])) {
        this.restart();
        return;
      }
      if (inRect(x, y, ob[1])) {
        this.toMenu();
        return;
      }
      return;
    }

    if (this.phase !== 'play' || !this.game) return;

    if (inRect(x, y, { x: 742, y: 236, w: 142, h: 210 })) {
      this.onDraw();
      return;
    }
    if (this.canPass() && inRect(x, y, { x: 40, y: 462, w: 170, h: 52 })) {
      this.onPass();
      return;
    }

    var hand = this.game.players[0].hand;
    var lay = this.handLayout();
    for (i = hand.length - 1; i >= 0; i--) {
      var hx = lay.startX + i * lay.step;
      if (x >= hx && x <= hx + lay.cw && y >= lay.y - 16 && y <= lay.y + lay.ch) {
        this.sel = i;
        this.tryPlaySel();
        return;
      }
    }
  },

  onTouchMove: function (e) {
    if (this.phase !== 'play' || !this.game || this.game.turn !== 0) return;
    var d = e && e.detail ? e.detail : {};
    var x = d.x;
    var y = d.y;
    if (x === undefined || y === undefined) return;
    var hand = this.game.players[0].hand;
    var lay = this.handLayout();
    for (var i = hand.length - 1; i >= 0; i--) {
      var hx = lay.startX + i * lay.step;
      if (x >= hx && x <= hx + lay.cw && y >= lay.y - 16 && y <= lay.y + lay.ch) {
        this.sel = i;
        return;
      }
    }
  },

  onTouchEnd: function () {
    // 出牌在按下时处理，抬手无需额外操作
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
    if (!this.game || this.phase !== 'play') return;
    var g = this.game;

    if (this.passTimer > 0 && g.turn === 0) {
      this.passTimer -= dt;
      if (this.passTimer <= 0) {
        this.passTimer = 0;
        if (g.drawn) this.passTurn();
      }
    }

    if (this.aiTimer > 0) {
      this.aiTimer -= dt;
      if (this.aiTimer <= 0) {
        this.aiTimer = 0;
        this.runAI();
      }
    }
  },

  // ---------------- 布局 ----------------

  menuRects: function () {
    var w = 184;
    var h = 88;
    var gap = 34;
    var total = 3 * w + 2 * gap;
    var startX = (W - total) / 2;
    var list = [];
    for (var i = 0; i < 3; i++) list.push({ x: startX + i * (w + gap), y: 352, w: w, h: h });
    return list;
  },

  colorRects: function () {
    var w = 112;
    var h = 112;
    var gap = 28;
    var total = 4 * w + 3 * gap;
    var startX = (W - total) / 2;
    var list = [];
    for (var i = 0; i < 4; i++) list.push({ x: startX + i * (w + gap), y: 312, w: w, h: h });
    return list;
  },

  overRects: function () {
    var w = 250;
    var h = 80;
    var gap = 40;
    var total = 2 * w + gap;
    var startX = (W - total) / 2;
    return [
      { x: startX, y: 380, w: w, h: h },
      { x: startX + w + gap, y: 380, w: w, h: h }
    ];
  },

  handLayout: function () {
    var n = this.game ? this.game.players[0].hand.length : 0;
    var cw = 94;
    var ch = 136;
    var y = 548;
    var avail = 1180;
    var step = 0;
    if (n > 1) step = (avail - cw) / (n - 1);
    if (step > cw + 8) step = cw + 8;
    if (step < 34) step = 34;
    var total = (n - 1) * step + cw;
    var startX = (W - total) / 2;
    if (startX < 30) startX = 30;
    return { startX: startX, step: step, cw: cw, ch: ch, y: y, n: n };
  },

  // ---------------- 绘制基础 ----------------

  drawCardBack: function (ctx, x, y, w, h) {
    ctx.setFillStyle('rgba(0,0,0,0.32)');
    ctx.fillRect(x + 3, y + 4, w, h);
    ctx.setFillStyle('#8a1e2c');
    ctx.fillRect(x, y, w, h);
    ctx.setFillStyle('rgba(255,255,255,0.12)');
    ctx.fillRect(x + 4, y + 4, w - 8, h - 8);
    ctx.setStrokeStyle('#f3e6e6');
    ctx.setLineWidth(2);
    ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);
  },

  labelSize: function (w, label) {
    if (label.length >= 2) return Math.round(w * 0.38);
    return Math.round(w * 0.52);
  },

  drawCard: function (ctx, x, y, w, h, card, opt) {
    opt = opt || {};
    if (opt.dim) ctx.setGlobalAlpha(0.72);

    ctx.setFillStyle('rgba(0,0,0,0.34)');
    ctx.fillRect(x + 4, y + 5, w, h);

    if (card.color === 'w') {
      ctx.setFillStyle('#20242c');
      ctx.fillRect(x, y, w, h);
      var pad = Math.round(w * 0.09);
      var iw = Math.round((w - pad * 3) / 2);
      var ih = Math.round((h - pad * 3) / 2);
      ctx.setFillStyle('#e11d48');
      ctx.fillRect(x + pad, y + pad, iw, ih);
      ctx.setFillStyle('#facc15');
      ctx.fillRect(x + pad * 2 + iw, y + pad, iw, ih);
      ctx.setFillStyle('#2563eb');
      ctx.fillRect(x + pad, y + pad * 2 + ih, iw, ih);
      ctx.setFillStyle('#16a34a');
      ctx.fillRect(x + pad * 2 + iw, y + pad * 2 + ih, iw, ih);
      ctx.setFillStyle('rgba(8,12,16,0.62)');
      ctx.fillRect(x + pad, y + Math.round(h * 0.38), w - pad * 2, Math.round(h * 0.24));
    } else {
      ctx.setFillStyle(COLOR_HEX[card.color] || '#1f2937');
      ctx.fillRect(x, y, w, h);
    }

    ctx.setStrokeStyle('rgba(255,255,255,0.22)');
    ctx.setLineWidth(2);
    ctx.strokeRect(x + 5, y + 5, w - 10, h - 10);

    var fs = this.labelSize(w, card.label);
    ctx.setTextAlign('center');
    ctx.setFontSize(fs);
    ctx.setFillStyle(card.color === 'y' ? '#1a1d24' : '#ffffff');
    ctx.fillText(card.label, x + w / 2, y + h / 2 + Math.round(fs * 0.36));
    ctx.setTextAlign('left');

    if (opt.playable) {
      ctx.setStrokeStyle('#ffffff');
      ctx.setLineWidth(4);
      ctx.strokeRect(x - 2, y - 2, w + 4, h + 4);
    } else {
      ctx.setStrokeStyle('rgba(255,255,255,0.32)');
      ctx.setLineWidth(2);
      ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);
    }

    ctx.setGlobalAlpha(1);
  },

  chipBg: function (ctx, x, y, w, h) {
    ctx.setFillStyle('#0b3a2a');
    ctx.fillRect(x, y, w, h);
    ctx.setStrokeStyle('#1f7455');
    ctx.setLineWidth(2);
    ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;
    ctx.setGlobalAlpha(1);
    ctx.setTextAlign('left');

    this.drawTable(ctx);
    this.drawHeader(ctx);
    if (this.game && this.phase !== 'menu') {
      this.drawOpponents(ctx);
      this.drawCenter(ctx);
      this.drawMessage(ctx);
      this.drawHand(ctx);
    }
    this.drawFooter(ctx);

    if (this.phase === 'menu') this.drawMenu(ctx);
    else if (this.phase === 'color') this.drawColorPicker(ctx);
    else if (this.phase === 'over') this.drawOver(ctx);

    ctx.draw();
  },

  drawTable: function (ctx) {
    ctx.setFillStyle('#062018');
    ctx.fillRect(0, 0, W, H);

    ctx.setFillStyle('#0d4b34');
    ctx.fillRect(24, 84, 1232, 592);
    ctx.setStrokeStyle('#1a7a54');
    ctx.setLineWidth(3);
    ctx.strokeRect(24, 84, 1232, 592);

    ctx.setStrokeStyle('rgba(255,255,255,0.05)');
    ctx.setLineWidth(2);
    ctx.strokeRect(38, 98, 1204, 564);

    ctx.setStrokeStyle('rgba(255,255,255,0.045)');
    ctx.beginPath();
    ctx.arc(640, 352, 268, 0, Math.PI * 2);
    ctx.stroke();
  },

  drawHeader: function (ctx) {
    var g = this.game;

    ctx.setFillStyle('#07271c');
    ctx.fillRect(0, 0, W, 76);
    ctx.setFillStyle('#1b6b4c');
    ctx.fillRect(0, 74, W, 2);

    ctx.setTextAlign('left');
    ctx.setFontSize(46);
    ctx.setFillStyle('#b3173a');
    ctx.fillText('UNO', 42, 54);
    ctx.setFillStyle('#ffd23f');
    ctx.fillText('UNO', 39, 51);

    var sub = '经典 108 张 · 单人对抗电脑';
    var subCol = '#8fc7ab';
    if (g && this.phase === 'over') {
      sub = '牌局结束';
    } else if (g && this.phase === 'color') {
      sub = '选择万能牌颜色';
    } else if (g) {
      if (g.turn === 0) {
        sub = '轮到你出牌';
        subCol = '#c8f5dc';
      } else {
        var dots = '...'.substring(0, 1 + Math.floor(this.time * 3) % 3);
        sub = g.players[g.turn].name + ' 思考中' + dots;
      }
    }
    ctx.setFillStyle(subCol);
    ctx.setFontSize(20);
    ctx.fillText(sub, 142, 53);

    var cy = 16;
    var chh = 44;

    var x1 = 704;
    var w1 = 176;
    this.chipBg(ctx, x1, cy, w1, chh);
    ctx.setFillStyle(g ? COLOR_HEX[g.color] : '#2a4b3d');
    ctx.beginPath();
    ctx.arc(x1 + 24, cy + 22, 11, 0, Math.PI * 2);
    ctx.fill();
    if (g) {
      ctx.setStrokeStyle('#ffffff');
      ctx.setLineWidth(2);
      ctx.beginPath();
      ctx.arc(x1 + 24, cy + 22, 11, 0, Math.PI * 2);
      ctx.stroke();
    }
    ctx.setFillStyle('#dff3e8');
    ctx.setFontSize(22);
    ctx.fillText('当前 · ' + (g ? COLOR_NAME[g.color] : '--'), x1 + 44, cy + 30);

    var x2 = 892;
    var w2 = 160;
    this.chipBg(ctx, x2, cy, w2, chh);
    ctx.setFillStyle('#dff3e8');
    ctx.setFontSize(20);
    ctx.fillText('方向 ' + (g ? (g.dir === 1 ? '顺时针' : '逆时针') : '--'), x2 + 16, cy + 29);

    var x3 = 1064;
    var w3 = 176;
    this.chipBg(ctx, x3, cy, w3, chh);
    ctx.setFillStyle('#dff3e8');
    ctx.setFontSize(22);
    ctx.fillText('牌堆 ' + (g ? g.deck.length : 0) + ' 张', x3 + 18, cy + 30);
  },

  drawOpponents: function (ctx) {
    var g = this.game;
    var n = g.players.length - 1;
    if (n <= 0) return;
    var w = 300;
    var h = 92;
    var gap = 24;
    var total = n * w + (n - 1) * gap;
    var startX = (W - total) / 2;

    for (var i = 1; i < g.players.length; i++) {
      var p = g.players[i];
      var x = startX + (i - 1) * (w + gap);
      var y = 86;
      var active = this.phase === 'play' && g.turn === i;

      ctx.setFillStyle(active ? '#0d5340' : '#0a3b2b');
      ctx.fillRect(x, y, w, h);
      ctx.setStrokeStyle(active ? '#38bdf8' : '#1b6a4c');
      ctx.setLineWidth(active ? 3 : 2);
      ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);

      ctx.setTextAlign('left');
      ctx.setFillStyle(active ? '#ffffff' : '#cfe8d8');
      ctx.setFontSize(26);
      ctx.fillText(p.name, x + 20, y + 38);

      ctx.setTextAlign('right');
      ctx.setFillStyle('#ffd166');
      ctx.setFontSize(30);
      ctx.fillText(p.hand.length + ' 张', x + w - 20, y + 40);

      var shown = Math.min(p.hand.length, 8);
      for (var k = 0; k < shown; k++) {
        this.drawCardBack(ctx, x + 20 + k * 12, y + 52, 22, 32);
      }

      if (active) {
        ctx.setTextAlign('left');
        ctx.setFillStyle('#7fd6ae');
        ctx.setFontSize(17);
        var dots = '...'.substring(0, 1 + Math.floor(this.time * 3) % 3);
        ctx.fillText('出牌中' + dots, x + w - 100, y + 82);
      }
      ctx.setTextAlign('left');
    }
  },

  drawCenter: function (ctx) {
    var g = this.game;

    // 摸牌堆
    var dx = 742;
    var dy = 236;
    var dw = 142;
    var dh = 210;
    if (g.deck.length > 0) {
      for (var k = 3; k >= 1; k--) {
        this.drawCardBack(ctx, dx + k * 4, dy - k * 3, dw, dh);
      }
      this.drawCardBack(ctx, dx, dy, dw, dh);
      ctx.setTextAlign('center');
      ctx.setFillStyle('#ffe6b0');
      ctx.setFontSize(46);
      ctx.fillText(String(g.deck.length), dx + dw / 2, dy + dh / 2 + 16);
      ctx.setFillStyle('#f0c2c8');
      ctx.setFontSize(19);
      ctx.fillText('摸牌堆', dx + dw / 2, dy + dh - 24);
      ctx.setTextAlign('left');
    } else {
      ctx.setFillStyle('rgba(0,0,0,0.22)');
      ctx.fillRect(dx, dy, dw, dh);
      ctx.setStrokeStyle('#2f8a66');
      ctx.setLineWidth(3);
      ctx.strokeRect(dx + 1, dy + 1, dw - 2, dh - 2);
      ctx.setTextAlign('center');
      ctx.setFillStyle('#7fb99e');
      ctx.setFontSize(30);
      ctx.fillText('空', dx + dw / 2, dy + dh / 2 + 10);
      ctx.setTextAlign('left');
    }

    // 弃牌堆
    var top = g.discard[g.discard.length - 1];
    var cx = 400;
    var cy2 = 236;
    var cw = 142;
    var ch2 = 210;

    ctx.setStrokeStyle(COLOR_HEX[g.color]);
    ctx.setLineWidth(6);
    ctx.strokeRect(cx - 9, cy2 - 9, cw + 18, ch2 + 18);

    var t = this.time - this.throwAt;
    if (t >= 0 && t < 0.18) {
      var sc = 0.88 + 0.12 * (t / 0.18);
      ctx.save();
      ctx.translate(cx + cw / 2, cy2 + ch2 / 2);
      ctx.scale(sc, sc);
      ctx.translate(-(cx + cw / 2), -(cy2 + ch2 / 2));
      this.drawCard(ctx, cx, cy2, cw, ch2, top, {});
      ctx.restore();
    } else {
      this.drawCard(ctx, cx, cy2, cw, ch2, top, {});
    }

    // 当前颜色
    ctx.setTextAlign('center');
    ctx.setFillStyle('#9fd9bb');
    ctx.setFontSize(19);
    ctx.fillText('当前颜色', 642, 258);

    ctx.setFillStyle(COLOR_HEX[g.color]);
    ctx.beginPath();
    ctx.arc(642, 300, 34, 0, Math.PI * 2);
    ctx.fill();
    ctx.setStrokeStyle('#ffffff');
    ctx.setLineWidth(3);
    ctx.beginPath();
    ctx.arc(642, 300, 34, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setFillStyle('#ffffff');
    ctx.setFontSize(28);
    ctx.fillText(COLOR_NAME[g.color], 642, 311);

    // 方向
    ctx.setStrokeStyle('#7fd6ae');
    ctx.setLineWidth(5);
    ctx.beginPath();
    ctx.moveTo(612, 392);
    ctx.lineTo(668, 392);
    ctx.stroke();

    ctx.setFillStyle('#7fd6ae');
    ctx.beginPath();
    if (g.dir === 1) {
      ctx.moveTo(682, 392);
      ctx.lineTo(662, 380);
      ctx.lineTo(662, 404);
    } else {
      ctx.moveTo(602, 392);
      ctx.lineTo(622, 380);
      ctx.lineTo(622, 404);
    }
    ctx.fill();

    ctx.setFillStyle('#bfe9d5');
    ctx.setFontSize(19);
    ctx.fillText('出牌方向', 642, 424);
    ctx.setFillStyle('#ffffff');
    ctx.setFontSize(22);
    ctx.fillText(g.dir === 1 ? '顺时针' : '逆时针', 642, 452);
    ctx.setTextAlign('left');
  },

  drawMessage: function (ctx) {
    ctx.setFillStyle('rgba(3,18,12,0.30)');
    ctx.fillRect(0, 458, W, 62);

    var hot = this.time - this.msgAt < 1.2;
    ctx.setTextAlign('center');
    ctx.setFontSize(26);
    ctx.setFillStyle(hot ? '#ffe08a' : '#d6ecdf');
    ctx.fillText(this.msg, 640, 498);

    if (this.canPass()) {
      var bx = 40;
      var by = 462;
      var bw = 170;
      var bh = 52;
      ctx.setFillStyle('#0b4a34');
      ctx.fillRect(bx, by, bw, bh);
      ctx.setStrokeStyle('#4fd6a0');
      ctx.setLineWidth(2);
      ctx.strokeRect(bx + 1, by + 1, bw - 2, bh - 2);
      ctx.setFillStyle('#d8ffe9');
      ctx.setFontSize(24);
      ctx.fillText('Y 过牌', bx + bw / 2, by + 34);
    }
    ctx.setTextAlign('left');
  },

  drawHand: function (ctx) {
    var g = this.game;
    if (!g) return;
    var hand = g.players[0].hand;
    if (!hand.length) return;

    var lay = this.handLayout();
    var top = g.discard[g.discard.length - 1];
    var mine = this.phase === 'play' && g.turn === 0;
    var i;

    for (i = 0; i < hand.length; i++) {
      var x = lay.startX + i * lay.step;
      var y = lay.y;
      if (i === this.sel && mine) y -= 10;
      var playable = mine && canPlay(hand[i], top, g.color);
      this.drawCard(ctx, x, y, lay.cw, lay.ch, hand[i], { playable: playable, dim: !mine });
    }

    if (mine && hand.length > 0) {
      var sx = lay.startX + this.sel * lay.step;
      var sy = lay.y - 10;
      ctx.setGlobalAlpha(0.65 + 0.35 * Math.abs(Math.sin(this.time * 3)));
      ctx.setStrokeStyle('#ffd23f');
      ctx.setLineWidth(5);
      ctx.strokeRect(sx - 6, sy - 6, lay.cw + 12, lay.ch + 12);

      ctx.setFillStyle('#ffd23f');
      ctx.beginPath();
      ctx.moveTo(sx + lay.cw / 2 - 11, sy - 16);
      ctx.lineTo(sx + lay.cw / 2 + 11, sy - 16);
      ctx.lineTo(sx + lay.cw / 2, sy - 3);
      ctx.fill();
      ctx.setGlobalAlpha(1);
    }
    ctx.setTextAlign('left');
  },

  drawFooter: function (ctx) {
    ctx.setFillStyle('#07241a');
    ctx.fillRect(0, 688, W, 32);
    ctx.setFillStyle('#1b6b4c');
    ctx.fillRect(0, 688, W, 1);

    var text;
    if (this.phase === 'menu') {
      text = '← → 选择人数 · A 开始 · 触摸按钮直接开始';
    } else if (this.phase === 'over') {
      text = 'A / Y 再来一局 · X 换人数 · 触摸按钮也可以';
    } else if (this.phase === 'color') {
      text = '← → 选择颜色 · A 确认 · 触摸色块直接选';
    } else {
      text = '← → 选牌 · A 出牌 · X 摸牌 · Y 过牌 · 触摸手牌或摸牌堆';
    }
    ctx.setTextAlign('center');
    ctx.setFillStyle('#8fc7ab');
    ctx.setFontSize(18);
    ctx.fillText(text, 640, 712);
    ctx.setTextAlign('left');
  },

  drawMenu: function (ctx) {
    ctx.setFillStyle('rgba(3,16,11,0.90)');
    ctx.fillRect(0, 0, W, H);

    var px = 260;
    var py = 140;
    var pw = 760;
    var ph = 440;
    ctx.setFillStyle('#0c2f23');
    ctx.fillRect(px, py, pw, ph);
    ctx.setStrokeStyle('#2b7a5c');
    ctx.setLineWidth(3);
    ctx.strokeRect(px + 1.5, py + 1.5, pw - 3, ph - 3);

    ctx.setTextAlign('center');
    ctx.setFontSize(72);
    ctx.setFillStyle('#9c1435');
    ctx.fillText('UNO', 643, 251);
    ctx.setFillStyle('#ffd23f');
    ctx.fillText('UNO', 640, 248);

    ctx.setFillStyle('#a7cdb9');
    ctx.setFontSize(24);
    ctx.fillText('你和电脑对战，先出完手牌的人获胜', 640, 294);

    ctx.setFillStyle('#8fc7ab');
    ctx.setFontSize(20);
    ctx.fillText('可出的手牌带白色描边 · 选中后按 A 打出', 640, 332);

    var rects = this.menuRects();
    for (var i = 0; i < rects.length; i++) {
      var r = rects[i];
      var on = i === this.menuIndex;
      ctx.setFillStyle(on ? '#1f7a56' : '#12402f');
      ctx.fillRect(r.x, r.y, r.w, r.h);
      ctx.setStrokeStyle(on ? '#ffd23f' : '#2f8a66');
      ctx.setLineWidth(on ? 4 : 2);
      ctx.strokeRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2);
      ctx.setFillStyle(on ? '#ffffff' : '#cfe8d8');
      ctx.setFontSize(32);
      ctx.fillText((i + 2) + ' 人', r.x + r.w / 2, r.y + 56);
    }

    ctx.setFillStyle('#79b79c');
    ctx.setFontSize(21);
    ctx.fillText('← → 选择人数 · A 开始牌局 · 触摸按钮直接开始', 640, 532);
    ctx.setTextAlign('left');
  },

  drawColorPicker: function (ctx) {
    ctx.setFillStyle('rgba(3,16,11,0.72)');
    ctx.fillRect(0, 0, W, H);

    var px = 320;
    var py = 180;
    var pw = 640;
    var ph = 330;
    ctx.setFillStyle('#0c2f23');
    ctx.fillRect(px, py, pw, ph);
    ctx.setStrokeStyle('#2b7a5c');
    ctx.setLineWidth(3);
    ctx.strokeRect(px + 1.5, py + 1.5, pw - 3, ph - 3);

    ctx.setTextAlign('center');
    ctx.setFillStyle('#f4f8ff');
    ctx.setFontSize(44);
    ctx.fillText('选择颜色', 640, 252);
    ctx.setFillStyle('#8fc7ab');
    ctx.setFontSize(20);
    ctx.fillText('万能牌打出后，由你决定接下来的颜色', 640, 292);

    var rects = this.colorRects();
    for (var i = 0; i < rects.length; i++) {
      var r = rects[i];
      var c = COLORS[i];
      var on = i === this.colorSel;
      ctx.setFillStyle(COLOR_HEX[c]);
      ctx.fillRect(r.x, r.y, r.w, r.h);
      ctx.setStrokeStyle(on ? '#ffd23f' : 'rgba(255,255,255,0.45)');
      ctx.setLineWidth(on ? 6 : 3);
      ctx.strokeRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
      ctx.setFillStyle(c === 'y' ? '#1a1d24' : '#ffffff');
      ctx.setFontSize(30);
      ctx.fillText(COLOR_NAME[c], r.x + r.w / 2, r.y + r.h / 2 + 11);
    }

    ctx.setFillStyle('#79b79c');
    ctx.setFontSize(20);
    ctx.fillText('← → 选择 · A 确认 · 触摸色块直接选', 640, 482);
    ctx.setTextAlign('left');
  },

  drawOver: function (ctx) {
    var g = this.game;
    ctx.setFillStyle('rgba(3,14,10,0.84)');
    ctx.fillRect(0, 0, W, H);

    var px = 280;
    var py = 160;
    var pw = 720;
    var ph = 380;
    ctx.setFillStyle('#0c2f23');
    ctx.fillRect(px, py, pw, ph);
    ctx.setStrokeStyle('#2b7a5c');
    ctx.setLineWidth(3);
    ctx.strokeRect(px + 1.5, py + 1.5, pw - 3, ph - 3);

    var win = this.winner === 0;
    var title = win ? '你赢了！' : (g && this.winner >= 0 ? g.players[this.winner].name + ' 赢了' : '牌局结束');
    ctx.setTextAlign('center');
    ctx.setFillStyle(win ? '#ffd23f' : '#ff8fa3');
    ctx.setFontSize(58);
    ctx.fillText(title, 640, 262);

    ctx.setFillStyle('#a7cdb9');
    ctx.setFontSize(22);
    ctx.fillText('先出完手牌的人获胜', 640, 304);

    var line = '';
    if (g) {
      for (var i = 0; i < g.players.length; i++) {
        line += g.players[i].name + ' ' + g.players[i].hand.length + ' 张';
        if (i < g.players.length - 1) line += '   ';
      }
    }
    ctx.setFillStyle('#7fb99e');
    ctx.setFontSize(20);
    ctx.fillText(line, 640, 342);

    var rects = this.overRects();
    var labels = ['再来一局  A', '换人数  X'];
    for (var k = 0; k < rects.length; k++) {
      var r = rects[k];
      ctx.setFillStyle(k === 0 ? '#1f7a56' : '#12402f');
      ctx.fillRect(r.x, r.y, r.w, r.h);
      ctx.setStrokeStyle(k === 0 ? '#ffd23f' : '#2f8a66');
      ctx.setLineWidth(k === 0 ? 4 : 2);
      ctx.strokeRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2);
      ctx.setFillStyle('#f4f8ff');
      ctx.setFontSize(28);
      ctx.fillText(labels[k], r.x + r.w / 2, r.y + 51);
    }

    ctx.setFillStyle('#79b79c');
    ctx.setFontSize(20);
    ctx.fillText('按 A / Y 直接再来一局', 640, 512);
    ctx.setTextAlign('left');
  }
});
