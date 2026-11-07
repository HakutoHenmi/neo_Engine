// チリツモの参考画像: ゲームの実モデル (PolyHaven FBX) を three.js で並べて描く共通処理
import * as THREE from 'three';
import {FBXLoader} from 'three/addons/loaders/FBXLoader.js';
import {RoomEnvironment} from 'three/addons/environments/RoomEnvironment.js';
import * as CANNON from 'cannon-es';
export {THREE};
export const W=1920,H=1080;
const BASE='/Resources/Models/PolyHaven/';
const manager=new THREE.LoadingManager();
manager.setURLModifier(u=>{if(u.startsWith('blob:')||u.startsWith('data:')||u.endsWith('.fbx'))return u;const n=u.split(/[\\/]/).pop();return BASE+'textures/'+n});
const loader=new FBXLoader(manager);const cache={};
export function rng(seed){let s=seed>>>0;return()=>{s=(s*1664525+1013904223)>>>0;return s/4294967296}}
// モデルを読み、最大の辺を 1 にそろえて原点へ置いた雛形を返す
const texIndex=await (await fetch('texindex.json')).json();const texLoader=new THREE.TextureLoader();const texCache={};const pending=[];
function findTex(model,mat){const key=(mat||'').toLowerCase().replace(/[^a-z0-9_]/g,''),base=model.toLowerCase();const all=[...texIndex.m.map(n=>[n,'/Resources/Models/PolyHaven/textures/']),...texIndex.t.map(n=>[n,'/Resources/Textures/PolyHaven/'])];
  const hit=all.find(([n])=>key&&n.toLowerCase().startsWith(key+'_diff'))||all.find(([n])=>key&&n.toLowerCase().includes(key))||all.find(([n])=>n.toLowerCase().startsWith(base+'_diff'))||all.find(([n])=>n.toLowerCase().startsWith(base));
  if(!hit)return null;const u=hit[1]+hit[0];if(!texCache[u]){pending.push(new Promise(ok=>{texCache[u]=texLoader.load(u,ok,undefined,ok)}));texCache[u].colorSpace=THREE.SRGBColorSpace;texCache[u].anisotropy=4}return texCache[u]}
// モデルを読み、最大の辺を 1 にそろえて原点へ置いた雛形を返す
export async function model(name){if(cache[name])return cache[name];
  const o=await loader.loadAsync(BASE+name+'_2k.fbx');
  o.traverse(c=>{if(c.isMesh){c.castShadow=c.receiveShadow=true;const ms=Array.isArray(c.material)?c.material:[c.material];const out=ms.map(m=>{const map=findTex(name,m.name)||m.map;if(map)map.colorSpace=THREE.SRGBColorSpace;return new THREE.MeshStandardMaterial({map:map||null,roughness:.7,metalness:0,color:map?0xffffff:0xb0b0b0})});c.material=out.length===1?out[0]:out}
    if(c.isLight||c.isCamera)c.visible=false});
  const b=new THREE.Box3().setFromObject(o),s=b.getSize(new THREE.Vector3()),m=Math.max(s.x,s.y,s.z),ct=b.getCenter(new THREE.Vector3());
  const g=new THREE.Group();o.position.sub(ct);g.add(o);g.scale.setScalar(1/m);const w=new THREE.Group();w.add(g);w.userData.size=s.clone().divideScalar(m);cache[name]=w;return w}
export function place(tmpl,scale,pos,rot=[0,0,0]){const c=tmpl.clone(true);c.scale.setScalar(scale);c.position.set(...pos);c.rotation.set(...rot);return c}
// 3D ノイズ (スライムの表面用)
function hash(x,y,z){const s=Math.sin(x*127.1+y*311.7+z*74.7)*43758.5453;return s-Math.floor(s)}
function vnoise(x,y,z){const i=Math.floor(x),j=Math.floor(y),k=Math.floor(z),f=x-i,g=y-j,h=z-k,u=f*f*(3-2*f),v=g*g*(3-2*g),w=h*h*(3-2*h);const L=(a,b,t)=>a+(b-a)*t;
  return L(L(L(hash(i,j,k),hash(i+1,j,k),u),L(hash(i,j+1,k),hash(i+1,j+1,k),u),v),L(L(hash(i,j,k+1),hash(i+1,j,k+1),u),L(hash(i,j+1,k+1),hash(i+1,j+1,k+1),u),v),w)}
