// Neon Runner：一键跳跃的横版跑酷
// 操作：A / B / 上 / 左摇杆上推 / 点击屏幕，二段跳；撞到障碍结束。

var W = 600;
var H = 330;
var GROUND = H - 52;
var GRAVITY = 2150;
var JUMP_V = -745;
var DOUBLE_V = -640;
var PW = 28;
var PH = 40;

function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
function rand(a, b) { return a + Math.random() * (b - a); }

Page({
  data: {
    score: '0',
    best: '0'
  },

  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.best = Number(wx.getStorageSync('runner_best') || 0) || 0;
    this.setData({ best: String(this.best) });

    this.sfx = {};
    var files = { jump: '/assets/jump.wav', coin: '/assets/coin.wav', hit: '/assets/hit.wav' };
    for (var name in files) {
      var audio = wx.createInnerAudioContext();
      audio.src = files[name];
      audio.volume = 0.7;
      this.sfx[name] = audio;
    }

    this.bgm = wx.createInnerAudioContext();
    this.bgm.src = '/assets/bgm.wav';
    this.bgm.loop = true;
    this.bgm.volume = 0.32;

    this.prevPad = { jump: false };
    this.lastHud = 0;
    this.lastTs = 0;
    this.reset();

    var loop = function (ts) {
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  onUnload: function () {
    for (var name in this.sfx) this.sfx[name].destroy();
    if (this.bgm) this.bgm.destroy();
  },

  // ---------------- 状态 ----------------

  reset: function () {
    this.game = {
      state: 'ready',
      time: 0,
      speed: 320,
      distance: 0,
      score: 0,
      coins: 0,
      spawnIn: 1.1,
      shake: 0,
      deadAt: 0,
      player: { x: 92, y: GROUND - PH, vy: 0, onGround: true, jumps: 0, squash: 1, trail: 0 },
      obstacles: [],
      coinsList: [],
      particles: [],
      stars: []
    };
    for (var i = 0; i < 44; i++) {
      this.game.stars.push({ x: rand(0, W), y: rand(10, GROUND - 90), r: rand(0.8, 1.8) });
    }
    this.setData({ score: '0' });
    if (this.bgm && !this.bgmPlaying) {
      this.bgm.play();
      this.bgmPlaying = true;
    }
  },

  startRun: function () {
    this.game.state = 'run';
    this.game.time = 0;
  },

  playSfx: function (name) {
    var audio = this.sfx[name];
    if (!audio) return;
    audio.seek(0);
    audio.play();
  },

  // ---------------- 输入 ----------------

  jump: function () {
    var g = this.game;
    if (g.state === 'ready') {
      this.startRun();
      return;
    }
    if (g.state === 'dead') {
      if (g.time - g.deadAt > 0.4) this.reset();
      return;
    }
    var p = g.player;
    if (p.onGround) {
      p.vy = JUMP_V;
      p.onGround = false;
      p.jumps = 1;
      p.squash = 0.78;
      this.playSfx('jump');
      wx.vibrateShort({ type: 'light' });
    } else if (p.jumps < 2) {
      p.vy = DOUBLE_V;
      p.jumps = 2;
      p.squash = 1.15;
      this.playSfx('jump');
      this.burst(p.x + PW / 2, p.y + PH, '#7ad7ff', 8);
      wx.vibrateShort({ type: 'light' });
    }
  },

  onTouch: function () {
    this.jump();
  },

  readPad: function () {
    var pad = wx.getGamepadState();
    var buttons = pad.buttons || {};
    var axes = pad.axes || [0, 0];
    var pressed = !!(buttons.a || buttons.b || buttons.up || buttons.lb ||
                     (axes[1] !== undefined && axes[1] < -0.6));
    var edge = pressed && !this.prevPad.jump;
    this.prevPad.jump = pressed;
    return edge;
  },

  // ---------------- 循环 ----------------

  tick: function (ts) {
    var dt = this.lastTs ? (ts - this.lastTs) / 1000 : 0.016;
    this.lastTs = ts;
    if (dt > 0.05) dt = 0.05;
    if (dt <= 0) dt = 0.016;

    if (this.readPad()) this.jump();

    this.update(dt);
    this.draw();
  },

  update: function (dt) {
    var g = this.game;
    g.time += dt;
    if (g.shake > 0) g.shake = Math.max(0, g.shake - dt * 4);

    var p = g.player;
    if (g.state === 'run') {
      g.speed = 320 + Math.min(520, g.distance * 0.045);
      g.distance += g.speed * dt;
      g.score = Math.floor(g.distance / 10) + g.coins * 10;

      // 生成障碍与金币
      g.spawnIn -= dt;
      if (g.spawnIn <= 0) {
        this.spawn();
        g.spawnIn = rand(0.9, 1.6) * (330 / g.speed);
      }
    }

    // 玩家物理
    if (!p.onGround) {
      p.vy += GRAVITY * dt;
      p.y += p.vy * dt;
      if (p.y >= GROUND - PH) {
        p.y = GROUND - PH;
        if (p.vy > 400) {
          p.squash = 0.8;
          this.burst(p.x + PW / 2, GROUND, '#3a4b60', 4);
        }
        p.vy = 0;
        p.onGround = true;
        p.jumps = 0;
      }
    }
    p.squash += (1 - p.squash) * Math.min(1, dt * 10);

    // 移动障碍
    for (var i = g.obstacles.length - 1; i >= 0; i--) {
      var o = g.obstacles[i];
      o.x -= g.speed * dt;
      if (o.x + o.w < -20) {
        g.obstacles.splice(i, 1);
        continue;
      }
      if (g.state === 'run' && this.hit(p, o)) this.die();
    }

    // 金币
    for (var j = g.coinsList.length - 1; j >= 0; j--) {
      var c = g.coinsList[j];
      c.x -= g.speed * dt;
      c.spin += dt * 6;
      if (c.x + 12 < -20) {
        g.coinsList.splice(j, 1);
        continue;
      }
      if (g.state === 'run' && Math.abs(c.x - (p.x + PW / 2)) < 26 &&
          Math.abs(c.y - (p.y + PH / 2)) < 30) {
        g.coinsList.splice(j, 1);
        g.coins += 1;
        this.playSfx('coin');
        this.burst(c.x, c.y, '#ffd166', 10);
      }
    }

    // 粒子
    for (var k = g.particles.length - 1; k >= 0; k--) {
      var pt = g.particles[k];
      pt.life -= dt;
      if (pt.life <= 0) {
        g.particles.splice(k, 1);
        continue;
      }
      pt.x += pt.vx * dt;
      pt.y += pt.vy * dt;
      pt.vy += 900 * dt;
    }

    // HUD 节流
    if (g.time - this.lastHud > 0.12) {
      this.lastHud = g.time;
      if (String(g.score) !== this.data.score) this.setData({ score: String(g.score) });
    }
  },

  spawn: function () {
    var g = this.game;
    var kind = Math.random() < 0.45 ? 'spike' : 'block';
    var w = kind === 'spike' ? 26 : rand(24, 42);
    var h = kind === 'spike' ? 30 : rand(28, 62);
    g.obstacles.push({ x: W + 30, w: w, h: h, kind: kind });

    if (Math.random() < 0.7) {
      var baseY = GROUND - rand(70, 130);
      var count = Math.random() < 0.4 ? 3 : 1;
      for (var i = 0; i < count; i++) {
        g.coinsList.push({
          x: W + 30 + w + 40 + i * 30,
          y: baseY - (count > 1 ? Math.sin(i / (count - 1) * Math.PI) * 26 : 0),
          spin: 0
        });
      }
    }
  },

  hit: function (p, o) {
    var pad = 5;
    return p.x + PW - pad > o.x && p.x + pad < o.x + o.w &&
           p.y + PH > GROUND - o.h;
  },

  die: function () {
    var g = this.game;
    g.state = 'dead';
    g.deadAt = g.time;
    g.shake = 1;
    this.playSfx('hit');
    wx.vibrateShort({ type: 'heavy' });
    this.burst(g.player.x + PW / 2, g.player.y + PH / 2, '#ff5f6d', 18);

    if (g.score > this.best) {
      this.best = g.score;
      wx.setStorageSync('runner_best', this.best);
      this.setData({ best: String(this.best) });
    }
  },

  burst: function (x, y, color, count) {
    var g = this.game;
    for (var i = 0; i < count; i++) {
      g.particles.push({
        x: x, y: y,
        vx: rand(-140, 140),
        vy: rand(-260, -40),
        life: rand(0.25, 0.6),
        color: color,
        size: rand(2, 4)
      });
    }
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;
    var g = this.game;
    var p = g.player;

    ctx.setFillStyle('#0b0e14');
    ctx.fillRect(0, 0, W, H);

    // 星空（两层视差）
    for (var i = 0; i < g.stars.length; i++) {
      var s = g.stars[i];
      var sx = (s.x - g.distance * 0.06) % W;
      if (sx < 0) sx += W;
      ctx.setFillStyle(i % 5 === 0 ? '#9fb3c8' : '#3a4b60');
      ctx.fillRect(sx, s.y, s.r, s.r);
    }

    // 远景山脊
    var offset = (g.distance * 0.18) % 160;
    ctx.setFillStyle('#111823');
    for (var m = -1; m < 5; m++) {
      var bx = m * 160 - offset;
      ctx.beginPath();
      ctx.moveTo(bx, GROUND);
      ctx.lineTo(bx + 80, GROUND - 78);
      ctx.lineTo(bx + 160, GROUND);
      ctx.lineTo(bx, GROUND);
      ctx.fill();
    }

    // 地面
    ctx.setFillStyle('#0f1620');
    ctx.fillRect(0, GROUND, W, H - GROUND);
    ctx.setFillStyle('#1f6feb');
    ctx.fillRect(0, GROUND, W, 2);
    ctx.setFillStyle('#16202c');
    var stripe = (g.distance * 1.0) % 48;
    for (var x = -48; x < W + 48; x += 48) {
      ctx.fillRect(x - stripe, GROUND + 10, 24, 3);
    }

    // 障碍
    for (var oi = 0; oi < g.obstacles.length; oi++) {
      var o = g.obstacles[oi];
      if (o.kind === 'spike') {
        ctx.setFillStyle('#ff5f6d');
        ctx.beginPath();
        ctx.moveTo(o.x, GROUND);
        ctx.lineTo(o.x + o.w / 2, GROUND - o.h);
        ctx.lineTo(o.x + o.w, GROUND);
        ctx.lineTo(o.x, GROUND);
        ctx.fill();
      } else {
        ctx.setFillStyle('#2b3b4e');
        ctx.fillRect(o.x, GROUND - o.h, o.w, o.h);
        ctx.setFillStyle('#3f5872');
        ctx.fillRect(o.x, GROUND - o.h, o.w, 4);
      }
    }

    // 金币
    for (var ci = 0; ci < g.coinsList.length; ci++) {
      var c = g.coinsList[ci];
      var wob = 1 + Math.sin(c.spin) * 0.18;
      ctx.setFillStyle('#ffd166');
      ctx.beginPath();
      ctx.arc(c.x, c.y, 8 * wob, 0, Math.PI * 2);
      ctx.fill();
      ctx.setFillStyle('#8a6a1f');
      ctx.beginPath();
      ctx.arc(c.x, c.y, 3, 0, Math.PI * 2);
      ctx.fill();
    }

    // 玩家
    var h = PH * p.squash;
    var w = PW / Math.max(0.72, p.squash);
    var px = p.x + (PW - w) / 2;
    var py = p.y + (PH - h);
    ctx.setFillStyle('#10dc78');
    ctx.fillRect(px, py, w, h);
    ctx.setFillStyle('#062b1a');
    ctx.fillRect(px + w - 10, py + 8, 4, 6);
    // 冲刺拖尾
    ctx.setFillStyle('rgba(16,220,120,0.28)');
    ctx.fillRect(px - 10, py + h * 0.35, 8, h * 0.4);

    // 粒子
    for (var pi = 0; pi < g.particles.length; pi++) {
      var pt = g.particles[pi];
      ctx.setFillStyle(pt.color);
      ctx.fillRect(pt.x, pt.y, pt.size, pt.size);
    }

    // HUD
    ctx.setFontSize(16);
    ctx.setFillStyle('#e8eef6');
    ctx.fillText('SCORE ' + g.score, 16, 26);
    ctx.setFillStyle('#5b6b7d');
    ctx.setFontSize(13);
    ctx.fillText('BEST ' + this.best, 16, 46);

    // 状态层
    if (g.state === 'ready') {
      this.drawCenter(ctx, 'NEON RUNNER', '按 A / 点击屏幕开始', '#10dc78');
    } else if (g.state === 'dead') {
      ctx.setFillStyle('rgba(6,8,12,0.62)');
      ctx.fillRect(0, 0, W, H);
      this.drawCenter(ctx, '撞车了', '得分 ' + g.score + ' · 按 A / 点击重开', '#ff5f6d');
    }

    ctx.draw();
  },

  drawCenter: function (ctx, title, hint, color) {
    ctx.setTextAlign('center');
    ctx.setFontSize(30);
    ctx.setFillStyle(color);
    ctx.fillText(title, W / 2, H / 2 - 12);
    ctx.setFontSize(14);
    ctx.setFillStyle('#9fb3c8');
    ctx.fillText(hint, W / 2, H / 2 + 20);
    ctx.setTextAlign('left');
  }
});
