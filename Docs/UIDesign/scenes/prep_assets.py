# 参考画像の素材をゲームのテクスチャと実機スクリーンショットから切り出す
import pathlib,shutil
from PIL import Image
here=pathlib.Path(__file__).resolve().parent
root=here.parents[2]
O=here/'assets';O.mkdir(exist_ok=True)
R=root/'Resources/Textures';T=root/'tests/out'
Image.open(R/'PolyHaven/asphalt_02_diff_2k.jpg').convert('RGB').save(O/'asphalt.jpg',quality=88)
Image.open(R/'PolyHaven/wooden_picnic_table_top_diff_2k.jpg').convert('RGB').resize((1024,1024)).save(O/'wood_a.jpg',quality=85)
Image.open(R/'PolyHaven/weathered_plank_siding_diff_2k.jpg').convert('RGB').resize((1024,1024)).save(O/'wood_b.jpg',quality=85)
Image.open(R/'paper.png').convert('RGB').save(O/'paper.jpg',quality=88)
Image.open(T/'slime-life-devour-big-moving.png').convert('RGB').save(O/'town_big.jpg',quality=92)
Image.open(T/'slime-life-devour-house.png').convert('RGB').save(O/'town_house.jpg',quality=92)
Image.open(T/'slime-life-room-meals.png').convert('RGB').crop((294,126,1560,855)).save(O/'room_meals.jpg',quality=92)
shutil.copy(root/'Docs/UIDesign/bg.jpg',O/'avenue.jpg')
K=R/'UI/kenney_input-prompts_1.5'
for n in 'keyboard_enter keyboard_escape keyboard_n keyboard_r keyboard_tab mouse_left keyboard_arrows_horizontal'.split():
    p=K/'Keyboard & Mouse/Default'/(n+'.png')
    if p.exists():shutil.copy(p,O/p.name)
for n in 'xbox_button_a xbox_button_b xbox_button_x xbox_button_y xbox_dpad'.split():
    p=K/'Xbox Series/Default'/(n+'.png')
    if p.exists():shutil.copy(p,O/p.name)
sheet=Image.new('RGB',(1500,500))
for i,n in enumerate(['wood_a','wood_b','asphalt']):
    sheet.paste(Image.open(O/(n+'.jpg')).resize((500,500)),(i*500,0))
sheet.save(O/'_tex.jpg')
