var QUOTES = [
  { t: '人生如逆旅，我亦是行人。', a: '苏轼', s: '临江仙·送钱穆父', tag: '心境' },
  { t: '山重水复疑无路，柳暗花明又一村。', a: '陆游', s: '游山西村', tag: '坚持' },
  { t: '长风破浪会有时，直挂云帆济沧海。', a: '李白', s: '行路难', tag: '坚持' },
  { t: '千磨万击还坚劲，任尔东西南北风。', a: '郑燮', s: '竹石', tag: '坚持' },
  { t: '纸上得来终觉浅，绝知此事要躬行。', a: '陆游', s: '冬夜读书示子聿', tag: '读书' },
  { t: '问渠那得清如许？为有源头活水来。', a: '朱熹', s: '观书有感', tag: '读书' },
  { t: '读书破万卷，下笔如有神。', a: '杜甫', s: '奉赠韦左丞丈二十二韵', tag: '读书' },
  { t: '粗缯大布裹生涯，腹有诗书气自华。', a: '苏轼', s: '和董传留别', tag: '读书' },
  { t: '非淡泊无以明志，非宁静无以致远。', a: '诸葛亮', s: '诫子书', tag: '心境' },
  { t: '采菊东篱下，悠然见南山。', a: '陶渊明', s: '饮酒', tag: '自然' },
  { t: '明月松间照，清泉石上流。', a: '王维', s: '山居秋暝', tag: '自然' },
  { t: '行到水穷处，坐看云起时。', a: '王维', s: '终南别业', tag: '心境' },
  { t: '落霞与孤鹜齐飞，秋水共长天一色。', a: '王勃', s: '滕王阁序', tag: '自然' },
  { t: '海上生明月，天涯共此时。', a: '张九龄', s: '望月怀远', tag: '自然' },
  { t: '海内存知己，天涯若比邻。', a: '王勃', s: '送杜少府之任蜀州', tag: '友情' },
  { t: '但愿人长久，千里共婵娟。', a: '苏轼', s: '水调歌头', tag: '情感' },
  { t: '人生自古谁无死，留取丹心照汗青。', a: '文天祥', s: '过零丁洋', tag: '气节' },
  { t: '粉骨碎身浑不怕，要留清白在人间。', a: '于谦', s: '石灰吟', tag: '气节' },
  { t: '不要人夸好颜色，只留清气满乾坤。', a: '王冕', s: '墨梅', tag: '气节' },
  { t: '天生我材必有用，千金散尽还复来。', a: '李白', s: '将进酒', tag: '自信' },
  { t: '仰天大笑出门去，我辈岂是蓬蒿人。', a: '李白', s: '南陵别儿童入京', tag: '自信' },
  { t: '会当凌绝顶，一览众山小。', a: '杜甫', s: '望岳', tag: '志向' },
  { t: '欲穷千里目，更上一层楼。', a: '王之涣', s: '登鹳雀楼', tag: '志向' },
  { t: '路漫漫其修远兮，吾将上下而求索。', a: '屈原', s: '离骚', tag: '求索' },
  { t: '天行健，君子以自强不息。', a: '《周易》', s: '乾卦', tag: '坚持' },
  { t: '地势坤，君子以厚德载物。', a: '《周易》', s: '坤卦', tag: '修养' },
  { t: '君子坦荡荡，小人长戚戚。', a: '《论语》', s: '述而', tag: '修养' },
  { t: '三人行，必有我师焉。', a: '《论语》', s: '述而', tag: '学习' },
  { t: '学而不思则罔，思而不学则殆。', a: '《论语》', s: '为政', tag: '学习' },
  { t: '知之者不如好之者，好之者不如乐之者。', a: '《论语》', s: '雍也', tag: '学习' },
  { t: '不积跬步，无以至千里。', a: '荀子', s: '劝学', tag: '坚持' },
  { t: '锲而不舍，金石可镂。', a: '荀子', s: '劝学', tag: '坚持' },
  { t: '业精于勤，荒于嬉。', a: '韩愈', s: '进学解', tag: '勤奋' },
  { t: '少壮不努力，老大徒伤悲。', a: '汉乐府', s: '长歌行', tag: '时间' },
  { t: '一寸光阴一寸金，寸金难买寸光阴。', a: '王贞白', s: '白鹿洞二首', tag: '时间' },
  { t: '盛年不重来，一日难再晨。', a: '陶渊明', s: '杂诗', tag: '时间' },
  { t: '明日复明日，明日何其多。', a: '钱福', s: '明日歌', tag: '时间' },
  { t: '花开堪折直须折，莫待无花空折枝。', a: '杜秋娘', s: '金缕衣', tag: '时间' },
  { t: '沉舟侧畔千帆过，病树前头万木春。', a: '刘禹锡', s: '酬乐天扬州初逢席上见赠', tag: '豁达' },
  { t: '莫愁前路无知己，天下谁人不识君。', a: '高适', s: '别董大', tag: '友情' },
  { t: '桃花潭水深千尺，不及汪伦送我情。', a: '李白', s: '赠汪伦', tag: '友情' },
  { t: '谁言寸草心，报得三春晖。', a: '孟郊', s: '游子吟', tag: '亲情' },
  { t: '随风潜入夜，润物细无声。', a: '杜甫', s: '春夜喜雨', tag: '自然' },
  { t: '春风得意马蹄疾，一日看尽长安花。', a: '孟郊', s: '登科后', tag: '喜悦' },
  { t: '竹杖芒鞋轻胜马，谁怕？一蓑烟雨任平生。', a: '苏轼', s: '定风波', tag: '豁达' },
  { t: '回首向来萧瑟处，归去，也无风雨也无晴。', a: '苏轼', s: '定风波', tag: '豁达' },
  { t: '不经一番寒彻骨，怎得梅花扑鼻香。', a: '黄檗禅师', s: '上堂开示颂', tag: '坚持' },
  { t: '宝剑锋从磨砺出，梅花香自苦寒来。', a: '古训', s: '', tag: '勤奋' },
  { t: '静以修身，俭以养德。', a: '诸葛亮', s: '诫子书', tag: '修养' },
  { t: '己所不欲，勿施于人。', a: '《论语》', s: '颜渊', tag: '修养' },
  { t: '千里之行，始于足下。', a: '老子', s: '道德经', tag: '行动' },
  { t: '知人者智，自知者明。', a: '老子', s: '道德经', tag: '修养' },
  { t: '上善若水，水善利万物而不争。', a: '老子', s: '道德经', tag: '心境' },
  { t: '天下难事，必作于易；天下大事，必作于细。', a: '老子', s: '道德经', tag: '行动' },
  { t: '合抱之木，生于毫末。', a: '老子', s: '道德经', tag: '积累' },
  { t: '穷则独善其身，达则兼济天下。', a: '孟子', s: '尽心上', tag: '修养' }
];

