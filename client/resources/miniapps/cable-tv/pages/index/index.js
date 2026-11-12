// 有线电视 · 央视 + 卫视直播
//
// 设计目标：启动即出画面。所有频道与线路都内置在包里，冷启动不做任何网络
// 解析，onLoad 里直接给 <video> 一个 src，由容器的 autoplay 起播。
//
// 已知容器行为（决定了下面的写法）：
// 1. <video autoplay> 对同一个节点只会触发一次起播，所以换台不能只改 src，
//    必须先 pause，等新 src 进入渲染树后再 play，播放器才会重新 load。
// 2. 视频画面在 NanoVG 之后直出，任何 UI 都盖不到画面上，所以频道表打开时
//    把 video 节点整个隐藏（音频仍在播），关闭后再 play 恢复画面。
// 3. 没有按键事件，只能靠焦点 + A 确认，所以换台都做成可聚焦按钮。

var CHANNELS = [
  { no: '001', name: 'CCTV-1', sub: '综合', g: 'cctv', urls: ['http://204.12.221.218:8181/3m1080p/cctv1.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv1hd', 'http://74.91.26.218:82/live/cctv1hd.m3u8'] },
  { no: '002', name: 'CCTV-2', sub: '财经', g: 'cctv', urls: ['http://204.12.221.218:8181/3m1080p/cctv2.m3u8', 'http://198.204.228.26/live/cctv2hd.m3u8', 'http://bztv.tvbus.cc:8081/cdnlive/cctv2.m3u8'] },
  { no: '003', name: 'CCTV-3', sub: '综艺', g: 'cctv', urls: ['http://74.91.26.218:82/live/cctv3hd.m3u8', 'http://107.150.60.122/live/cctv3hd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv3hd'] },
  { no: '004', name: 'CCTV-4', sub: '中文国际', g: 'cctv', urls: ['http://207.56.13.146:81/cdnlive/cctv4.m3u8', 'http://173.208.212.130:8181/1080p/cctv4.m3u8', 'http://74.91.26.218:82/live/cctv4hd.m3u8'] },
  { no: '005', name: 'CCTV-5', sub: '体育', g: 'cctv', urls: ['http://107.150.60.122/live/cctv5hd.m3u8', 'https://zby.130519.xyz/PLTV/11/224/3221225734/index.m3u8', 'https://www.freetv.top/migu/641886683.m3u8?migutoken=3e5c01f93922de8d1c940fe15bb47349b27e8f7a2e624dde1a83dff3b2a80f58b7624151a3a4d00488edee34f202818557340903382fae02bb754084eb942bd50e59061df36fab62fb5b908903'] },
  { no: '006', name: 'CCTV-5+', sub: '体育赛事', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv5p.m3u8?auth=test20251009', 'https://myip.pdtvhd.com/Sports/streams/CCTV5pul.m3u8', 'https://www.freetv.top/migu/641886773.m3u8?migutoken=03a1b82e35e58cd89a95034ecb29e77e50493397000ec2b1e4d1ce59af01e6420d6dbc068c8230ec03be3a35a20eed8714e0959e3570a08713a7a2e8d86085eabf8f1aa85744c80c38343988b2'] },
  { no: '007', name: 'CCTV-6', sub: '电影', g: 'cctv', urls: ['http://198.204.228.26/live/cctv6hd.m3u8', 'http://74.91.26.218:82/live/cctv6hd.m3u8', 'http://69.30.245.50/live/cctv6.m3u8'] },
  { no: '008', name: 'CCTV-7', sub: '国防军事', g: 'cctv', urls: ['http://207.56.13.146:81/cdnlive/cctv7.m3u8', 'http://74.91.26.218:82/live/cctv7hd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv7hd'] },
  { no: '009', name: 'CCTV-8', sub: '电视剧', g: 'cctv', urls: ['http://bztv.tvbus.cc:8081/cdnlive/cctv8.m3u8', 'http://74.91.26.218:82/live/cctv8hd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv8k'] },
  { no: '010', name: 'CCTV-9', sub: '纪录', g: 'cctv', urls: ['http://74.91.26.218:82/live/cctv9hd.m3u8', 'http://192.151.150.154/live/cctv9hd.m3u8', 'http://107.150.60.122/live/cctv9hd.m3u8'] },
  { no: '011', name: 'CCTV-10', sub: '科教', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv10hd.m3u8?auth=test20251009', 'http://74.91.26.218:82/live/cctv10hd.m3u8', 'http://204.12.221.218:8181/3m1080p/cctv10.m3u8'] },
  { no: '012', name: 'CCTV-11', sub: '戏曲', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv11hd.m3u8?auth=test20251009', 'http://74.91.26.218:82/live/cctv11hd.m3u8', 'http://198.204.228.26/live/cctv11hd.m3u8'] },
  { no: '013', name: 'CCTV-12', sub: '社会与法', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqpub/cctv12hd.m3u8?auth=testpub', 'http://74.91.26.218:82/live/cctv12hd.m3u8', 'http://107.150.60.122/live/cctv12hd.m3u8'] },
  { no: '014', name: 'CCTV-13', sub: '新闻', g: 'cctv', urls: ['http://120.76.248.139/live/bfgd/4200000067.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv13hd', 'http://38.75.136.137:98/gslb/dsdqbv/cctv13hd.m3u8?auth=test20251009'] },
  { no: '015', name: 'CCTV-14', sub: '少儿', g: 'cctv', urls: ['http://198.204.228.26/live/cctv14hd.m3u8', 'http://204.12.221.218:8181/3m1080p/cctv14.m3u8', 'http://38.75.136.137:98/gslb/dsdqbv/cctv14hd.m3u8?auth=test20251009'] },
  { no: '016', name: 'CCTV-15', sub: '音乐', g: 'cctv', urls: ['http://204.12.221.218:8181/3m1080p/cctv15.m3u8', 'https://zby.130519.xyz/PLTV/11/224/3221226147/index.m3u8', 'https://www.freetv.top/migu/673168223.m3u8?migutoken=4e8d5766579c6bef231494cabd07e5b49d965804790214280c9791e4c331fd90311cb62ea43d3a1899229bfa57be4afe5a567b32a6b78153fc9a9801e28a39435e078b48c211e71d6083519be4'] },
  { no: '017', name: 'CCTV-16', sub: '奥林匹克', g: 'cctv', urls: ['http://207.56.13.146:81/cdnlive/cctv16.m3u8', 'http://38.75.136.137:98/gslb/dsdqbv/cctv16hd.m3u8?auth=test20251009', 'https://zby.130519.xyz/PLTV/11/224/3221226120/index.m3u8'] },
  { no: '018', name: 'CCTV-17', sub: '农业农村', g: 'cctv', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv17hd', 'http://74.91.26.218:82/live/cctv17hd.m3u8', 'https://live.264788.xyz/channel/cctv17?streamid=348c8c0c04a6d2b900e5a3bcb4af8ce1&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk'] },
  { no: '101', name: '湖南卫视', sub: '省级卫视', g: 'ws', urls: ['http://hlsal-ldvt.qing.mgtv.com/nn_live/nn_x64/dWlwPTEyNy4wLjAuMSZ1aWQ9cWluZy1jbXMmbm5fdGltZXpvbmU9OCZjZG5leF9pZD1hbF9obHNfbGR2dCZ1dWlkPTliODY4NmU5ZTM2YzYwMmMmZT02OTE0NjA0JnY9MSZpZD1ITldTWkdTVCZzPTcwN2RiYTc2YzJjNmJmMTQ4MmUyZGYzOWU2NWM3YWFi/HNWSZGST.m3u8', 'https://txmov2.a.kwimgs.com/bs3/video-hls/5199687338554291078_hlsb.m3u8', 'http://107.150.60.122/live/hnwshd.m3u8'] },
  { no: '102', name: '浙江卫视', sub: '省级卫视', g: 'ws', urls: ['https://ali-m-l.cztv.com/channels/lantian/channel001/1080p.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/zjwshd.m3u8?auth=testpub', 'http://107.150.60.122/live/zjwshd.m3u8'] },
  { no: '103', name: '江苏卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqbv/jswshd.m3u8?auth=test20251009', 'http://107.150.60.122/live/jswshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jswshd'] },
  { no: '104', name: '东方卫视', sub: '省级卫视', g: 'ws', urls: ['https://txmov2.a.kwimgs.com/bs3/video-hls/5219953535631090825_hlsb.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=dfwshd', 'http://38.75.136.137:98/gslb/dsdqpub/dfwshd.m3u8?auth=testpub'] },
  { no: '105', name: '北京卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqbv/bjwshd.m3u8?auth=test20251009', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=bjwshd', 'http://107.150.60.122/live/bjwshd.m3u8'] },
  { no: '106', name: '广东卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/gdwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gdwshd', 'https://txmov2.a.kwimgs.com/upic/2023/01/26/09/BMjAyMzAxMjYwOTE3NDVfNzM5MzQzNzIyXzk0NjI1OTUwNTA3XzBfMw==_b_B81ddda927d33445d544befd008e60109.mp4'] },
  { no: '107', name: '深圳卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/szwshd.m3u8', 'https://zby.130519.xyz/PLTV/11/224/3221225728/index.m3u8', 'https://smart.pendy.dpdns.org/Smart.php?id=shenzhen'] },
  { no: '108', name: '安徽卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/ahwshd.m3u8', 'http://204.12.221.218:8181/3m1080p/ahws.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=ahwshd'] },
  { no: '109', name: '山东卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000099.m3u8', 'http://107.150.60.122/live/sdwshd.m3u8', 'http://198.204.228.26/live/sdwshd.m3u8'] },
  { no: '110', name: '湖北卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000102.m3u8', 'http://107.150.60.122/live/hbwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hbwshd'] },
  { no: '111', name: '河南卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hawshd', 'http://107.150.60.122/live/hawshd.m3u8', 'http://173.208.212.130:8181/720p/haws.m3u8'] },
  { no: '112', name: '河北卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000108.m3u8', 'https://event.pull.hebtv.com/live/live101.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hewshd'] },
  { no: '113', name: '天津卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/tjwshd.m3u8?auth=testpub', 'http://107.150.60.122/live/tjwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=tjwshd'] },
  { no: '114', name: '四川卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/scwshd.m3u8?auth=testpub', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=scwshd', 'https://live.264788.xyz/channel/sichuanweishi4k?streamid=21be5901ee81559c827dc84e46f26452&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk'] },
  { no: '115', name: '重庆卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/cqwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cqwshd', 'https://zby.130519.xyz/PLTV/11/224/3221226171/index.m3u8'] },
  { no: '116', name: '辽宁卫视', sub: '省级卫视', g: 'ws', urls: ['http://204.12.221.218:8181/3m1080p/lnws.m3u8', 'http://198.204.228.26/live/lnwshd.m3u8', 'http://107.150.60.122/live/lnwshd.m3u8'] },
  { no: '117', name: '黑龙江卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000095.m3u8', 'http://198.204.228.26/live/hljwshd.m3u8', 'http://107.150.60.122/live/hljwshd.m3u8'] },
  { no: '118', name: '吉林卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jlwshd', 'http://107.150.60.122/live/jlwshd.m3u8', 'https://zby.130519.xyz/PLTV/11/224/3221226149/index.m3u8'] },
  { no: '119', name: '内蒙古卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/nmgws.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=nmgws', 'https://live.264788.xyz/channel/neimengguweishi?streamid=81a015dbecf3031d7d847b7a9b059f67&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk'] },
  { no: '120', name: '山西卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/sxwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=sxwshd', 'https://live.264788.xyz/channel/shanxiweishi?streamid=eeb7d60ca6e0db131ea7a092a3cd28d2&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk'] },
  { no: '121', name: '陕西卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/snwshd.m3u8', 'http://192.151.150.154/live/snwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=snwshd'] },
  { no: '122', name: '甘肃卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000119.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/gswshd.m3u8?auth=testpub', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gswshd'] },
  { no: '123', name: '青海卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/qhws.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=qhws', 'https://hls-qhmh.lanzhousobey.cn/qhmh/mhds.m3u8'] },
  { no: '124', name: '宁夏卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/nxws.m3u8', 'https://live.264788.xyz/channel/ningxiaweishi?streamid=e1236348f815c36a0429357e5b5c5f10&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk', 'https://live.264788.xyz/channel/ningxiaweishi?streamid=551474d02bee1c160e1692e22c45ddba&livekey=01WgOR41rriMmMkzNsd0UoaxJRwetZdxIvtVk'] },
  { no: '125', name: '新疆卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/xjws.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=xjws', 'http://198.204.228.26/live/xjws.m3u8'] },
  { no: '126', name: '西藏卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/xzws.m3u8', 'http://112.27.5.218:9901/tsfile/live/0111_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=xzws'] },
  { no: '127', name: '云南卫视', sub: '省级卫视', g: 'ws', urls: ['http://192.151.150.154/live/ynwshd.m3u8', 'http://107.150.60.122/live/ynwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=ynwshd'] },
  { no: '128', name: '贵州卫视', sub: '省级卫视', g: 'ws', urls: ['http://204.12.221.218:8181/3m1080p/gzws.m3u8', 'http://107.150.60.122/live/gzwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gzwshd'] },
  { no: '129', name: '广西卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000116.m3u8', 'http://198.204.228.26/live/gxwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gxwshd'] },
  { no: '130', name: '海南卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000473.m3u8', 'https://zby.130519.xyz/PLTV/11/224/3221226154/index.m3u8', 'http://[2409:8087:8:21::18]:6610/otttv.bj.chinamobile.com/PLTV/88888888/224/3221227029/1.m3u8?GuardEncType=2&accountinfo=%7E%7EV2.0%7EEuBMjt2kLMD8fAO7QYER7Q%7EtP4-l0lmSfjwLWEfK_el1vH_mv-s1zo4AQJwdedaVwG9xkuFTDg8J26cwOrNJzn20BErrHdLhuZ9EzLUCD3PMW-OMx4MGteHV2vLeW6BqoY%2CEND'] },
  { no: '131', name: '江西卫视', sub: '省级卫视', g: 'ws', urls: ['http://112.27.5.218:9901/tsfile/live/faacts/0138_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://107.150.60.122/live/jxwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jxwshd'] },
  { no: '132', name: '东南卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000483.m3u8', 'http://107.150.60.122/live/dnwshd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/dnwshd.m3u8?auth=testpub'] },
  { no: '133', name: '三沙卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/ssws.m3u8?auth=testpub', 'http://198.204.228.26/live/ssws.m3u8', 'http://107.150.60.122/live/ssws.m3u8'] },
  { no: '134', name: '香港卫视', sub: '省级卫视', g: 'ws', urls: ['https://stream1.freetv.fun/5335c88a9a7d8b74173b491f480d4f94e8d3bd596af54478106aa2399e6c7917.ctv', 'https://stream1.freetv.fun/c751876ae5ceaa0d82b5cf2badc152342aa92a4b45bf504d12ee45966b2fc644.ctv', 'https://stream1.freetv.fun/5335c88a9a7d8b74173b491f480d4f94e8d3bd596af54478106aa2399e6c7917.m3u8'] }
];