// スライム: つぶれた球に低周波のうねりを足す。中の物が透けて見える
export function slime(r,o={}){const P=Object.assign({squash:.72,lump:.14,seed:3,color:0x7fe02c,flat:.25},o);const g=new THREE.IcosahedronGeometry(1,64),p=g.attributes.position,v=new THREE.Vector3();
  for(let i=0;i<p.count;i++){v.fromBufferAttribute(p,i);const n=vnoise(v.x*1.8+P.seed,v.y*1.8,v.z*1.8)-.5,n2=vnoise(v.x*4+P.seed,v.y*4,v.z*4)-.5;let l=1+n*P.lump*2+n2*P.lump*.5;let y=v.y*l*P.squash;if(y<-P.flat)y=-P.flat-(y+P.flat)*-.08;p.setXYZ(i,v.x*l*(1+.12*(1-v.y*v.y)),y,v.z*l*(1+.12*(1-v.y*v.y)))}
  g.computeVertexNormals();const m=new THREE.MeshPhysicalMaterial({color:0xa8ff4a,roughness:.14,metalness:0,transmission:P.trans??.62,thickness:P.thick??r*.25,ior:1.22,attenuationColor:new THREE.Color(0x7de82a),attenuationDistance:r*1.6,clearcoat:1,clearcoatRoughness:.1,emissive:0x2c9a08,emissiveIntensity:.42,envMapIntensity:1.2});
  const s=new THREE.Mesh(g,m);s.scale.setScalar(r);s.castShadow=true;return s}
// 目: カメラの方向から表面へ光線を飛ばし、当たった所に置く
export function eyesAt(s,sl,r,o={}){const P=Object.assign({gap:.34,up:.12,size:.085,push:.01},o);sl.updateMatrixWorld(true);s.cam.updateMatrixWorld(true);const d=new THREE.Vector3().subVectors(s.cam.position,sl.position).normalize();if(s.cam.isOrthographicCamera)d.set(0,0,1);const right=new THREE.Vector3().crossVectors(new THREE.Vector3(0,1,0),d).normalize(),up=new THREE.Vector3().crossVectors(d,right);const m=new THREE.MeshStandardMaterial({color:0x07130a,roughness:.2,transparent:true});
  for(const sx of [-1,1]){const o2=sl.position.clone().addScaledVector(d,r*6).addScaledVector(right,sx*r*P.gap).addScaledVector(up,r*P.up);const hit=new THREE.Raycaster(o2,d.clone().negate()).intersectObject(sl,false)[0];if(!hit)continue;const e=new THREE.Mesh(new THREE.SphereGeometry(1,24,16),m);e.position.copy(hit.point).addScaledVector(d,r*P.push);e.scale.set(r*P.size,r*P.size*1.4,r*P.size*.35);e.lookAt(e.position.clone().add(d));s.sc.add(e)}}
// 上から下への色の背景
export function gradBg(sc,stops){const c=document.createElement('canvas');c.width=8;c.height=512;const x=c.getContext('2d'),g=x.createLinearGradient(0,0,0,512);stops.forEach((s,i)=>g.addColorStop(i/(stops.length-1),s));x.fillStyle=g;x.fillRect(0,0,8,512);const t=new THREE.CanvasTexture(c);t.colorSpace=THREE.SRGBColorSpace;sc.background=t}
// 画面と同じ向きの正投影。1 単位 = 100px、x は左端 0、y は下端 0。後ろの壁に影を落とす
export function flatCam(s){const c=new THREE.OrthographicCamera(0,19.2,10.8,0,.1,100);c.position.set(0,0,30);c.lookAt(0,0,0);s.cam=c;const w=new THREE.Mesh(new THREE.PlaneGeometry(60,40),new THREE.ShadowMaterial({opacity:.32,color:0x3a2a00}));w.position.set(9.6,5.4,-1.6);w.receiveShadow=true;s.sc.add(w);s.sun.target.position.set(9.6,5.4,0);s.sc.add(s.sun.target);s.sun.position.set(2,10,24);s.sun.shadow.camera.left=-16;s.sun.shadow.camera.right=16;s.sun.shadow.camera.top=16;s.sun.shadow.camera.bottom=-16}
export function stage(cv,o={}){const P=Object.assign({bg:0xe8e4da,fov:30,exposure:1,sun:2.4,sunPos:[-6,12,7],env:.55,shadow:true},o);
  const r=new THREE.WebGLRenderer({canvas:cv,antialias:true,alpha:P.bg===null,preserveDrawingBuffer:true});r.setSize(W,H,false);r.setPixelRatio(1);r.outputColorSpace=THREE.SRGBColorSpace;r.toneMapping=THREE.ACESFilmicToneMapping;r.toneMappingExposure=P.exposure;r.shadowMap.enabled=P.shadow;r.shadowMap.type=THREE.PCFSoftShadowMap;
  const sc=new THREE.Scene();if(P.bg!==null)sc.background=new THREE.Color(P.bg);const pm=new THREE.PMREMGenerator(r);sc.environment=pm.fromScene(new RoomEnvironment(),.04).texture;sc.environmentIntensity=P.env;
  const cam=new THREE.PerspectiveCamera(P.fov,W/H,.05,500);const sun=new THREE.DirectionalLight(0xfff2dc,P.sun);sun.position.set(...P.sunPos);sun.castShadow=true;sun.shadow.mapSize.set(4096,4096);const d=P.shadowArea||14;Object.assign(sun.shadow.camera,{left:-d,right:d,top:d,bottom:-d,near:.1,far:80});sun.shadow.bias=-.0004;sun.shadow.radius=6;sc.add(sun);
  return {r,sc,cam,sun,async draw(){await Promise.all(pending);r.render(sc,this.cam)}}}
