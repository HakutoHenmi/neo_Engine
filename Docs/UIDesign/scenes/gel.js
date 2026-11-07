// 参考画像の共通処理: スライムのゲル描画 (高さ場 + 屈折 + 吸収 + 鏡面) と、ノイズ・網点など
const W=1920,H=1080;
function mk(w=W,h=H){const c=document.createElement('canvas');c.width=w;c.height=h;return c}
function loadImg(src){return new Promise((ok,ng)=>{const i=new Image();i.onload=()=>ok(i);i.onerror=ng;i.src=src})}
function rng(seed){let s=seed>>>0;return()=>{s=(s*1664525+1013904223)>>>0;return s/4294967296}}
// なめらかなノイズの画像 (グレー)
function noiseCanvas(scale,seed=1,oct=4,w=W,h=H){const out=mk(w,h),o=out.getContext('2d');const r=rng(seed);
  o.fillStyle='#808080';o.fillRect(0,0,w,h);let a=.5;
  for(let k=0;k<oct;k++){const s=scale/(2**k),cw=Math.ceil(w/s)+2,ch=Math.ceil(h/s)+2,t=mk(cw,ch),tc=t.getContext('2d'),id=tc.createImageData(cw,ch);
    for(let i=0;i<cw*ch;i++){const v=r()*255;id.data[i*4]=id.data[i*4+1]=id.data[i*4+2]=v;id.data[i*4+3]=255}
    tc.putImageData(id,0,0);o.globalAlpha=a;o.imageSmoothingEnabled=true;o.imageSmoothingQuality='high';o.drawImage(t,0,0,cw*s,ch*s);a*=.55}
  o.globalAlpha=1;return out}
// 不透明度をノイズで削って「かすれ」にする
function wear(ctx,noise,lo,hi){const w=ctx.canvas.width,h=ctx.canvas.height,d=ctx.getImageData(0,0,w,h),n=noise.getContext('2d').getImageData(0,0,w,h).data;
  for(let i=0;i<w*h;i++){let v=(n[i*4]/255-lo)/(hi-lo);v=v<0?0:v>1?1:v;d.data[i*4+3]*=v}ctx.putImageData(d,0,0)}
function blurred(src,px){const c=mk(src.width,src.height),o=c.getContext('2d');o.filter=`blur(${px}px)`;o.drawImage(src,0,0);return c}
function boxBlur(f,w,h,r){const t=new Float32Array(f.length);
  for(let y=0;y<h;y++){let s=0;for(let x=-r;x<=r;x++)s+=f[y*w+Math.min(w-1,Math.max(0,x))];for(let x=0;x<w;x++){t[y*w+x]=s/(2*r+1);s+=f[y*w+Math.min(w-1,x+r+1)]-f[y*w+Math.max(0,x-r)]}}
  for(let x=0;x<w;x++){let s=0;for(let y=-r;y<=r;y++)s+=t[Math.min(h-1,Math.max(0,y))*w+x];for(let y=0;y<h;y++){f[y*w+x]=s/(2*r+1);s+=t[Math.min(h-1,y+r+1)*w+x]-t[Math.max(0,y-r)*w+x]}}}
