# scenes の HTML を Edge のヘッドレスで 1920x1080 の PNG にする。引数で名前を絞り込める
import pathlib,subprocess,sys
here=pathlib.Path(__file__).resolve().parent
edge=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
pat=sys.argv[1:] or ['']
for page in sorted(here.glob('[abc][123]_*.html')):
    if not any(p in page.stem for p in pat):continue
    out=here/(page.stem+'.png')
    r=subprocess.run([edge,'--headless=new','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files','--force-device-scale-factor=1','--window-size=1920,1080','--virtual-time-budget=45000','--user-data-dir='+str(here/'.edge'),'--screenshot='+str(out),page.as_uri()],capture_output=True,timeout=170)
    print(out.name,r.returncode,out.stat().st_size if out.exists() else None)
