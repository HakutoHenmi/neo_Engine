# 確認用の一覧画像を作る
from PIL import Image
for k,names in {'P':['p1_title_pile','p2_select_pile','p3_result_pile'],'G':['g1_title_gut','g2_select_gut','g3_result_gut'],'S':['s1_title_scale','s2_select_scale','s3_result_scale']}.items():
    s=Image.new('RGB',(1920,1080))
    for i,n in enumerate(names):
        s.paste(Image.open(n+'.png').convert('RGB').resize((960,540)),((i%2)*960,(i//2)*540))
    s.save('_sheet'+k+'.jpg',quality=88)
