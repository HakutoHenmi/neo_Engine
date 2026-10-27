# デザイン案の HTML に共通アイコンを差し込み、Edge のヘッドレスで 1280x720 の PNG にする
import pathlib,subprocess,sys
here=pathlib.Path(__file__).resolve().parent
icons=(here/'icons.html').read_text(encoding='utf-8')
edge=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
for page in sorted(here.glob('[a-z]_*.html')):
    built=here/('_'+page.name);built.write_text(page.read_text(encoding='utf-8').replace('<!--ICONS-->',icons),encoding='utf-8')
    out=here/(page.stem+'.png')
    subprocess.run([edge,'--headless=new','--disable-gpu','--hide-scrollbars','--force-device-scale-factor=1','--window-size=1280,720','--virtual-time-budget=8000','--screenshot='+str(out),built.as_uri()],check=True,capture_output=True)
    built.unlink();print(out.name)
