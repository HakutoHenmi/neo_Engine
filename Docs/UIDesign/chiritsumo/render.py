# チリツモの案を PNG にする。リポジトリの根を 8765 番で配信している前提 (python -m http.server 8765 --bind 127.0.0.1)
import pathlib,subprocess,sys
here=pathlib.Path(__file__).resolve().parent
edge=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
pat=sys.argv[1:] or ['']
for page in sorted(here.glob('[a-z][0-9]_*.html')):
    if not any(p in page.stem for p in pat):continue
    out=here/(page.stem+'.png')
    url='http://127.0.0.1:8765/Docs/UIDesign/chiritsumo/'+page.name
    r=subprocess.run([edge,'--headless=new','--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist','--hide-scrollbars','--force-device-scale-factor=1','--window-size=1920,1080','--virtual-time-budget=120000','--timeout=170000','--user-data-dir='+str(here/'.edge'),'--screenshot='+str(out),url],capture_output=True,timeout=280)
    print(out.name,r.returncode,out.stat().st_size if out.exists() else None)
