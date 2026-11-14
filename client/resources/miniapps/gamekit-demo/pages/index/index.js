var W = 420;
var H = 300;
var SIZE = 34;

Page({
  data: {
    padText: '未检测',
    axesText: '0.00 / 0.00',
    touchText: '空闲',
    vibrateText: '未触发',
    audioText: '待播放',
    wsText: '未连接',
    musicLabel: '播放 BGM'
  },

  onLoad: function () {
    this.box = { x: (W - SIZE) / 2, y: (H - SIZE) / 2 };
    this.press = { active: false, x: 0, y: 0 };
    this.jump = 0;
    this.lastTs = 0;
    this.frames = 0;
    this.fps = 0;
    this.fpsClock = 0;
    this.padClock = 0;
    this.musicOn = false;

    this.ctx = wx.createCanvasContext('game');

    this.music = wx.createInnerAudioContext();
    this.music.src = '/assets/bgm.wav';
    this.music.loop = true;
    this.music.volume = 0.35;
    this.music.onError(function () {
      this.setData({ audioText: 'BGM 加载失败' });
    }.bind(this));

    this.beep = wx.createInnerAudioContext();
    this.beep.src = '/assets/beep.wav';
    this.beep.volume = 0.8;

    this.socket = null;

    var loop = function (ts) {
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  onUnload: function () {
    if (this.music) this.music.destroy();
    if (this.beep) this.beep.destroy();
    if (this.socket) this.socket.close();
  },

  tick: function (ts) {
    var dt = this.lastTs ? Math.min(0.05, (ts - this.lastTs) / 1000) : 0.016;
    this.lastTs = ts;

    this.frames += 1;
    if (ts - this.fpsClock >= 1000) {
      this.fps = Math.round(this.frames * 1000 / (ts - this.fpsClock));
      this.frames = 0;
      this.fpsClock = ts;
    }

    var pad = wx.getGamepadState();
    var axes = pad.axes || [0, 0];
    var box = this.box;
    var speed = 430;

    box.x += axes[0] * speed * dt;
    box.y += axes[1] * speed * dt;

    if (this.press.active) {
      box.x += (this.press.x - SIZE / 2 - box.x) * Math.min(1, dt * 10);
      box.y += (this.press.y - SIZE / 2 - box.y) * Math.min(1, dt * 10);
    }

    box.x = Math.max(0, Math.min(W - SIZE, box.x));
    box.y = Math.max(0, Math.min(H - SIZE, box.y));

    if (pad.buttons && pad.buttons.a && this.jump <= 0) {
      this.jump = 1;
      wx.vibrateShort({ type: 'light' });
    }
    if (this.jump > 0) this.jump = Math.max(0, this.jump - dt * 2.4);

    this.draw(pad, ts);

    if (ts - this.padClock >= 200) {
      this.padClock = ts;
      var pressed = (pad.pressed && pad.pressed.length) ? pad.pressed.join(',') : '无';
      this.setData({
        padText: pad.connected ? ('已连接 · ' + pressed) : '未检测',
        axesText: axes[0].toFixed(2) + ' / ' + axes[1].toFixed(2)
      });
    }
  },

  draw: function (pad, ts) {
    var ctx = this.ctx;
    var box = this.box;

    ctx.setFillStyle('#0b0e12');
    ctx.fillRect(0, 0, W, H);

    ctx.setStrokeStyle('#1a222c');
    ctx.setLineWidth(1);
    var step = 42;
    for (var x = step; x < W; x += step) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, H);
      ctx.stroke();
    }
    for (var y = step; y < H; y += step) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(W, y);
      ctx.stroke();
    }

    var lift = this.jump * 10;
    ctx.setFillStyle('#10dc78');
    ctx.fillRect(box.x, box.y - lift, SIZE, SIZE);

    ctx.setFillStyle('#eef1f5');
    ctx.setFontSize(14);
    ctx.fillText('FPS ' + this.fps + '  A 跳跃  ' + (pad.connected ? '' : '无手柄'), 12, 24);

    ctx.setFillStyle('#5b6875');
    ctx.fillText('左摇杆移动 · 触屏拖拽 · 长按画布', 12, H - 14);

    ctx.draw();
  },

  onTouchStart: function (e) {
    var t = (e.touches && e.touches[0]) || (e.changedTouches && e.changedTouches[0]);
    if (!t) return;
    this.press.active = true;
    this.press.x = e.x !== undefined ? e.x : t.clientX;
    this.press.y = e.y !== undefined ? e.y : t.clientY;
    this.setData({ touchText: '按下 ' + Math.round(this.press.x) + ',' + Math.round(this.press.y) });
  },

  onTouchMove: function (e) {
    var t = (e.touches && e.touches[0]) || (e.changedTouches && e.changedTouches[0]);
    if (!t) return;
    this.press.x = e.x !== undefined ? e.x : t.clientX;
    this.press.y = e.y !== undefined ? e.y : t.clientY;
    this.setData({ touchText: '拖动 ' + Math.round(this.press.x) + ',' + Math.round(this.press.y) });
  },

  onTouchEnd: function () {
    this.press.active = false;
    this.setData({ touchText: '结束' });
  },

  onLongPress: function () {
    this.setData({ touchText: '长按触发' });
    wx.vibrateShort({ type: 'heavy' });
  },

  onVibrate: function () {
    wx.vibrateShort({ type: 'heavy' });
    this.setData({ vibrateText: '已触发 ' + new Date().toLocaleTimeString() });
  },

  onToggleMusic: function () {
    if (!this.music) return;
    if (this.musicOn) {
      this.music.pause();
      this.musicOn = false;
      this.setData({ musicLabel: '播放 BGM', audioText: 'BGM 已暂停' });
    } else {
      this.music.play();
      this.musicOn = true;
      this.setData({ musicLabel: '暂停 BGM', audioText: 'BGM 播放中' });
    }
  },

  onBeep: function () {
    if (!this.beep) return;
    this.beep.stop();
    this.beep.play();
    this.setData({ audioText: '音效 ' + new Date().toLocaleTimeString() });
  },

  onConnect: function () {
    if (this.socket) {
      this.socket.close();
      this.socket = null;
      this.setData({ wsText: '未连接' });
      return;
    }
    var self = this;
    var socket = wx.connectSocket({ url: 'wss://echo.websocket.events' });
    this.socket = socket;
    this.setData({ wsText: '连接中…' });
    socket.onOpen(function () {
      self.setData({ wsText: '已连接' });
    });
    socket.onMessage(function (res) {
      self.setData({ wsText: '收到：' + String(res.data).slice(0, 32) });
    });
    socket.onError(function (res) {
      self.setData({ wsText: '错误：' + (res.errMsg || 'unknown') });
    });
    socket.onClose(function () {
      self.setData({ wsText: '已断开' });
      self.socket = null;
    });
  },

  onSend: function () {
    if (!this.socket) {
      this.setData({ wsText: '请先连接' });
      return;
    }
    this.socket.send({ data: 'ping ' + Date.now() });
    this.setData({ wsText: '已发送 Ping' });
  }
});