var STORE_KEY = 'ctv_ch';
var SRC_KEY = 'ctv_src';
var SRC_AT_KEY = 'ctv_src_at';
var SRC_URL = 'http://ecfun.cc/api/v1/tv/channels';
var SRC_TTL = 6 * 3600 * 1000;
var CONNECT_TIMEOUT = 12000;

function indexOfNo(no) {
  for (var i = 0; i < CHANNELS.length; i++) {
    if (CHANNELS[i].no === no) return i;
  }
  return -1;
}

Page({
  data: {
    src: '',
    chno: '001',
    name: '',
    sub: '',
    groupLabel: '',
    status: '正在连接…',
    live: false,
    panel: false,
    tab: 'all',
    rows: [],
    // 新版容器支持页面按键事件时，控制条默认显示几秒后自动收起。
    keys: false,
    hud: true
  },

  onLoad: function() {
    this.line = 0;
    this.index = 0;
    this.warned = false;
    this.watchTimer = null;
    this.retryTimer = null;
    this.resumeTimer = null;
    this.refreshTimer = null;
    this.hudTimer = null;

    // 旧容器没有 keyEvents 标记：控制条常驻，退回纯按钮操作。
    var keys = false;
    try {
      var info = wx.getSystemInfoSync ? wx.getSystemInfoSync() : null;
      keys = !!(info && info.keyEvents);
    } catch (e) {}
    this.keys = keys;
    this.fs = false;
    this.entered = false;
    this.wasFs = false;
    if (keys) this.setData({ keys: true });

    // 先用上次缓存的新线路覆盖内置表（同步、无网络等待），再起播。
    this.applyCachedSources();

    var idx = 0;
    try {
      var saved = wx.getStorageSync(STORE_KEY);
      var hit = indexOfNo(saved);
      if (hit >= 0) idx = hit;
    } catch (e) {}
    this.tune(CHANNELS[idx], 0, true);

    // 起播之后再静默刷新，慢网络也不影响出画面。
    var self = this;
    this.refreshTimer = setTimeout(function() {
      self.refreshTimer = null;
      self.refreshSources();
    }, 4000);
  },

  applyCachedSources: function() {
    var raw = '';
    try { raw = wx.getStorageSync(SRC_KEY); } catch (e) { return; }
    if (!raw) return;
    var data = raw;
    if (typeof raw === 'string') {
      try { data = JSON.parse(raw); } catch (e) { return; }
    }
    if (data && data.list) this.mergeSources(data.list, '');
  },

  // 按频道名覆盖线路；skipName 用于跳过正在播放的频道，避免切线路错位。
  mergeSources: function(list, skipName) {
    if (!list || !list.length) return 0;
    var map = {};
    for (var i = 0; i < list.length; i++) {
      var item = list[i];
      if (item && item.name && item.urls && item.urls.length) map[item.name] = item.urls;
    }
    var applied = 0;
    for (var j = 0; j < CHANNELS.length; j++) {
      var ch = CHANNELS[j];
      var urls = map[ch.name];
      if (urls && ch.name !== skipName) {
        ch.urls = urls.slice(0, 3);
        applied++;
      }
    }
    return applied;
  },

  // 服务端抓取并缓存最新线路，6 小时内不重复请求；失败完全静默。
  refreshSources: function() {
    if (!wx.request) return;
    var last = 0;
    try { last = Number(wx.getStorageSync(SRC_AT_KEY)) || 0; } catch (e) {}
    var now = Date.now();
    if (last && now - last < SRC_TTL) return;
    var self = this;
    wx.request({
      url: SRC_URL,
      method: 'GET',
      success: function(res) {
        var body = res && res.data;
        if (typeof body === 'string') {
          try { body = JSON.parse(body); } catch (e) { return; }
        }
        if (!body || !body.channels || !body.channels.length) return;
        var list = [];
        for (var i = 0; i < body.channels.length; i++) {
          var c = body.channels[i];
          if (!c || !c.name || !c.urls || !c.urls.length) continue;
          list.push({ name: c.name, urls: c.urls });
        }
        if (list.length < 10) return;
        try {
          wx.setStorageSync(SRC_KEY, JSON.stringify({ list: list }));
          wx.setStorageSync(SRC_AT_KEY, now);
        } catch (e) {}
        self.mergeSources(list, self.cur ? self.cur.name : '');
      }
    });
  },

  // 切到一个频道的某条线路。initial=true 时交给 autoplay 起播。
  tune: function(ch, line, initial) {
    if (!ch) return;
    this.clearWatch();
    this.cur = ch;
    this.line = line || 0;
    this.warned = false;
    var idx = indexOfNo(ch.no);
    this.index = idx < 0 ? 0 : idx;

    var urls = ch.urls || [];
    var url = urls[this.line] || '';
    if (!url) {
      this.setData({ status: '这个频道暂时没有可用线路', live: false });
      return;
    }

    if (!initial) {
      try { wx.createVideoContext('player').pause(); } catch (e) {}
    }

    var patch = {
      src: url,
      chno: ch.no,
      name: ch.name,
      sub: ch.sub,
      groupLabel: ch.g === 'cctv' ? '央视' : '卫视',
      status: urls.length > 1 ? '正在连接 ' + (this.line + 1) + '/' + urls.length + ' 号线路' : '正在连接…',
      live: false,
      panel: false
    };
    if (this.data.panel) patch.rows = [];
    this.setData(patch);

    try { wx.setStorageSync(STORE_KEY, ch.no); } catch (e) {}

    var self = this;
    if (!initial) {
      this.resumeTimer = setTimeout(function() { self.resume(); }, 160);
    }
    this.watchTimer = setTimeout(function() { self.onStall(); }, CONNECT_TIMEOUT);
  },

  // 首次布局完成就进入全屏，缓冲阶段也是满屏，不闪窗口。
  onReady: function() {
    if (!this.keys) return;
    this.requestFs();
    this.armHud();
  },

  resume: function() {
    this.resumeTimer = null;
    try { wx.createVideoContext('player').play(); } catch (e) {}
    // 从频道表/换台回来时，若之前是全屏，自动恢复全屏播放。
    if (this.keys && this.wasFs) {
      this.wasFs = false;
      this.entered = true;
      this.requestFs();
      this.armHud();
    }
  },

  onPlay: function() {
    this.clearWatch();
    if (this.data.status !== '直播中' || !this.data.live) {
      this.setData({ status: '直播中', live: true });
    }
    // 兜底：onReady 的全屏请求没生效时，出画面后再请求一次。
    if (this.keys && !this.entered && !this.fs) {
      this.entered = true;
      this.requestFs();
      this.armHud();
    }
  },

  onPause: function() {
    this.setData({ status: '已暂停', live: false });
  },

  onWaiting: function() {
    this.setData({ status: '缓冲中…', live: false });
  },

  onEnded: function() {
    this.setData({ status: '信号中断，自动切换…', live: false });
    this.nextLine();
  },

  onVideoError: function() {
    if (!this.warned) {
      this.warned = true;
      wx.showToast({ title: '线路不可用，自动切换', icon: 'none' });
    }
    this.nextLine();
  },

  // 连接超时（一直没等到 play）也换下一条线路。
  onStall: function() {
    this.watchTimer = null;
    this.nextLine();
  },

  // 当前频道换下一条线路；线路用尽后 6 秒从第一条重试。
  nextLine: function() {
    var ch = this.cur;
    if (!ch) return;
    var urls = ch.urls || [];
    this.clearWatch();
    if (this.line + 1 < urls.length) {
      this.tune(ch, this.line + 1, false);
      return;
    }
    this.setData({ status: '信号中断，6 秒后重试…', live: false });
    var self = this;
    this.retryTimer = setTimeout(function() { self.tune(ch, 0, false); }, 6000);
  },

  step: function(delta) {
    var n = CHANNELS.length;
    if (!n) return;
    var next = (this.index + delta + n) % n;
    this.tune(CHANNELS[next], 0, false);
  },

  onPrev: function() {
    this.step(-1);
  },

  onNext: function() {
    this.step(1);
  },

  requestFs: function() {
    try { wx.createVideoContext('player').requestFullScreen(); } catch (e) {}
  },

  onToggleFullscreen: function() {
    if (this.fs) {
      try { wx.createVideoContext('player').exitFullScreen(); } catch (e) {}
    } else {
      this.requestFs();
    }
  },

  // 容器进入/退出全屏（B 键、requestFullScreen、exitFullScreen）都会回调。
  onFsChange: function(e) {
    this.fs = !!(e && e.detail && e.detail.fullScreen);
    if (this.fs) {
      this.armHud();
      return;
    }
    if (this.hudTimer) {
      clearTimeout(this.hudTimer);
      this.hudTimer = null;
    }
    // 退出全屏后控制条常驻，保持窗口模式可用。
    if (this.keys && !this.data.hud && !this.data.panel) this.setData({ hud: true });
  },

  showHud: function() {
    if (!this.keys) return;
    if (!this.data.hud) this.setData({ hud: true });
    this.armHud();
  },

  // 全屏且无操作 4 秒后收起控制条；窗口模式与频道表不收。
  armHud: function() {
    if (!this.keys) return;
    var self = this;
    if (this.hudTimer) {
      clearTimeout(this.hudTimer);
      this.hudTimer = null;
    }
    this.hudTimer = setTimeout(function() {
      self.hudTimer = null;
      if (self.fs && !self.data.panel) self.setData({ hud: false });
    }, 4000);
  },

  hideHud: function() {
    if (this.hudTimer) {
      clearTimeout(this.hudTimer);
      this.hudTimer = null;
    }
    if (!this.data.panel && this.data.hud) this.setData({ hud: false });
  },

  // 页面级按键：方向键直接换台，A 打开频道表，B 收起控制条 / 退出。
  // 返回 true 表示消费按键，容器不再执行默认焦点移动。
  onKeyDown: function(e) {
    if (!this.keys) return false;
    var key = e && e.key;
    if (this.data.panel) {
      // 频道表里把方向键和 A 留给容器做焦点导航与选中；B 关表。
      if (key === 'b') {
        this.onClose();
        return true;
      }
      return false;
    }
    if (key === 'left' || key === 'up') {
      this.step(-1);
      this.showHud();
      return true;
    }
    if (key === 'right' || key === 'down') {
      this.step(1);
      this.showHud();
      return true;
    }
    if (key === 'a') {
      if (!this.data.hud) {
        this.showHud();
        return true;
      }
      this.onList();
      return true;
    }
    if (key === 'b') {
      if (this.data.hud && this.fs) {
        this.hideHud();
        return true;
      }
      return false;
    }
    return false;
  },

  onList: function() {
    if (this.data.panel) return;
    // 浏览频道表时把连接超时停掉，避免看着列表突然被换台。
    this.clearWatch();
    this.wasFs = !!(this.keys && this.fs);
    // 频道表要盖住画面，先退出全屏，避免 video 直出层压住列表。
    if (this.wasFs) {
      try { wx.createVideoContext('player').exitFullScreen(); } catch (e) {}
    }
    this.setData({ panel: true, hud: false, rows: this.buildRows(this.data.tab) });
  },

  onClose: function() {
    if (!this.data.panel) return;
    this.setData({ panel: false, hud: true });
    var self = this;
    // video 节点重新出现，play 一下让画面回来（同一地址不会重新加载）。
    this.resumeTimer = setTimeout(function() { self.resume(); }, 160);
  },

  onTab: function(e) {
    var tab = e.currentTarget.dataset.tab;
    if (!tab || tab === this.data.tab) return;
    this.setData({ tab: tab, rows: this.buildRows(tab) });
  },

  // 不支持 flex-wrap，按 4 列在 JS 里切成行，空位补 blank。
  buildRows: function(tab) {
    var rows = [];
    var row = null;
    var cols = 4;
    var count = 0;
    var curNo = this.cur ? this.cur.no : '';
    for (var i = 0; i < CHANNELS.length; i++) {
      var c = CHANNELS[i];
      if (tab === 'cctv' && c.g !== 'cctv') continue;
      if (tab === 'ws' && c.g !== 'ws') continue;
      if (count % cols === 0) {
        row = [];
        rows.push(row);
      }
      row.push({ id: c.no, no: c.no, name: c.name, on: c.no === curNo, blank: false });
      count++;
    }
    if (row) {
      while (row.length < cols) {
        row.push({ id: '', no: '', name: '', on: false, blank: true });
      }
    }
    return rows;
  },

  onPick: function(e) {
    var id = e.currentTarget.dataset.id;
    if (!id) return;
    if (this.cur && id === this.cur.no) {
      this.onClose();
      return;
    }
    var idx = indexOfNo(id);
    if (idx < 0) return;
    this.tune(CHANNELS[idx], 0, false);
  },

  clearWatch: function() {
    if (this.watchTimer) {
      clearTimeout(this.watchTimer);
      this.watchTimer = null;
    }
    if (this.retryTimer) {
      clearTimeout(this.retryTimer);
      this.retryTimer = null;
    }
    if (this.resumeTimer) {
      clearTimeout(this.resumeTimer);
      this.resumeTimer = null;
    }
  },

  // 容器销毁页面时不会主动停 MPV，必须自己停，否则回到桌面还有声音。
  onUnload: function() {
    this.clearWatch();
    if (this.refreshTimer) {
      clearTimeout(this.refreshTimer);
      this.refreshTimer = null;
    }
    if (this.hudTimer) {
      clearTimeout(this.hudTimer);
      this.hudTimer = null;
    }
    try { wx.createVideoContext('player').stop(); } catch (e) {}
  }
});