// 地面: 背景と同じ色で影だけ落とす
export function ground(sc,color,size=300,op=.42){const m=new THREE.Mesh(new THREE.PlaneGeometry(size,size),new THREE.ShadowMaterial({opacity:op,color:0x1a0a06}));m.rotation.x=-Math.PI/2;m.receiveShadow=true;sc.add(m);return m}
// 山を積む: 物理で落として積み上げる。list は [モデル名,大きさ,数]。大きい物から順に落とす
export async function drop(sc,list,o={}){const P=Object.assign({radius:1.6,seed:7,base:[0,0,0],every:7,settle:260,gap:1.2,tube:null},o),r=rng(P.seed),items=[];
  for(const e of list){const [n,s,c]=e;const t=typeof n==='string'?await model(n):n;for(let i=0;i<c;i++)items.push({t,s:s*(.84+r()*.32)})}
  items.sort((a,b)=>b.s-a.s);const w=new CANNON.World({gravity:new CANNON.Vec3(0,-22,0)});w.allowSleep=true;w.solver.iterations=12;w.defaultContactMaterial.friction=1.1;w.defaultContactMaterial.restitution=.02;
  const gb=new CANNON.Body({mass:0,shape:new CANNON.Plane()});gb.quaternion.setFromEuler(-Math.PI/2,0,0);gb.position.y=P.base[1];w.addBody(gb);
  const walls=[];if(P.tube){const T=P.tube;for(const [dx,dz] of [[1,0],[-1,0],[0,1],[0,-1]]){const wb=new CANNON.Body({mass:0,shape:new CANNON.Box(new CANNON.Vec3(dx?.5:T+1,40,dz?.5:T+1))});wb.position.set(P.base[0]+dx*(T+.5),40,P.base[2]+dz*(T+.5));w.addBody(wb);walls.push(wb)}}
  let top=P.base[1];const bodies=[];const add=it=>{const sz=it.t.userData.size||new THREE.Vector3(1,1,1),hx=sz.x*it.s*.46,hy=sz.y*it.s*.46,hz=sz.z*it.s*.46;const bd=new CANNON.Body({mass:hx*hy*hz*8+.05,shape:new CANNON.Box(new CANNON.Vec3(hx,hy,hz)),linearDamping:.12,angularDamping:.35,sleepSpeedLimit:.4});
    const a=r()*6.283,d=P.radius*Math.sqrt(r());bd.position.set(P.base[0]+Math.cos(a)*d,top+it.s*P.gap+1,P.base[2]+Math.sin(a)*d);bd.quaternion.setFromEuler((r()-.5)*2,r()*6.28,(r()-.5)*2);w.addBody(bd);bodies.push([bd,it]);};
  for(let i=0;i<items.length;i++){add(items[i]);for(let k=0;k<P.every;k++)w.step(1/60);top=P.base[1];for(const [b2] of bodies)if(b2.velocity.length()<3)top=Math.max(top,b2.position.y)}
  for(let k=0;k<420;k++)w.step(1/60);for(const [b2] of bodies){b2.linearDamping=.93;b2.angularDamping=.95;b2.wakeUp()}walls.forEach(b2=>w.removeBody(b2));for(let k=0;k<(P.tube?150:P.settle);k++)w.step(1/60);
  // 山の頂上: 中央付近の物の上位 1 割を除いた高さ。そこより大きく飛び出した物は描かない
  const ys=bodies.filter(([bd])=>Math.hypot(bd.position.x-P.base[0],bd.position.z-P.base[2])<(P.tube||P.radius)*.7).map(([bd])=>bd.position.y).sort((a,b)=>a-b);const peak=ys[Math.floor(ys.length*.88)]||0;
  for(const [bd,it] of bodies){if(bd.position.y>peak+2)continue;const c=it.t.clone(true);c.scale.setScalar(it.s);c.position.set(bd.position.x,bd.position.y,bd.position.z);c.quaternion.set(bd.quaternion.x,bd.quaternion.y,bd.quaternion.z,bd.quaternion.w);sc.add(c)}return peak}
