# bake.html を開いて、物差しの画面の画像を Resources/Textures/UI/Scale/ へ書き出す。
# 小道具は透明な余白を切り落とし、同じ形の白い影 (_sil) も作る。縦横比は最後に表示する
import base64,functools,http.server,io,json,pathlib,subprocess,threading
from PIL import Image
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
out=root/'Resources/Textures/UI/Scale';out.mkdir(parents=True,exist_ok=True)
edge=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
done=threading.Event();sizes={}
class H(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*a):pass
    def do_POST(self):
        body=self.rfile.read(int(self.headers.get('Content-Length',0)))
        if self.path=='/done':done.set()
        else:
            name=self.path.split('/')[-1];im=Image.open(io.BytesIO(base64.b64decode(body.split(b',')[1]))).convert('RGBA')
            if not name.startswith('logo'):
                im=im.crop(im.getbbox());m=768 if name in('car','horse','slime','barrier') else 512
                k=min(1,m/max(im.size));im=im.resize((max(1,round(im.width*k)),max(1,round(im.height*k))),Image.LANCZOS)
                # 端の色が反対側へ回り込まないよう、透明の余白を残す
                pad=Image.new('RGBA',(im.width+8,im.height+8),(0,0,0,0));pad.paste(im,(4,4));im=pad
                white=Image.new('RGBA',im.size,(255,255,255,0));white.putalpha(im.getchannel('A'));white.save(out/(name+'_sil.png'),optimize=True)
                sizes[name]=round(im.width/im.height,3)
            im.save(out/(name+'.png'),optimize=True);print(name,im.size,flush=True)
        self.send_response(200);self.end_headers()
srv=http.server.ThreadingHTTPServer(('127.0.0.1',8766),functools.partial(H,directory=str(root)))
threading.Thread(target=srv.serve_forever,daemon=True).start()
p=subprocess.Popen([edge,'--headless=new','--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist','--remote-debugging-port=0','--user-data-dir='+str(here/'.edge'),'http://127.0.0.1:8766/Docs/UIDesign/chiritsumo/bake.html'])
ok=done.wait(200);p.kill();print('done' if ok else 'timeout');print(json.dumps(sizes))