function bbox(m,w,h){let a=w,b=h,c=0,d=0;for(let y=0;y<h;y+=4)for(let x=0;x<w;x+=4)if(m[y*w+x]>.5){a=Math.min(a,x);b=Math.min(b,y);c=Math.max(c,x);d=Math.max(d,y)}return{x:a,y:b,w:Math.max(1,c-a),h:Math.max(1,d-b)}}
// shape: 白で形を描いたキャンバス。ctx の絵の上にゲルを重ねる
function gel(ctx,shape,o={}){const w=ctx.canvas.width,h=ctx.canvas.height;
  const P=Object.assign({edge:9,big:70,height:90,refract:.55,sigma:[2.4,.35,3.2],base:[.40,.92,.12],shadow:.55,shadowOff:[14,20],shadowBlur:26,light:[-.42,-.62,.66],seed:5,specks:true,gain:1,smooth:3,density:1},o);
  if(P.shadow>0){const s=mk(w,h),c=s.getContext('2d');c.filter=`blur(${P.shadowBlur}px)`;c.drawImage(shape,P.shadowOff[0],P.shadowOff[1]);c.filter='none';c.globalCompositeOperation='source-in';c.fillStyle='#061208';c.fillRect(0,0,w,h);ctx.globalAlpha=P.shadow;ctx.drawImage(s,0,0);ctx.globalAlpha=1}
  const e=blurred(shape,P.edge).getContext('2d').getImageData(0,0,w,h).data,b=blurred(shape,P.big).getContext('2d').getImageData(0,0,w,h).data;
  const f=new Float32Array(w*h),m=new Float32Array(w*h);
  for(let i=0;i<w*h;i++){const a=e[i*4+3]/255,bb=b[i*4+3]/255;const t=Math.min(1,Math.max(0,(a-.47)/.06));m[i]=t*t*(3-2*t);const q=Math.max(0,(a-.47)/.53);f[i]=Math.pow(q,.55)*(.28+.72*Math.pow(bb,1.25))}
  boxBlur(f,w,h,P.smooth);boxBlur(f,w,h,P.smooth);boxBlur(f,w,h,2);
  const src=ctx.getImageData(0,0,w,h),out=ctx.getImageData(0,0,w,h),S=src.data,D=out.data,[lx,ly,lz]=P.light;
  let hx=lx,hy=ly,hz=lz+1;const hl=Math.hypot(hx,hy,hz);hx/=hl;hy/=hl;hz/=hl;const rim=[.80,.98,.62];
  for(let y=1;y<h-1;y++)for(let x=1;x<w-1;x++){const i=y*w+x;if(m[i]<=.001)continue;
    const dx=(f[i+1]-f[i-1])*P.height*.5,dy=(f[i+w]-f[i-w])*P.height*.5;let nx=-dx,ny=-dy,nz=1;const l=Math.hypot(nx,ny,nz);nx/=l;ny/=l;nz/=l;
    const t=f[i],off=P.refract*P.height*(.3+t);let sx=Math.round(x+nx*off),sy=Math.round(y+ny*off);sx=sx<0?0:sx>=w?w-1:sx;sy=sy<0?0:sy>=h?h-1:sy;const k=(sy*w+sx)*4;
    const dif=Math.max(0,nx*lx+ny*ly+nz*lz),hd=Math.max(0,nx*hx+ny*hy+nz*hz),spec=Math.pow(hd,150)*1.25+Math.pow(hd,22)*.16;
    const fr=Math.pow(1-nz,2.6),th=(t*1.3+.12)*P.density,sc=1-Math.exp(-2.4*th);
    for(let c=0;c<3;c++){const tr=Math.exp(-P.sigma[c]*th),bg=S[k+c]/255;let v=bg*tr*(1-sc*.55)+P.base[c]*sc*(.5+.72*dif)*P.gain;
      v=v*(1-fr*.5)+rim[c]*fr*.5;v+=spec;v=v>1?1:v;D[i*4+c]=S[i*4+c]*(1-m[i])+v*255*m[i]}}
  ctx.putImageData(out,0,0);
  if(P.specks){const r=rng(P.seed),mask=mk(w,h),mc=mask.getContext('2d'),id=mc.createImageData(w,h);for(let i=0;i<w*h;i++){id.data[i*4]=255;id.data[i*4+1]=255;id.data[i*4+2]=255;id.data[i*4+3]=f[i]>.38?255:0}mc.putImageData(id,0,0);
    const dots=mk(w,h),dc=dots.getContext('2d'),bb=bbox(m,w,h);for(let i=0;i<(bb.w*bb.h)/2600;i++){const x=bb.x+r()*bb.w,y=bb.y+r()*bb.h,s=.8+r()*r()*3.2;dc.fillStyle=`rgba(225,255,190,${.14+r()*.32})`;dc.beginPath();dc.arc(x,y,s,0,7);dc.fill()}
    dc.globalCompositeOperation='destination-in';dc.drawImage(mask,0,0);ctx.drawImage(dots,0,0)}
  return {f,m}}
// 太さの変わる線 (スライムの跡) を丸の連続で描く。pts は [x,y,太さ]
function stroke(c,pts,steps=500){c.fillStyle='#fff';for(let s=0;s<steps;s++){const u=s/steps*(pts.length-1),i=Math.floor(u),t=u-i,a=pts[Math.max(0,i-1)],b=pts[i],d=pts[Math.min(pts.length-1,i+1)],e=pts[Math.min(pts.length-1,i+2)];
  const cr=k=>.5*((2*b[k])+(-a[k]+d[k])*t+(2*a[k]-5*b[k]+4*d[k]-e[k])*t*t+(-a[k]+3*b[k]-3*d[k]+e[k])*t*t*t);c.beginPath();c.arc(cr(0),cr(1),Math.max(.5,cr(2)),0,7);c.fill()}}
function blob(c,x,y,r,ry){c.fillStyle='#fff';c.beginPath();c.ellipse(x,y,r,ry||r,0,0,7);c.fill()}
// 画像を網点にする。ink は [r,g,b]。paper の上に乗算で重ねる
function halftone(ctx,img,sx,sy,sw,sh,dx,dy,dw,dh,o={}){const P=Object.assign({cell:7,angle:.5,ink:[24,22,30],gamma:1,contrast:1.25,lift:0},o);
  const t=mk(Math.ceil(dw),Math.ceil(dh)),c=t.getContext('2d');c.imageSmoothingQuality='high';c.drawImage(img,sx,sy,sw,sh,0,0,dw,dh);const d=c.getImageData(0,0,t.width,t.height),p=d.data,w=t.width,hh=t.height;
  const ca=Math.cos(P.angle),sa=Math.sin(P.angle),cl=P.cell;const lum=new Float32Array(w*hh);for(let i=0;i<w*hh;i++)lum[i]=(p[i*4]*.3+p[i*4+1]*.59+p[i*4+2]*.11)/255;boxBlur(lum,w,hh,2);
  for(let y=0;y<hh;y++)for(let x=0;x<w;x++){const i=y*w+x,u=(x*ca+y*sa)/cl,v=(-x*sa+y*ca)/cl,fu=u-Math.floor(u)-.5,fv=v-Math.floor(v)-.5,dist=Math.hypot(fu,fv)/.72;
    let l=(lum[i]-.5)*P.contrast+.5+P.lift;l=Math.pow(Math.min(1,Math.max(0,l)),P.gamma);const a=Math.min(1,Math.max(0,((1-l)-dist)*7+.5));
    p[i*4]=P.ink[0];p[i*4+1]=P.ink[1];p[i*4+2]=P.ink[2];p[i*4+3]=a*255}
  c.putImageData(d,0,0);ctx.drawImage(t,dx,dy);return t}
const fontReady=(list)=>Promise.all(list.map(([f,t])=>document.fonts.load(f,t)));
