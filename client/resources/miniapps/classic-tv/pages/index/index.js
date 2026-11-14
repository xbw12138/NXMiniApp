// 老式电视 · 只看央视和卫视
// 逻辑对齐老式电视机：打开就播上次看的台，上下键换台，屏幕上只留一个会自己收起的台标。

var CHANNELS = [
  { no: '001', name: 'CCTV-1', sub: '综合', g: 'cctv', urls: ['http://198.204.228.26/live/cctv1hd.m3u8', 'http://38.75.136.137:98/gslb/dsdqbv/cctv1hd.m3u8?auth=test20251009', 'http://204.12.221.218:8181/3m1080p/cctv1.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv1hd', 'http://74.91.26.218:82/live/cctv1hd.m3u8', 'https://liveplay-srs.voc.com.cn/hls/tv/134_180adf.m3u8'] },
  { no: '002', name: 'CCTV-2', sub: '财经', g: 'cctv', urls: ['http://192.151.150.154/live/cctv2hd.m3u8', 'http://204.12.221.218:8181/3m1080p/cctv2.m3u8', 'http://198.204.228.26/live/cctv2hd.m3u8', 'http://107.150.60.122/live/cctv2hd.m3u8'] },
  { no: '003', name: 'CCTV-3', sub: '综艺', g: 'cctv', urls: ['http://69.30.245.50/live/cctv3.m3u8', 'http://74.91.26.218:82/live/cctv3hd.m3u8', 'http://107.150.60.122/live/cctv3hd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv3hd'] },
  { no: '004', name: 'CCTV-4', sub: '中文国际', g: 'cctv', urls: ['http://107.150.60.122/live/cctv4hd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/cctv4k.m3u8?auth=testpub', 'http://173.208.212.130:8181/1080p/cctv4.m3u8', 'http://74.91.26.218:82/live/cctv4hd.m3u8'] },
  { no: '005', name: 'CCTV-5', sub: '体育', g: 'cctv', urls: ['http://107.150.60.122/live/cctv5hd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/cctv5p.m3u8?auth=testpub', 'http://gmxw.7766.org:808/hls/93/index.m3u8'] },
  { no: '006', name: 'CCTV-5+', sub: '体育赛事', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv5p.m3u8?auth=test20251009', 'http://223.78.65.165:9901/tsfile/live/0006_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://120.76.248.139/live/bfgd/4200000246.m3u8'] },
  { no: '007', name: 'CCTV-6', sub: '电影', g: 'cctv', urls: ['http://198.204.228.26/live/cctv6hd.m3u8', 'http://198.204.240.250:82/live/cctv6.m3u8', 'http://69.30.245.50/live/cctv6.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv6hd', 'http://74.91.26.218:82/live/cctv6hd.m3u8'] },
  { no: '008', name: 'CCTV-7', sub: '国防军事', g: 'cctv', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv7hd', 'http://107.150.60.122/live/cctv7hd.m3u8', 'http://74.91.26.218:82/live/cctv7hd.m3u8'] },
  { no: '009', name: 'CCTV-8', sub: '电视剧', g: 'cctv', urls: ['http://69.30.245.50/live/cctv8.m3u8', 'http://192.151.150.154/live/cctv8k.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv8k', 'http://74.91.26.218:82/live/cctv8hd.m3u8', 'http://124.165.251.82:85/tsfile/live/0008_1.m3u8?key=txiptv&playlive=1&authid=0'] },
  { no: '010', name: 'CCTV-9', sub: '纪录', g: 'cctv', urls: ['https://xykt-fix.github.io/Y77.m3u8', 'http://74.91.26.218:82/live/cctv9hd.m3u8', 'http://192.151.150.154/live/cctv9hd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv9hd', 'http://107.150.60.122/live/cctv9hd.m3u8'] },
  { no: '011', name: 'CCTV-10', sub: '科教', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv10hd.m3u8?auth=test20251009', 'http://74.91.26.218:82/live/cctv10hd.m3u8', 'http://204.12.221.218:8181/3m1080p/cctv10.m3u8', 'http://69.30.245.50/live/cctv10.m3u8', 'https://cdn4.skygo.mn/live/disk1/CCTV-10/HLSv3-FTA/CCTV-10.m3u8'] },
  { no: '012', name: 'CCTV-11', sub: '戏曲', g: 'cctv', urls: ['http://204.12.221.218:8181/3m1080p/cctv11.m3u8', 'http://38.75.136.137:98/gslb/dsdqbv/cctv11hd.m3u8?auth=test20251009', 'http://38.75.136.137:98/gslb/dsdqpub/cctv11hd.m3u8?auth=testpub', 'http://198.204.228.26/live/cctv11hd.m3u8', 'http://74.91.26.218:82/live/cctv11hd.m3u8'] },
  { no: '013', name: 'CCTV-12', sub: '社会与法', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv12hd.m3u8?auth=test20251009', 'http://74.91.26.218:82/live/cctv12hd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/cctv12hd.m3u8?auth=testpub', 'http://124.165.251.82:85/tsfile/live/0012_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://107.150.60.122/live/cctv12hd.m3u8'] },
  { no: '014', name: 'CCTV-13', sub: '新闻', g: 'cctv', urls: ['http://ali-m-l.cztv.com/channels/lantian/channel21/1080p.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv13hd', 'https://event.pull.hebtv.com/jishi/cp1.m3u8', 'http://38.75.136.137:98/gslb/dsdqbv/cctv13hd.m3u8?auth=test20251009', 'http://120.76.248.139/live/bfgd/4200000067.m3u8', 'http://74.91.26.218:82/live/cctv13hd.m3u8'] },
  { no: '015', name: 'CCTV-14', sub: '少儿', g: 'cctv', urls: ['http://204.12.221.218:8181/3m1080p/cctv14.m3u8', 'http://198.204.228.26/live/cctv14hd.m3u8', 'http://38.75.136.137:98/gslb/dsdqbv/cctv14hd.m3u8?auth=test20251009', 'http://74.91.26.218:82/live/cctv14hd.m3u8', 'https://event.pull.hebtv.com/jishi/cp2.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv14hd'] },
  { no: '016', name: 'CCTV-15', sub: '音乐', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv15hd.m3u8?auth=test20251009', 'http://204.12.221.218:8181/3m1080p/cctv15.m3u8', 'http://198.204.228.26/live/cctv15hd.m3u8', 'http://74.91.26.218:82/live/cctv15hd.m3u8', 'http://gmxw.7766.org:808/hls/102/index.m3u8', 'http://120.76.248.139/live/bfgd/4200000133.m3u8'] },
  { no: '017', name: 'CCTV-16', sub: '奥林匹克', g: 'cctv', urls: ['http://38.75.136.137:98/gslb/dsdqbv/cctv16hd.m3u8?auth=test20251009', 'http://38.75.136.137:98/gslb/dsdqpub/cctv16hd.m3u8?auth=testpub', 'http://gmxw.7766.org:808/hls/169/index.m3u8', 'http://74.91.26.218:82/live/cctv16hd.m3u8'] },
  { no: '018', name: 'CCTV-17', sub: '农业农村', g: 'cctv', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cctv17hd', 'http://38.75.136.137:98/gslb/dsdqbv/cctv17hd.m3u8?auth=test20251009', 'http://74.91.26.218:82/live/cctv17hd.m3u8', 'http://120.76.248.139/live/bfgd/4200000204.m3u8'] },
  { no: '101', name: '湖南卫视', sub: '省级卫视', g: 'ws', urls: ['https://txmov2.a.kwimgs.com/bs3/video-hls/5199687338554291078_hlsb.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hnwshd', 'http://107.150.60.122/live/hnwshd.m3u8', 'http://hlsal-ldvt.qing.mgtv.com/nn_live/nn_x64/Y2RuZXhfaWQ9YWxfaGxzX2xkdnQmZT02OTE0NjA0JnY9MSZpZD1ITldTWkdTVCZzPTcwN2RiYTc2YzJjNmJmMTQ4MmUyZGYzOWU2NWM3YWFi/HNWSZGST.m3u8'] },
  { no: '102', name: '浙江卫视', sub: '省级卫视', g: 'ws', urls: ['http://ali-m-l.cztv.com/channels/lantian/channel01/1080p.m3u8', 'http://ali-m-l.cztv.com:80/channels/lantian/channel001/1080p.m3u8', 'https://ali-m-l.cztv.com/channels/lantian/channel001/1080p.m3u8', 'http://ali-m-l.cztv.com/channels/lantian/channel001/1080p.m3u8', 'http://ali-xwl.cztv.com/live/channel011080Plxw.m3u8', 'http://107.150.60.122/live/zjwshd.m3u8'] },
  { no: '103', name: '江苏卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/jswshd.m3u8?auth=testpub', 'http://38.75.136.137:98/gslb/dsdqbv/jswshd.m3u8?auth=test20251009', 'http://107.150.60.122/live/jswshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jswshd', 'http://125.43.249.76:9901/tsfile/live/1030_1.m3u8?key=txiptv&playlive=1&authid=0'] },
  { no: '104', name: '东方卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqbv/dfwshd.m3u8?auth=test20251009', 'http://107.150.60.122/live/dfwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=dfwshd', 'http://38.75.136.137:98/gslb/dsdqpub/dfwshd.m3u8?auth=testpub', 'https://bp-resource-dfl.bestv.cn/148/3/video.m3u8', 'https://txmov2.a.kwimgs.com/bs3/video-hls/5219953535631090825_hlsb.m3u8'] },
  { no: '105', name: '北京卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=bjwshd', 'http://107.150.60.122/live/bjwshd.m3u8', 'http://38.75.136.137:98/gslb/dsdqbv/bjwshd.m3u8?auth=test20251009', 'http://120.76.248.139/live/bfgd/4200000083.m3u8'] },
  { no: '106', name: '广东卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/gdwshd.m3u8?auth=testpub', 'http://107.150.60.122/live/gdwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gdwshd', 'http://222.169.85.8:9901/tsfile/live/0125_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://120.76.248.139/live/bfgd/4200000092.m3u8'] },
  { no: '107', name: '深圳卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/szwshd.m3u8?auth=testpub', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=szwshd', 'http://107.150.60.122/live/szwshd.m3u8', 'http://gmxw.7766.org:808/hls/45/index.m3u8', 'http://120.76.248.139/live/bfgd/4200000100.m3u8'] },
  { no: '108', name: '安徽卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=ahwshd', 'http://107.150.60.122/live/ahwshd.m3u8', 'http://198.204.228.26/live/ahwshd.m3u8', 'http://125.43.249.76:9901/tsfile/live/1037_1.m3u8?key=txiptv&playlive=1&authid=0'] },
  { no: '109', name: '山东卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/sdwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=sdwshd', 'http://38.75.136.137:98/gslb/dsdqpub/sdwshd.m3u8?auth=testpub', 'http://198.204.228.26/live/sdwshd.m3u8', 'http://125.43.249.76:9901/tsfile/live/1023_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://120.76.248.139/live/bfgd/4200000099.m3u8'] },
  { no: '110', name: '湖北卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hbwshd', 'http://107.150.60.122/live/hbwshd.m3u8', 'http://120.76.248.139/live/bfgd/4200000102.m3u8'] },
  { no: '111', name: '河南卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/hawshd.m3u8?auth=testpub', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hawshd', 'http://107.150.60.122/live/hawshd.m3u8'] },
  { no: '112', name: '河北卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hewshd', 'http://38.75.136.137:98/gslb/dsdqpub/hewshd.m3u8?auth=testpub', 'https://event.pull.hebtv.com/live/live101.m3u8', 'http://120.76.248.139/live/bfgd/4200000108.m3u8'] },
  { no: '113', name: '天津卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/tjwshd.m3u8?auth=testpub', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=tjwshd', 'http://107.150.60.122/live/tjwshd.m3u8', 'http://222.169.85.8:9901/tsfile/live/0135_1.m3u8?key=txiptv&playlive=1&authid=0'] },
  { no: '114', name: '四川卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/scwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=scwshd', 'http://38.75.136.137:98/gslb/dsdqpub/scwshd.m3u8?auth=testpub'] },
  { no: '115', name: '重庆卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=cqwshd', 'http://107.150.60.122/live/cqwshd.m3u8'] },
  { no: '116', name: '辽宁卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=lnwshd', 'http://198.204.228.26/live/lnwshd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/lnwshd.m3u8?auth=testpub', 'http://107.150.60.122/live/lnwshd.m3u8', 'http://61.136.172.236:9901/tsfile/live/0121_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://204.12.221.218:8181/3m1080p/lnws.m3u8'] },
  { no: '117', name: '黑龙江卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=hljwshd', 'http://38.75.136.137:98/gslb/dsdqpub/hljwshd.m3u8?auth=testpub', 'http://107.150.60.122/live/hljwshd.m3u8', 'https://idclive.hljtv.com:4430/live/hljws_own.m3u8', 'http://120.76.248.139/live/bfgd/4200000095.m3u8'] },
  { no: '118', name: '吉林卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jlwshd', 'http://107.150.60.122/live/jlwshd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/jlwshd.m3u8?auth=testpub', 'http://221.7.175.154:8445/tsfile/live/1014_1.m3u8?key=txiptv&playlive=1&authid=0'] },
  { no: '119', name: '内蒙古卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/nmgws.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=nmgws', 'http://play1-qk.nmtv.cn/live/1769652018126032.m3u8'] },
  { no: '120', name: '山西卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/sxwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=sxwshd', 'http://204.12.221.218:8181/3m1080p/sxws.m3u8', 'http://119.39.9.8:9901/tsfile/live/0118_1.m3u8', 'http://m3u8.channel.wsrtv.com.cn/cms/videos/nmip-media/channellive/channel7/playlist.m3u8'] },
  { no: '121', name: '陕西卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/snwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=snwshd', 'http://192.151.150.154/live/snwshd.m3u8'] },
  { no: '122', name: '甘肃卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gswshd', 'http://38.75.136.137:98/gslb/dsdqpub/gswshd.m3u8?auth=testpub', 'http://120.76.248.139/live/bfgd/4200000119.m3u8', 'http://218.13.170.98:9901/tsfile/live/0141_1.m3u8'] },
  { no: '123', name: '青海卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/qhws.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=qhws', 'http://120.76.248.139/live/bfgd/4200000111.m3u8'] },
  { no: '124', name: '宁夏卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/nxws.m3u8', 'http://198.204.228.26/live/nxws.m3u8', 'https://epg.pw/stream/36d06a642456bafe7043aaed3e01dd7557dba0fb2072c40a91ae029700665b25.m3u8', 'https://hls.ningxiahuangheyun.com/live/nxws1M.m3u8'] },
  { no: '125', name: '新疆卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=xjws', 'http://38.75.136.137:98/gslb/dsdqpub/xjws.m3u8?auth=testpub', 'http://198.204.228.26/live/xjws.m3u8', 'http://107.150.60.122/live/xjws.m3u8'] },
  { no: '126', name: '西藏卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=xzws', 'http://107.150.60.122/live/xzws.m3u8', 'http://112.27.5.218:9901/tsfile/live/0111_1.m3u8?key=txiptv&playlive=1&authid=0', 'http://120.76.248.139/live/bfgd/4200000121.m3u8'] },
  { no: '127', name: '云南卫视', sub: '省级卫视', g: 'ws', urls: ['http://192.151.150.154/live/ynwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=ynwshd', 'http://107.150.60.122/live/ynwshd.m3u8', 'http://120.76.248.139/live/bfgd/4200000482.m3u8'] },
  { no: '128', name: '贵州卫视', sub: '省级卫视', g: 'ws', urls: ['http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gzwshd', 'http://204.12.221.218:8181/3m1080p/gzws.m3u8', 'http://107.150.60.122/live/gzwshd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/gzwshd.m3u8?auth=testpub'] },
  { no: '129', name: '广西卫视', sub: '省级卫视', g: 'ws', urls: ['http://38.75.136.137:98/gslb/dsdqpub/gxwshd.m3u8?auth=testpub', 'http://198.204.228.26/live/gxwshd.m3u8', 'http://107.150.60.122/live/gxwshd.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=gxwshd', 'http://120.76.248.139/live/bfgd/4200000116.m3u8'] },
  { no: '130', name: '海南卫视', sub: '省级卫视', g: 'ws', urls: ['http://120.76.248.139/live/bfgd/4200000473.m3u8'] },
  { no: '131', name: '江西卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/jxwshd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/jxwshd.m3u8?auth=testpub', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=jxwshd', 'http://112.27.5.218:9901/tsfile/live/0138_1.m3u8?key=txiptv&playlive=1&authid=0'] },
  { no: '132', name: '东南卫视', sub: '省级卫视', g: 'ws', urls: ['http://live.zohi.tv/video/s10001-fztv-3/index.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=dnwshd', 'http://107.150.60.122/live/dnwshd.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/dnwshd.m3u8?auth=testpub', 'http://120.76.248.139/live/bfgd/4200000483.m3u8'] },
  { no: '133', name: '三沙卫视', sub: '省级卫视', g: 'ws', urls: ['http://107.150.60.122/live/ssws.m3u8', 'http://63.141.230.178:82/gslb/zbdq5.m3u8?id=ssws', 'http://198.204.228.26/live/ssws.m3u8', 'http://38.75.136.137:98/gslb/dsdqpub/ssws.m3u8?auth=testpub'] },
];