var WEEK = ['星期日', '星期一', '星期二', '星期三', '星期四', '星期五', '星期六'];

Page({
  data: {
    dateText: '',
    quote: { t: '', a: '', s: '', tag: '' },
    authorText: '',
    isFav: false,
    favorites: []
  },

  onLoad() {
    var d = new Date();
    var dateText = (d.getMonth() + 1) + '月' + d.getDate() + '日 · ' + WEEK[d.getDay()];
    var dayNum = Math.floor(d.getTime() / 86400000);
    this.quoteIndex = dayNum % QUOTES.length;
    this.favorites = wx.getStorageSync('favs') || [];
    this.setData({ dateText: dateText, favorites: this.favorites });
    this.showQuote(this.quoteIndex);
  },

  showQuote(i) {
    var q = QUOTES[i];
    var author = q.a + (q.s ? '《' + q.s + '》' : '');
    this.quoteIndex = i;
    this.setData({
      quote: q,
      authorText: '— ' + author,
      isFav: this.findFav(q) >= 0
    });
  },

  findFav(q) {
    for (var i = 0; i < this.favorites.length; i++) {
      if (this.favorites[i].t === q.t) return i;
    }
    return -1;
  },

  onNext() {
    this.showQuote((this.quoteIndex + 1) % QUOTES.length);
  },

  onFav() {
    var q = this.data.quote;
    var pos = this.findFav(q);
    if (pos >= 0) {
      this.favorites.splice(pos, 1);
      wx.showToast({ title: '已取消收藏', icon: 'none' });
    } else {
      var text = q.t.length > 20 ? q.t.slice(0, 20) + '…' : q.t;
      this.favorites.unshift({ t: q.t, a: q.a, s: q.s, short: text });
      wx.showToast({ title: '已收藏', icon: 'none' });
    }
    wx.setStorageSync('favs', this.favorites);
    this.setData({
      favorites: this.favorites,
      isFav: this.findFav(q) >= 0
    });
  },

  onDelFav(e) {
    var i = e.currentTarget.dataset.i;
    this.favorites.splice(i, 1);
    wx.setStorageSync('favs', this.favorites);
    this.setData({
      favorites: this.favorites,
      isFav: this.findFav(this.data.quote) >= 0
    });
  },

  onCopy() {
    var q = this.data.quote;
    var text = q.t + '\n' + this.data.authorText;
    wx.setClipboardData({
      data: text,
      success: function () {
        wx.showToast({ title: '已复制', icon: 'none' });
      }
    });
  }
});
