# チリツモの 9 画面の HTML を書き出す。共通の頭を 1 か所にまとめるための生成スクリプト
import pathlib
here=pathlib.Path(__file__).resolve().parent
HEAD='''<!doctype html><meta charset="utf-8">
<script type="importmap">{"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js","three/addons/":"https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/","cannon-es":"https://cdn.jsdelivr.net/npm/cannon-es@0.20.0/dist/cannon-es.js"}}</script>
<link href="https://fonts.googleapis.com/css2?family=Zen+Kaku+Gothic+New:wght@700;900&family=Anton&display=block" rel="stylesheet">
<style>html,body{margin:0;width:1920px;height:1080px;overflow:hidden;font-family:"Zen Kaku Gothic New";font-weight:900}canvas{position:absolute;left:0;top:0;width:1920px;height:1080px}
#e{position:absolute;left:20px;top:20px;color:#fff;background:#000;font:18px monospace;z-index:9;white-space:pre-wrap}
.abs{position:absolute}.num{font-family:Anton,"Zen Kaku Gothic New";font-weight:400;letter-spacing:.01em}
.keys{display:inline-flex;gap:4px;align-items:center;background:#111;padding:5px 8px;border-radius:6px}.keys img{height:40px;display:block}
.bar{position:absolute;display:flex;gap:40px;align-items:center;font-size:27px}.bar span{display:flex;gap:10px;align-items:center}
%s</style><body>
%s
<div id="e"></div>
<script type="module">
const E=document.getElementById('e');addEventListener('error',e=>E.textContent+=e.message+'\\n');addEventListener('unhandledrejection',e=>E.textContent+=String(e.reason&&e.reason.stack||e.reason)+'\\n');
import {THREE,W,H,stage,ground,drop,model,place,slime,eyesAt,slab,toy,rng,gradBg,flatCam,logoSVG,ready} from './scene3d.js';
%s
ready();
</script>'''
def keys(*n): return '<i class="keys">'+''.join(f'<img src="../scenes/assets/{x}.png">' for x in n)+'</i>'
ENTER=keys('keyboard_enter','xbox_button_a');ARW=keys('keyboard_arrows_horizontal','xbox_dpad');ESC=keys('keyboard_escape','xbox_button_b')
KN=keys('keyboard_n','xbox_button_a');KR=keys('keyboard_r','xbox_button_x');KT=keys('keyboard_tab','xbox_button_y')
CITY="[['covered_car',4.2,3],['concrete_road_barrier',2.8,4],['horse_statue_01',2.8,1],['Barrel_01',1.3,12],['wooden_crate_01',1.5,10],['trashbag',1.2,20],['cardboard_box_01',1.05,20],['old_tyre',1.1,12],['fire_hydrant',.95,6],['concrete_cat_statue',1.1,3],['plastic_crate_01',.8,16],['metal_jerrycan',.7,14],['metal_trash_can',1,8]]"
TOYS="[[toy('box',0xd9423a),1.1,14],[toy('box',0x2a79bf),1.1,14],[toy('box',0xe9b022),1.1,14],[toy('ball',0x2fa58a),.9,14],[toy('ball',0xd94e87),.8,12],['cardboard_box_01',1.2,5],['plastic_crate_02',1,5]]"
P={}
# ---- 山 ----
P['p1_title_pile']=('''body{background:#d8452e;color:#fff}
.menu{left:72px;top:610px;display:grid;gap:4px}.m{font-size:44px;display:flex;align-items:center;gap:16px}.m.on{font-size:84px}.m.on:before{content:"";width:40px;height:16px;background:#fff}''',
f'''<canvas id="c"></canvas><div class="abs menu"><div class="m on">はじめる</div><div class="m">クレジット</div><div class="m">やめる</div></div>
<div class="bar" style="left:72px;bottom:36px"><span>{ENTER}けってい</span></div><div class="abs" style="left:560px;bottom:44px;font-size:19px;font-weight:700">音楽 Kevin MacLeod (incompetech.com) CC BY 4.0</div>''',
f'''const BG=0xd8452e,s=stage(document.getElementById('c'),{{bg:BG,fov:24,sun:2.8,sunPos:[-8,16,10],env:.75,exposure:1.05,shadowArea:24}});ground(s.sc,BG);
const X=[-9.8,-3.3,3.2,9.7];[['チ',-.03],['リ',.02],['ツ',-.02],['モ',.05]].forEach(([c,z],i)=>slab(s.sc,c,{{size:6.3,pos:[X[i],9.4,-1],rot:[0,0,z],depth:1.2,side:0xa82c1a}}));
const peak=await drop(s.sc,{CITY}.map(([n,a,c])=>[n,a,Math.ceil(c*1.7)]),{{radius:2.2,tube:3.5,base:[7,0,3],seed:11,every:3}});
s.cam.position.set(0,6,36);s.cam.lookAt(0,6,0);
const R=1.75,sl=slime(R,{{seed:6,squash:.84,lump:.06}});sl.position.set(6.2,7.7,3);s.sc.add(sl);eyesAt(s,sl,R);await s.draw();''')
P['p2_select_pile']=('''body{background:#222;color:#111}#a{clip-path:polygon(0 0,1330px 0,1130px 100%,0 100%)}#b{clip-path:polygon(1330px 0,100% 0,100% 100%,1130px 100%)}
.big{font-size:128px;line-height:1.05}.row{display:flex;gap:44px;margin-top:10px}.k{font-size:26px}.v{font-size:66px;line-height:1.1}''',
f'''<canvas id="a"></canvas><canvas id="b"></canvas>
<div class="abs" style="left:72px;top:54px"><div class="num" style="font-size:44px">STAGE 1</div><div class="big">子ども部屋</div>
<div class="row"><div><div class="k">目標</div><div class="v num">15cm</div></div><div><div class="k">最高記録</div><div class="v num">28.4cm</div></div><div><div class="k">ランク</div><div class="v num">A</div></div><div><div class="k">時間</div><div class="v num">3:00</div></div></div></div>
<div class="abs" style="left:1400px;top:54px;color:#fff"><div class="num" style="font-size:36px">STAGE 2</div><div style="font-size:84px;line-height:1.1">輪の町</div><div style="font-size:30px;margin-top:6px">子ども部屋を<br>15cm でクリア</div></div>
<div class="bar" style="left:72px;bottom:30px"><span>{ENTER}出発</span><span>{ARW}えらぶ</span></div><div class="bar" style="right:60px;bottom:30px;color:#fff"><span>{ESC}もどる</span></div>''',
f'''const a=stage(document.getElementById('a'),{{bg:0xf1ae1d,fov:24,sun:2.8,sunPos:[-8,16,10],env:.8,exposure:1.05,shadowArea:20}});ground(a.sc,0);
const pk=await drop(a.sc,{TOYS},{{radius:1.3,tube:2.3,base:[0,0,0],seed:4,every:4}});a.cam.position.set(.8,5.6,27);a.cam.lookAt(.8,5.4,0);
const R=1.3,sl=slime(R,{{seed:2,squash:.84,lump:.05}});sl.position.set(0,pk+.8,.3);a.sc.add(sl);eyesAt(a,sl,R);await a.draw();
const b=stage(document.getElementById('b'),{{bg:0x2447c9,fov:24,sun:1.2,sunPos:[-6,16,10],env:.1,exposure:.7,shadowArea:20}});ground(b.sc,0);
const dark=new THREE.MeshBasicMaterial({{color:0x0d1c5e}});await drop(b.sc,{CITY},{{radius:1.4,tube:2.8,base:[0,0,0],seed:5,every:4}});b.sc.traverse(o=>{{if(o.isMesh&&o.material.type!=='ShadowMaterial')o.material=dark}});
b.cam.position.set(-10,5.5,31);b.cam.lookAt(-10,5.5,0);await b.draw();''')
P['p3_result_pile']=('''body{background:#2447c9;color:#fff}
.lab{font-size:30px}.n{font-size:236px;line-height:1}.tag{position:absolute;background:#fff;color:#111;font-size:30px;padding:4px 14px;white-space:nowrap}.ln{position:absolute;height:4px;background:#fff;transform-origin:0 50%}
.rank{left:560px;top:430px;width:210px;height:210px;background:#ffd21f;color:#111;display:grid;place-items:center;font-size:170px;line-height:1}
.btn{display:flex;align-items:center;gap:16px;font-size:38px;background:#fff;color:#111;padding:10px 26px 10px 12px}.btn.on{background:#ffd21f}''',
f'''<canvas id="c"></canvas>
<div class="abs" style="left:72px;top:46px"><div class="lab">輪の町</div><div class="num n">126.31<span style="font-size:110px">m</span></div></div>
<div class="abs" style="left:72px;top:352px;font-size:52px;line-height:1.3">722個、積み上げた。</div>
<div class="abs" style="left:72px;top:450px;display:grid;gap:4px;font-size:34px"><div>町の <span class="num" style="font-size:60px">30%</span></div><div>のこり <span class="num" style="font-size:60px">0:45</span></div><div style="background:#ffd21f;color:#111;padding:0 12px;width:max-content;font-size:30px">最高記録 更新</div></div>
<div class="abs rank num">S</div>
<div class="abs" style="left:72px;bottom:34px;display:flex;gap:14px"><div class="btn on">{KN}次のステージ</div><div class="btn">{KR}もう一度</div><div class="btn">{KT}タイトル</div></div>
<i class="ln" style="left:1060px;top:662px;width:210px;transform:rotate(10deg)"></i><div class="tag" style="left:850px;top:640px">ドラム缶 ×48</div>
<i class="ln" style="left:1740px;top:650px;width:100px;transform:rotate(140deg)"></i><div class="tag" style="left:1730px;top:610px">ゴミ袋 ×210</div>
<i class="ln" style="left:1130px;top:760px;width:110px;transform:rotate(-12deg)"></i><div class="tag" style="left:950px;top:742px">タイヤ ×64</div>''',
f'''const BG=0x2447c9,s=stage(document.getElementById('c'),{{bg:BG,fov:24,sun:3,sunPos:[-8,18,12],env:.8,exposure:1.05,shadowArea:26}});ground(s.sc,BG,300,.5);
const pk=await drop(s.sc,{CITY}.map(([n,a,c])=>[n,a,Math.ceil(c*2.3)]),{{radius:2.4,tube:4.1,base:[6.6,0,0],seed:21,every:3}});
s.cam.position.set(0,6.5,38);s.cam.lookAt(0,6.5,0);const R=2,sl=slime(R,{{seed:9,squash:.86,lump:.06}});sl.position.set(6.4,6.5,0);s.sc.add(sl);eyesAt(s,sl,R);await s.draw();''')
# ---- 腹の中 ----
FLOAT='''async function float(sc,list,seed,area){const r=rng(seed);for(const [n,sz,c] of list){const t=typeof n==='string'?await model(n):n;for(let i=0;i<c;i++){const px=(r()-.5)*area[0],py=(r()-.5)*area[1],pz=-r()*area[2]+area[3],m=place(t,sz*(.8+r()*.4),[px,py,pz],[r()*6.3,r()*6.3,r()*6.3]);if(px/(12-pz)<-.22&&py/(12-pz)<-.02)continue;sc.add(m)}}}
function bubbles(sc,seed,n,area){const r=rng(seed),m=new THREE.MeshPhysicalMaterial({color:0xffffff,transmission:.9,roughness:.05,thickness:.2,ior:1.2,envMapIntensity:1.6,transparent:true,opacity:.75});for(let i=0;i<n;i++){const b=new THREE.Mesh(new THREE.SphereGeometry(.04+r()*r()*.34,20,14),m);b.position.set((r()-.5)*area[0],(r()-.5)*area[1],-r()*area[2]+area[3]);sc.add(b)}}
function rays(){const c=document.getElementById('ray'),x=c.getContext('2d');c.width=W;c.height=H;x.globalCompositeOperation='lighter';const r=rng(5);for(let i=0;i<9;i++){const X=r()*W*1.2-100,w=60+r()*170;const g=x.createLinearGradient(0,0,0,H*.9);g.addColorStop(0,'rgba(240,255,190,.34)');g.addColorStop(1,'rgba(240,255,190,0)');x.fillStyle=g;x.beginPath();x.moveTo(X,0);x.lineTo(X+w,0);x.lineTo(X+w-260,H);x.lineTo(X-260,H);x.closePath();x.fill()}}
'''
P['g1_title_gut']=('''body{background:#14724a;color:#fff}#ray{mix-blend-mode:screen;filter:blur(14px)}
.menu{left:76px;top:640px;display:grid;gap:4px;text-shadow:0 3px 0 #064028}.m{font-size:44px}.m.on{font-size:84px;color:#eaff7a}''',
f'''<canvas id="c"></canvas><canvas id="ray"></canvas><div class="abs menu"><div class="m on">はじめる</div><div class="m">クレジット</div><div class="m">やめる</div></div>
<div class="bar" style="left:76px;bottom:36px"><span>{ENTER}けってい</span></div><div class="abs" style="right:50px;bottom:44px;font-size:19px;font-weight:700">音楽 Kevin MacLeod (incompetech.com) CC BY 4.0</div>''',
FLOAT+f'''const s=stage(document.getElementById('c'),{{bg:0x14724a,fov:38,sun:2.2,sunPos:[2,20,6],env:.9,exposure:1.05,shadow:false}});gradBg(s.sc,['#d7f76c','#6fd14a','#14724a','#0b4a35']);s.sc.fog=new THREE.Fog(0x3fae4e,12,58);
const X=[-7.2,-2.4,2.4,7.2];['チ','リ','ツ','モ'].forEach((c,i)=>slab(s.sc,c,{{size:4.6,pos:[X[i],2.6,-4],rot:[.04*(i%2?1:-1),.12-.08*i,(i%2?.05:-.06)],depth:.5,side:0x2f9a4a,layers:14}}));s.sc.traverse(o=>{{if(o.isMesh)o.material.fog=false}});
await float(s.sc,[['covered_car',3.4,2],['horse_statue_01',3,1],['Barrel_01',1.3,7],['wooden_crate_01',1.4,6],['trashbag',1.2,8],['cardboard_box_01',1.1,10],['old_tyre',1.2,7],['fire_hydrant',1,4],['concrete_cat_statue',1.1,3],['plastic_crate_01',.8,8],['metal_jerrycan',.7,7],['street_rat',.5,6]],8,[40,20,26,-2]);
await float(s.sc,[['old_tyre',1.9,1],['wooden_crate_01',1.7,1],['Barrel_01',1.8,1]],15,[2,2,.1,3.5]);
s.sc.children.slice(-3).forEach((m,i)=>m.position.set([4.6,1.4,5.6][i],[-2.5,-3.6,-.9][i],[3.6,2.2,1.8][i]));
bubbles(s.sc,3,110,[34,20,24,6]);s.cam.position.set(0,0,12);s.cam.lookAt(0,0,0);await s.draw();rays();''')
P['g2_select_gut']=('''body{background:#14724a;color:#fff}#ray{mix-blend-mode:screen;filter:blur(14px)}.big{font-size:104px;line-height:1.1;text-shadow:0 4px 0 #064028}.k{font-size:25px}.v{font-size:60px;line-height:1.1}''',
f'''<canvas id="c"></canvas><canvas id="ray"></canvas>
<div class="abs" style="left:76px;top:690px;text-shadow:0 3px 0 #064028"><div class="big">子ども部屋</div><div style="display:flex;gap:42px"><div><div class="k">目標</div><div class="v num">15cm</div></div><div><div class="k">最高記録</div><div class="v num" style="color:#eaff7a">28.4cm</div></div><div><div class="k">ランク</div><div class="v num">A</div></div><div><div class="k">時間</div><div class="v num">3:00</div></div></div></div>
<div class="abs" style="left:1290px;top:790px;text-shadow:0 3px 0 #064028"><div style="font-size:52px">輪の町</div><div style="font-size:27px">子ども部屋を 15cm でクリア</div></div>
<div class="bar" style="left:76px;bottom:26px;color:#fff"><span>{ENTER}出発</span><span>{ARW}えらぶ</span></div><div class="bar" style="right:60px;bottom:26px;color:#fff"><span>{ESC}もどる</span></div>''',
FLOAT+f'''const s=stage(document.getElementById('c'),{{bg:0x14724a,fov:34,sun:2.4,sunPos:[-3,20,10],env:1,exposure:1.05,shadow:false}});gradBg(s.sc,['#d7f76c','#6fd14a','#14724a','#0b4a35']);s.sc.fog=new THREE.Fog(0x3fae4e,14,44);
const bub=(R,c)=>{{const m=new THREE.Mesh(new THREE.SphereGeometry(R,64,48),new THREE.MeshPhysicalMaterial({{color:c,transmission:.97,roughness:.03,thickness:.25,ior:1.15,clearcoat:1,envMapIntensity:1.5}}));return m}};
const A=[-4.2,2.3,0],B=[6.8,1.2,-3];const ba=bub(5,0xffffff);ba.position.set(...A);s.sc.add(ba);
const toys=[[toy('box',0xd9423a),.95,6],[toy('box',0x2a79bf),.95,6],[toy('box',0xe9b022),.95,6],[toy('ball',0x2fa58a),.8,6],[toy('ball',0xd94e87),.7,6],['cardboard_box_01',1,3]];
{{const r=rng(4);for(const [n,z,c] of toys){{const t=typeof n==='string'?await model(n):n;for(let i=0;i<c;i++){{const u=r()*6.28,v=Math.acos(2*r()-1),d=3.4*Math.cbrt(r());s.sc.add(place(t,z,[A[0]+Math.sin(v)*Math.cos(u)*d,A[1]+Math.cos(v)*d*.8-.4,A[2]+Math.sin(v)*Math.sin(u)*d],[r()*6,r()*6,r()*6]))}}}}}}
const bb=bub(3.5,0xcfe8d0);bb.material.roughness=.5;bb.position.set(...B);s.sc.add(bb);
{{const r=rng(9);for(const [n,z,c] of [['covered_car',2,1],['Barrel_01',.9,4],['trashbag',.9,4],['old_tyre',.9,3]]){{const t=await model(n);for(let i=0;i<c;i++)s.sc.add(place(t,z,[B[0]+(r()-.5)*3.2,B[1]+(r()-.5)*3,B[2]+(r()-.5)*3],[r()*6,r()*6,r()*6]))}}}}
await float(s.sc,[['cardboard_box_01',1,6],['old_tyre',1.1,5],['metal_jerrycan',.7,6],['plastic_crate_01',.8,6]],22,[44,22,26,-8]);bubbles(s.sc,6,90,[34,20,24,6]);
s.cam.position.set(0,0,16);s.cam.lookAt(0,0,0);await s.draw();rays();''')
P['g3_result_gut']=('''body{background:#e8f6c9;color:#0b3a26}.n{font-size:220px;line-height:1}.rank{left:1660px;top:56px;width:190px;height:190px;background:#0b3a26;color:#eaff7a;display:grid;place-items:center;font-size:150px;line-height:1}
.st{font-size:34px;line-height:1.25}.btn{display:flex;align-items:center;gap:16px;font-size:36px;background:#0b3a26;color:#fff;padding:9px 24px 9px 10px}.btn.on{background:#d8452e}''',
f'''<canvas id="c"></canvas>
<div class="abs" style="left:72px;top:40px"><div style="font-size:30px">輪の町</div><div class="num n">126.31<span style="font-size:100px">m</span></div></div>
<div class="abs st" style="left:1160px;top:74px;text-align:right;width:470px"><div>飲んだ物 <span class="num" style="font-size:66px">722</span> 個</div><div>町の <span class="num" style="font-size:66px">30</span> %</div><div>のこり <span class="num" style="font-size:66px">0:45</span></div></div>
<div class="abs rank num">S</div>
<div class="abs" style="left:72px;bottom:30px;display:flex;gap:14px"><div class="btn on">{KN}次のステージ</div><div class="btn">{KR}もう一度</div><div class="btn">{KT}タイトル</div></div>''',
f'''const s=stage(document.getElementById('c'),{{bg:0xe8f6c9,fov:26,sun:2.6,sunPos:[-6,18,14],env:1,exposure:1.05,shadowArea:26}});ground(s.sc,0,300,.3);
s.cam.position.set(0,5.6,40);s.cam.lookAt(0,5.6,0);
const r=rng(31);const list={CITY};for(const [n,z,c] of list){{const t=await model(n);for(let i=0;i<c*2;i++){{const u=r()*6.28,v=Math.acos(r()),d=9.6*Math.cbrt(r());s.sc.add(place(t,z*.85,[Math.sin(v)*Math.cos(u)*d*1.42,Math.cos(v)*d*.72+.4,Math.sin(v)*Math.sin(u)*d],[r()*6,r()*6,r()*6]))}}}}
const R=10,sl=slime(R,{{seed:12,squash:.8,lump:.04,flat:.02,trans:.95,thick:.4}});sl.material.attenuationDistance=R*5;sl.material.emissiveIntensity=.03;sl.material.roughness=.05;sl.material.clearcoat=0;sl.material.roughness=.22;sl.material.envMapIntensity=.2;sl.scale.x=R*1.5;sl.position.set(0,0,0);s.sc.add(sl);eyesAt(s,sl,R,{{gap:.17,up:.42,size:.028,push:.06}});await s.draw();''')
# ---- 物差し ----
TAPE='''function tape(id,ticks,y,h){const c=document.getElementById(id),x=c.getContext('2d');c.width=W;c.height=H;x.fillStyle='#f5c400';x.fillRect(0,y,W,H-y);x.fillStyle='#111';x.fillRect(0,y,W,3);const r=ticks;for(let i=0;i<=W;i+=12){const big=i%120===0,mid=i%60===0;x.fillRect(i,y,big?4:2,big?h:mid?h*.7:h*.42)}x.font='400 40px Anton';x.textBaseline='top';for(const [px,t,red] of r){x.fillStyle=red?'#c7241e':'#111';x.fillText(t,px+8,y+h*.72)}}
'''
P['s1_title_scale']=('''body{background:#f5c400;color:#111}.menu{left:72px;top:64px;display:grid;gap:2px}.m{font-size:42px}.m.on{font-size:84px;display:flex;gap:14px;align-items:center}.m.on:before{content:"";width:0;height:0;border-left:34px solid #c7241e;border-top:22px solid transparent;border-bottom:22px solid transparent}
.lg{position:absolute;bottom:560px;color:#111}''',
f'''<canvas id="c"></canvas><canvas id="t"></canvas><div class="abs menu"><div class="m on">はじめる</div><div class="m">クレジット</div><div class="m">やめる</div></div>
<div id="logo"></div>
<div class="bar" style="left:72px;top:330px"><span>{ENTER}けってい</span></div><div class="abs" style="right:48px;top:30px;font-size:19px;font-weight:700">音楽 Kevin MacLeod (incompetech.com) CC BY 4.0</div>''',
TAPE+f'''await document.fonts.load('400 40px Anton','0');
let lx=640;[['チ',120],['リ',190],['ツ',290],['モ',440]].forEach(([c,z])=>{{const d=document.createElement('div');d.className='lg';d.style.cssText=`left:${{lx}}px;width:${{z}}px;height:${{z}}px`;d.innerHTML=logoSVG(c,20,'#111');document.getElementById('logo').append(d);lx+=z*1.04}});
const s=stage(document.getElementById('c'),{{bg:0xf5c400,sun:2.6,sunPos:[-5,9,12],env:.9,exposure:1.05,shadowArea:14}});flatCam(s);
const line=[['street_rat',.5,1.1],['metal_jerrycan',.75,1.8],['old_tyre',1.1,2.7],['Barrel_01',1.5,4.2],['fire_hydrant',1.5,5.8],['cardboard_box_01',1.5,7.4],['concrete_cat_statue',2.2,9.4],['covered_car',4.6,13],['horse_statue_01',4.4,17]];
for(const [n,z,x] of line){{const t=await model(n);const m=place(t,z,[x,0,0],[0,.5,0]);const h=t.userData.size.y*z;m.position.y=1.5+h*.5;s.sc.add(m)}}
const R=.34,sl=slime(R,{{seed:4,squash:.8,lump:.05,flat:.5}});sl.position.set(.45,1.5+R*.42,0);s.sc.add(sl);eyesAt(s,sl,R);await s.draw();
tape('t',[[0,'5cm',1],[384,'30cm',0],[643,'1m',0],[988,'5m',0],[1482,'50m',0],[1680,'126m',1]],930,120);''')
P['s2_select_scale']=('''body{background:#f5c400;color:#111}.big{font-size:128px;line-height:1.05}.k{font-size:26px}.v{font-size:68px;line-height:1.1}
.rng{position:absolute;height:64px;display:flex;align-items:center;padding:0 18px;font-size:32px;box-sizing:border-box}''',
f'''<canvas id="c"></canvas><canvas id="t"></canvas>
<div class="abs" style="left:72px;top:50px"><div class="num" style="font-size:44px">STAGE 1</div><div class="big">子ども部屋</div>
<div style="display:flex;gap:44px;margin-top:8px"><div><div class="k">目標</div><div class="v num">15cm</div></div><div><div class="k">最高記録</div><div class="v num" style="color:#c7241e">28.4cm</div></div><div><div class="k">ランク</div><div class="v num">A</div></div><div><div class="k">時間</div><div class="v num">3:00</div></div></div></div>
<div class="rng" style="left:0;top:846px;width:384px;background:#111;color:#f5c400">1 子ども部屋</div>
<div class="rng" style="left:643px;top:846px;width:1037px;background:rgba(17,17,17,.14);color:#111">2 輪の町　子ども部屋を 15cm でクリア</div>
<div class="bar" style="left:72px;bottom:18px;display:none"></div>
<div class="bar" style="right:60px;top:60px"><span>{ENTER}出発</span><span>{ARW}えらぶ</span><span>{ESC}もどる</span></div>''',
TAPE+f'''await document.fonts.load('400 40px Anton','0');
const s=stage(document.getElementById('c'),{{bg:0xf5c400,sun:2.6,sunPos:[-5,9,12],env:.9,exposure:1.05,shadowArea:14}});flatCam(s);
const put=async(n,z,x,ry=.5,c)=>{{const t=typeof n==='string'?await model(n):n;const m=place(t,z,[x,0,0],[0,ry,0]);m.position.y=2.34+(t.userData.size.y*z)*.5;s.sc.add(m);return m}};
await put(toy('box',0xd9423a),.8,.6);await put(toy('ball',0x2fa58a),.7,1.45);await put('cardboard_box_01',.9,2.35);
const dk=new THREE.MeshBasicMaterial({{color:0xb88f00}});const g=[await put('covered_car',3,8.2),await put('Barrel_01',1.4,10),await put('horse_statue_01',3.4,12.8),await put('concrete_road_barrier',3,16)];g.forEach(m=>m.traverse(o=>{{if(o.isMesh)o.material=dk}}));
const R=.62,sl=slime(R,{{seed:4,squash:.8,lump:.05,flat:.5}});sl.position.set(3.73,2.34+R*.42,0);s.sc.add(sl);eyesAt(s,sl,R);await s.draw();
tape('t',[[0,'5cm',0],[236,'15cm',1],[384,'30cm',0],[643,'1m',0],[988,'5m',0],[1482,'50m',1],[1680,'126m',0]],930,120);''')
P['s3_result_scale']=('''body{background:#f5c400;color:#111}.n{font-size:250px;line-height:1}.rank{left:640px;top:360px;width:220px;height:220px;border-radius:50%;background:#c7241e;color:#fff;display:grid;place-items:center;font-size:150px;line-height:1}
.st{font-size:40px;line-height:1.3}.btn{display:flex;align-items:center;gap:16px;font-size:36px;background:#111;color:#fff;padding:9px 24px 9px 10px}.btn.on{background:#c7241e}''',
f'''<canvas id="c"></canvas><canvas id="t"></canvas>
<div class="abs" style="left:72px;top:40px"><div style="font-size:30px">輪の町</div><div class="num n">126.31<span style="font-size:110px">m</span></div></div>
<div class="abs st" style="left:72px;top:356px"><div>車 <span class="num" style="font-size:84px">28</span> 台ぶん</div><div>飲んだ物 <span class="num" style="font-size:64px">722</span> 個</div><div>町の <span class="num" style="font-size:64px">30</span> %</div><div>のこり <span class="num" style="font-size:64px">0:45</span></div></div>
<div class="abs rank num">S</div><div class="abs" style="left:610px;top:596px;background:#111;color:#f5c400;font-size:30px;padding:2px 14px">最高記録 更新</div><div class="abs" style="left:560px;top:880px;font-size:28px;background:#111;color:#f5c400;padding:0 12px">車 1台</div><i class="abs" style="left:666px;top:918px;width:4px;height:22px;background:#111;transform:rotate(-20deg)"></i>
<div class="abs" style="left:72px;bottom:30px;display:flex;gap:14px"><div class="btn on">{KN}次のステージ</div><div class="btn">{KR}もう一度</div><div class="btn">{KT}タイトル</div></div>''',
f'''await document.fonts.load('400 40px Anton','0');
const s=stage(document.getElementById('c'),{{bg:0xf5c400,sun:2.6,sunPos:[-5,9,12],env:.9,exposure:1.05,shadowArea:14}});flatCam(s);
// 足元に同じ縮尺の車を 1 台置く。体の高さは車 28 台ぶん
const R=4.5,sl=slime(R,{{seed:7,squash:1.02,lump:.045,flat:.92}});sl.scale.x=R*1.12;sl.scale.y=R*1.06;sl.position.set(12.6,1.3+R*.98,0);s.sc.add(sl);eyesAt(s,sl,R,{{gap:.3,up:.2,size:.06}});
const car=await model('covered_car');const m=place(car,.34,[6.75,0,1],[0,.25,0]);m.position.y=1.3+car.userData.size.y*.17;s.sc.add(m);await s.draw();
const c=document.getElementById('t'),x=c.getContext('2d');c.width=W;c.height=H;x.fillStyle='#f5c400';x.fillRect(0,950,W,130);x.fillStyle='#111';x.fillRect(0,950,W,3);for(let i=0;i<W;i+=12){{x.fillRect(i,950,i%120?2:4,i%120?34:70)}}x.fillStyle='#c7241e';x.fillRect(1830,52,8,898);x.fillRect(1800,52,68,8);x.fillRect(1800,942,68,8);''')
for k,(css,body,js) in P.items():
    (here/(k+'.html')).write_text(HEAD%(css,body,js),encoding='utf-8')
print(len(P))