var CH_KEY = 'tv_ch';          // 上次看的台
var LINE_KEY = 'tv_lines2';    // 每个台上次成功的线路（v2：线路表重排后作废旧记录）
var SRC_KEY = 'tv_src2';       // 静默更新下来的备用线路
var SRC_URL = 'http://ecfun.cc/api/v1/tv/channels';
var CONNECT_TIMEOUT = 6000;    // 起播超时：6 秒不出画面就换线
var REBUFFER_TIMEOUT = 3200;   // 播放中卡住超时：3 秒不动就换线
var COLUMNS = 4;

function indexOfNo(no) {
  for (var i = 0; i < CHANNELS.length; i++) {
    if (CHANNELS[i].no === no) return i;
  }
  return -1;
}

function buildRows(currentNo) {
  var rows = [];
  var row = [];
  for (var i = 0; i < CHANNELS.length; i++) {
    var ch = CHANNELS[i];
    row.push({ id: i, no: ch.no, name: ch.name, on: ch.no === currentNo, blank: false });
    if (row.length === COLUMNS) {
      rows.push(row);
      row = [];
    }
  }
  while (row.length > 0 && row.length < COLUMNS) {
    row.push({ id: -1, no: '', name: '', on: false, blank: true });
  }
  if (row.length) rows.push(row);
  return rows;
}