// 子ども部屋の積み木とボール (ゲームの部屋の見た目に合わせた単純な形)
export function toy(kind,color){const g=new THREE.Group();const m=new THREE.MeshStandardMaterial({color,roughness:.45});const mesh=new THREE.Mesh(kind==='ball'?new THREE.SphereGeometry(.5,32,24):new THREE.BoxGeometry(1,1,1),m);mesh.castShadow=mesh.receiveShadow=true;g.add(mesh);g.userData.size=new THREE.Vector3(1,1,1);return g}
// ロゴ: カタカナ 4 文字を太い線で組んだ自作の字形 (100x100 の升)
export const GLYPH={'チ':[[[72,10],[26,24]],[[8,48],[92,48]],[[52,20],[52,62],[30,92]]],'リ':[[[24,12],[24,60]],[[76,8],[76,56],[44,94]]],'ツ':[[[12,24],[22,48]],[[42,18],[52,42]],[[88,16],[78,62],[36,94]]],'モ':[[[18,16],[82,16]],[[6,48],[94,48]],[[42,16],[42,76],[54,88],[92,88]]]};
export function drawGlyph(c,ch,x,y,s,w=19){c.save();c.translate(x,y);c.scale(s/100,s/100);c.lineWidth=w;c.lineCap='butt';c.lineJoin='miter';c.miterLimit=3;for(const st of GLYPH[ch]){c.beginPath();st.forEach(([a,b],i)=>i?c.lineTo(a,b):c.moveTo(a,b));c.stroke()}c.restore()}
export function logoSVG(ch,w=19,color='currentColor'){return `<svg viewBox="0 0 100 100" style="overflow:visible"><g fill="none" stroke="${color}" stroke-width="${w}" stroke-linejoin="miter" stroke-miterlimit="3">${GLYPH[ch].map(st=>`<polyline points="${st.map(p=>p.join(',')).join(' ')}"/>`).join('')}</g></svg>`}
// 厚みのある文字: 透明抜きの板を重ねて立体にする
export function slab(sc,ch,o={}){const P=Object.assign({size:6,pos:[0,0,0],rot:[0,0,0],front:0xffffff,side:0xbbbbbb,depth:1,layers:26,w:19},o);const cv=document.createElement('canvas');cv.width=cv.height=1024;const c=cv.getContext('2d');c.strokeStyle='#fff';drawGlyph(c,ch,112,112,800,P.w);const t=new THREE.CanvasTexture(cv);t.anisotropy=8;
  const g=new THREE.Group(),pl=new THREE.PlaneGeometry(P.size*1.28,P.size*1.28);for(let i=0;i<P.layers;i++){const fr=i===P.layers-1;const m=new THREE.MeshStandardMaterial({map:t,alphaTest:.5,color:fr?P.front:P.side,roughness:.55,side:THREE.DoubleSide});const q=new THREE.Mesh(pl,m);q.position.z=(i/(P.layers-1)-.5)*P.depth;q.castShadow=true;q.receiveShadow=fr;q.customDepthMaterial=new THREE.MeshDepthMaterial({depthPacking:THREE.RGBADepthPacking,map:t,alphaTest:.5});g.add(q)}
  g.position.set(...P.pos);g.rotation.set(...P.rot);sc.add(g);return g}
export function ready(){document.body.dataset.done='1'}
