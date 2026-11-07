# ゲームの GPU 流体を撮った画像 (tests/out/sprite) から、透明度付きのスライムの連番を作る。
# 同じ瞬間を黄と青の背景で撮ってあるので、背景の差の残り方から透明度を求める (差が消えた所ほど不透明)。
# 撮影: DirectXGameApp.exe --slime-life-smoke --slime-devour --devour-stage=0 --slime-sprite-capture
import json,pathlib,statistics
from PIL import Image
root=pathlib.Path(__file__).resolve().parents[3]
src=root/'tests/out/sprite';out=root/'Resources/Textures/UI/Scale'
VIEW=(294,256,1560,722)   # エディタの Game ビューのうち、HUD の無い範囲
SIDE=14                   # 背景の色を調べる左右の幅
MAXW=1024
def matte(a_path,b_path):
    a=Image.open(a_path).convert('RGB').crop(VIEW);b=Image.open(b_path).convert('RGB').crop(VIEW);w,h=a.size;pa=a.load();pb=b.load()
    res=Image.new('RGBA',(w,h),(0,0,0,0));pr=res.load()
    for y in range(h):
        cols=list(range(4,4+SIDE))+list(range(w-4-SIDE,w-4))
        b1=[statistics.median(pa[x,y][c] for x in cols) for c in range(3)];b2=[statistics.median(pb[x,y][c] for x in cols) for c in range(3)]
        d=[b1[c]-b2[c] for c in range(3)];dd=sum(v*v for v in d) or 1
        for x in range(w):
            c1=pa[x,y];c2=pb[x,y]
            t=((c1[0]-c2[0])*d[0]+(c1[1]-c2[1])*d[1]+(c1[2]-c2[2])*d[2])/dd
            al=1-t
            if al<.05:continue
            al=min(1,al);pr[x,y]=(*[max(0,min(255,round((c1[c]-(1-al)*b1[c])/al))) for c in range(3)],round(al*255))
    # 壁と床の継ぎ目は左右の端まで透明度が残るので、その行の体の外側を消す
    for y in range(h):
        if pr[8,y][3] or pr[w-8,y][3]:
            for x in range(w):
                if pr[x,y][3]>=170:break
                pr[x,y]=(0,0,0,0)
            for x in range(w-1,-1,-1):
                if pr[x,y][3]>=170:break
                pr[x,y]=(0,0,0,0)
    return res
sizes={}
for kind,count in (('idle',12),('roll',10)):
    frames=[matte(src/f'{kind}_{i:02d}_a.png',src/f'{kind}_{i:02d}_b.png') for i in range(count)]
    # 体 (ほぼ不透明の所) の外枠をそろえ、全コマで同じ切り抜きにする
    boxes=[f.getchannel('A').point(lambda v:255 if v>150 else 0).getbbox() for f in frames]
    box=(min(b[0] for b in boxes),min(b[1] for b in boxes),max(b[2] for b in boxes),max(b[3] for b in boxes))
    pad_top=min(box[1],70);box=(max(0,box[0]-24),box[1]-pad_top,min(frames[0].width,box[2]+24),min(frames[0].height,box[3]+10))
    k=min(1,MAXW/(box[2]-box[0]))
    for i,f in enumerate(frames):
        f=f.crop(box);f=f.resize((round(f.width*k),round(f.height*k)),Image.LANCZOS)
        canvas=Image.new('RGBA',(f.width+8,f.height+8),(0,0,0,0));canvas.paste(f,(4,4));canvas.save(out/f'slime_{kind}_{i:02d}.png',optimize=True)
        if kind=='idle'and i==0:
            white=Image.new('RGBA',canvas.size,(255,255,255,0));white.putalpha(canvas.getchannel('A').point(lambda v:255 if v>90 else 0));white.save(out/'slime_shadow.png',optimize=True)
    sizes[kind]=(canvas.width,canvas.height,round(canvas.width/canvas.height,3))
    print(kind,box,sizes[kind],flush=True)
print(json.dumps(sizes))