Page({
  data: {
    src: '',
    live: false,
    panel: false,
    rows: [],
    chno: '001',
    hud: false,
    off: false,
    cctvCount: 18,
    wsCount: 33
  },

  onLoad: function () {
    var self = this;
    this.token = 0;
    this.lineMap = {};
    this.curIndex = 0;
    this.cur = CHANNELS[0];
    this.line = 0;
    this.fs = false;
    this.lastFailAt = 0;
    this.timers = {};

    var info = null;
    try {
      info = wx.getSystemInfoSync ? wx.getSystemInfoSync() : null;
    } catch (e) {
      info = null;
    }
    this.keys = !!(info && info.keyEvents);

    this.loadLineMap();
    this.mergeCachedSources();

    var start = 0;
    try {
      var saved = wx.getStorageSync(CH_KEY);
      var hit = indexOfNo(saved);
      if (hit >= 0) start = hit;
    } catch (e) {
      start = 0;
    }
    this.curIndex = start;
    this.cur = CHANNELS[start];

    this.setData({
      chno: this.cur.no,
      cctvCount: 18,
      wsCount: CHANNELS.length - 18
    });

    // 打开就播：留给界面一帧时间，再上流
    this.timers.boot = setTimeout(function () {
      self.tune(self.cur, self.preferredLine(self.cur), true);
    }, 120);

    // 后台静默更新线路，不打扰当前播放（与换台定时器分开，避免被清掉）
    this.refreshTimer = setTimeout(function () {
      self.refreshSources();
    }, 5000);
  },

  onUnload: function () {
    this.clearTimers();
    if (this.refreshTimer) clearTimeout(this.refreshTimer);
    this.refreshTimer = null;
    try {
      this.ctx().stop();
    } catch (e) {
      // 页面销毁时播放器可能已经释放
    }
  },

  ctx: function () {
    // 每次取新上下文，频道表收起后视频节点会重建，缓存旧上下文会失效。
    return wx.createVideoContext('player');
  },

  clearTimers: function () {
    var t = this.timers || {};
    for (var k in t) {
      if (t[k]) clearTimeout(t[k]);
    }
    this.timers = {};
  },

  preferredLine: function (ch) {
    var saved = this.lineMap[ch.no];
    if (typeof saved === 'number' && saved >= 0 && saved < ch.urls.length) return saved;
    return 0;
  },

  loadLineMap: function () {
    try {
      var raw = wx.getStorageSync(LINE_KEY);
      if (raw) {
        var obj = JSON.parse(raw);
        if (obj && typeof obj === 'object') this.lineMap = obj;
      }
    } catch (e) {
      this.lineMap = {};
    }
  },

  saveLine: function (no, line) {
    this.lineMap[no] = line;
    try {
      wx.setStorageSync(LINE_KEY, JSON.stringify(this.lineMap));
    } catch (e) {
      // 存储失败不影响播放
    }
  },

  mergeCachedSources: function () {
    var self = this;
    try {
      var raw = wx.getStorageSync(SRC_KEY);
      if (!raw) return;
      var list = JSON.parse(raw);
      if (!list || !list.length) return;
      self.applySourceList(list);
    } catch (e) {
      // 缓存损坏时直接用内置线路
    }
  },

  applySourceList: function (list) {
    for (var i = 0; i < list.length; i++) {
      var item = list[i];
      if (!item || !item.name || !item.urls) continue;
      var idx = -1;
      for (var j = 0; j < CHANNELS.length; j++) {
        if (CHANNELS[j].name === item.name) {
          idx = j;
          break;
        }
      }
      if (idx < 0) continue;
      var ch = CHANNELS[idx];
      for (var k = 0; k < item.urls.length && ch.urls.length < 6; k++) {
        var u = item.urls[k];
        if (u && ch.urls.indexOf(u) < 0) ch.urls.push(u);
      }
    }
  },

  refreshSources: function () {
    var self = this;
    wx.request({
      url: SRC_URL,
      method: 'GET',
      success: function (res) {
        var body = res && res.data;
        if (typeof body === 'string') {
          try {
            body = JSON.parse(body);
          } catch (e) {
            return;
          }
        }
        var list = body && body.channels;
        if (!list || !list.length) return;
        self.applySourceList(list);
        try {
          wx.setStorageSync(SRC_KEY, JSON.stringify(list));
        } catch (e) {
          // 忽略缓存失败
        }
      }
    });
  },

  saveChannel: function (no) {
    try {
      wx.setStorageSync(CH_KEY, no);
    } catch (e) {
      // 忽略
    }
  },

  // 换台/换线路的统一入口。
  // 摘掉视频节点是为了让容器重新加载新地址（同一节点换 src 不会重新起播）；
  // 新地址先以隐藏状态挂上（off），序号继续留在黑底上，等画面真的出来再显示视频。
  // 连续按上下键时不断顺延，等手停下来才真正起播。
  tune: function (ch, line, initial) {
    if (!ch || !ch.urls.length) return;
    this.clearTimers();
    this.token++;
    var token = this.token;

    if (line >= ch.urls.length) line = 0;
    this.cur = ch;
    this.curIndex = indexOfNo(ch.no);
    this.line = line;
    this.zapUrl = ch.urls[line];

    if (!initial) {
      try {
        this.ctx().pause();
      } catch (e) {
        // 播放器尚未创建
      }
    }

    this.setData({
      src: '',
      off: false,
      chno: ch.no,
      hud: true,
      live: false
    });
    this.tunedAt = Date.now();
    this.saveChannel(ch.no);

    var self = this;
    var delay = initial ? 250 : 650;
    this.timers.start = setTimeout(function () {
      if (token !== self.token) return;
      self.playAt = Date.now();
      // 新节点先藏起来加载：mpv 加载期间是黑帧，露出来会把序号盖掉。
      self.setData({ src: self.zapUrl, off: true });
      self.timers.play = setTimeout(function () {
        if (token === self.token) self.play();
      }, 160);
    }, delay);

    this.timers.connect = setTimeout(function () {
      if (token === self.token && !self.data.live) self.nextLine('信号中断');
    }, CONNECT_TIMEOUT + delay);

    // 兜底：个别流不报进度时，也要把画面放出来，不能一直停在序号上。
    this.timers.reveal = setTimeout(function () {
      if (token === self.token) self.reveal();
    }, delay + 4000);
  },

  // 画面确实在走了，才把视频露出来、收掉序号。
  reveal: function () {
    if (!this.data.off) return;
    this.clearTimers();
    this.lastFailAt = 0;
    if (this.cur) this.saveLine(this.cur.no, this.line);
    this.setData({ off: false, hud: false, live: true });
  },

  onTimeUpdate: function (e) {
    if (!this.data.off) return;
    if (Date.now() - (this.playAt || 0) < 700) return;
    var detail = (e && e.detail) || {};
    if (detail.currentTime > 0.2) this.reveal();
  },

  onProgress: function () {
    if (!this.data.off) return;
    if (Date.now() - (this.playAt || 0) < 1500) return;
    this.reveal();
  },

  play: function () {
    try {
      this.ctx().play();
    } catch (e) {
      // 忽略
    }
  },

  nextLine: function (reason) {
    var now = Date.now();
    if (now - this.lastFailAt < 800) return;
    this.lastFailAt = now;

    var ch = this.cur;
    if (!ch) return;
    var next = this.line + 1;
    if (next < ch.urls.length) {
      this.tune(ch, next, false);
      return;
    }

    // 全部线路都不行：静默等几秒后自动再试
    this.clearTimers();
    this.setData({ live: false });
    var self = this;
    this.timers.retry = setTimeout(function () {
      self.tune(ch, 0, false);
    }, 4000);
  },

  step: function (delta) {
    var idx = this.curIndex + delta;
    if (idx < 0) idx = CHANNELS.length - 1;
    if (idx >= CHANNELS.length) idx = 0;
    var ch = CHANNELS[idx];
    this.tune(ch, this.preferredLine(ch), false);
  },

  onPlay: function () {
    // play 事件在换台请求一发出就会到，此时还没有画面，不能据此收起序号。
    this.lastFailAt = 0;
  },

  onPause: function () {
    // 换台时暂停不算错误，静默处理
  },

  onWaiting: function () {
    if (!this.data.live) return;
    var self = this;
    if (this.timers.buffer) clearTimeout(this.timers.buffer);
    this.timers.buffer = setTimeout(function () {
      if (self.data.live) self.nextLine('缓冲超时');
    }, REBUFFER_TIMEOUT);
  },

  onEnded: function () {
    if (Date.now() - (this.tunedAt || 0) < 350) return;
    this.nextLine('直播结束');
  },

  onVideoError: function () {
    if (Date.now() - (this.tunedAt || 0) < 350) return;
    this.nextLine('线路不可用');
  },

  onFsChange: function (e) {
    var detail = (e && e.detail) || {};
    this.fs = !!detail.fullScreen;
  },

  onKeyDown: function (e) {
    if (!this.keys) return false;
    var key = e && e.key;
    // 页面本身已经铺满整个屏幕，不进入播放器全屏：
    // 否则播放器画面会盖住换台序号。
    if (key === 'y') return true;
    if (this.data.panel) {
      if (key === 'b') {
        this.closePanel();
        return true;
      }
      return false;
    }
    if (key === 'up') {
      this.step(-1);
      return true;
    }
    if (key === 'down') {
      this.step(1);
      return true;
    }
    if (key === 'a') {
      this.openPanel();
      return true;
    }
    return false;
  },

  openPanel: function () {
    this.clearTimers();
    if (this.fs) {
      try {
        this.ctx().exitFullScreen();
      } catch (e) {
        // 忽略
      }
    }
    try {
      this.ctx().pause();
    } catch (e) {
      // 忽略
    }
    this.setData({
      panel: true,
      hud: false,
      rows: buildRows(this.cur ? this.cur.no : '001')
    });
  },

  closePanel: function () {
    var self = this;
    this.setData({ panel: false, live: false });
    this.timers.resume = setTimeout(function () {
      self.reopen();
    }, 180);
  },

  reopen: function () {
    var ch = this.cur;
    if (!ch) return;
    var url = ch.urls[this.line] || ch.urls[0];
    var self = this;
    this.setData({ off: false, hud: false, live: true, src: url });
    this.token++;
    this.timers.play = setTimeout(function () {
      self.play();
    }, 160);
    this.timers.connect = setTimeout(function () {
      if (!self.data.live) self.nextLine('信号中断');
    }, CONNECT_TIMEOUT);
  },

  onClose: function () {
    this.closePanel();
  },

  onPick: function (e) {
    var id = e && e.currentTarget && e.currentTarget.dataset ? e.currentTarget.dataset.id : -1;
    var idx = parseInt(id, 10);
    if (isNaN(idx) || idx < 0 || idx >= CHANNELS.length) return;
    var ch = CHANNELS[idx];
    this.setData({ panel: false });
    this.tune(ch, this.preferredLine(ch), false);
  }
});
