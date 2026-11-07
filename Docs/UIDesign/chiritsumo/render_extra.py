# extra.html の案を PNG にする (Edge を使用)。ゲーム画面の背景は tests/out のスクリーンショットから切り出す
import pathlib,subprocess,sys
from PIL import Image
here=pathlib.Path(__file__).resolve().parent;root=here.parents[2]
edge=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
Image.open(root/'tests/out/slime-life-room-meals.png').convert('RGB').crop((294,126,1560,855)).save(here/'game_room.jpg',quality=88)
names={'L1':'x_load1','L2':'x_load2','L3':'x_load3','C1':'x_credit1','C2':'x_credit2','P1':'x_pause1','P2':'x_pause2','G1':'x_graphics'}
for key,name in names.items():
    if sys.argv[1:] and key not in sys.argv[1:]:continue
    out=here/(name+'.png')
    subprocess.run([edge,'--headless=new','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files','--force-device-scale-factor=1','--window-size=1920,1080','--virtual-time-budget=6000','--user-data-dir='+str(here/'.edge'),'--screenshot='+str(out),(here/'extra.html').as_uri()+'#'+key],capture_output=True,timeout=60)
    print(name,out.stat().st_size)
s=Image.new('RGB',(1920,2160))
for i,k in enumerate(names.values()):
    p=here/(k+'.png')
    if p.exists():s.paste(Image.open(p).convert('RGB').resize((960,540)),((i%2)*960,(i//2)*540))
s.save(here/'_sheet.jpg',quality=88)
