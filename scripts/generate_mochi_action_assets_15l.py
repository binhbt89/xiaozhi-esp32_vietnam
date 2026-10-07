from pathlib import Path

OUT = Path('main/boards/xingzhi-cube-1.83tft-wifi/mochi_action_assets_15l.h')

PAL={
'T':(0,0,0,0),'K':(76,71,112,255),'k':(105,97,139,255),'C':(255,247,236,255),'W':(255,255,252,255),
'B':(148,209,245,255),'b':(104,176,232,255),'P':(255,157,187,255),'p':(255,203,218,255),
'O':(247,183,117,255),'S':(246,142,104,255),'G':(255,207,83,255),'g':(255,231,145,255),
'M':(166,229,216,255),'R':(181,126,91,255),'D':(137,89,70,255),'L':(115,186,115,255),
'H':(255,221,203,255),'h':(245,178,170,255),'U':(221,239,255,255),'Q':(205,174,235,255)
}

def blank(w,h): return [['T']*w for _ in range(h)]
def put(im,x,y,c):
    h=len(im);w=len(im[0])
    if 0<=x<w and 0<=y<h: im[y][x]=c

def rect(im,x0,y0,x1,y1,c):
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):put(im,x,y,c)

def ellipse(im,x0,y0,x1,y1,c):
    cx=(x0+x1)/2;cy=(y0+y1)/2;rx=max(.5,(x1-x0+1)/2);ry=max(.5,(y1-y0+1)/2)
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            if ((x-cx)/rx)**2+((y-cy)/ry)**2<=1:put(im,x,y,c)

def poly(im,pts,c):
    xs=[p[0] for p in pts];ys=[p[1] for p in pts]
    for y in range(min(ys),max(ys)+1):
      for x in range(min(xs),max(xs)+1):
        inside=False;j=len(pts)-1
        for i in range(len(pts)):
          xi,yi=pts[i];xj,yj=pts[j]
          if ((yi>y)!=(yj>y)) and x<(xj-xi)*(y-yi)/(yj-yi+1e-9)+xi:inside=not inside
          j=i
        if inside:put(im,x,y,c)

def line(im,x0,y0,x1,y1,c,w=1):
    dx=abs(x1-x0);sx=1 if x0<x1 else -1;dy=-abs(y1-y0);sy=1 if y0<y1 else -1;err=dx+dy
    while True:
      for oy in range(-(w//2),w//2+1):
       for ox in range(-(w//2),w//2+1):put(im,x0+ox,y0+oy,c)
      if x0==x1 and y0==y1:break
      e2=2*err
      if e2>=dy:err+=dy;x0+=sx
      if e2<=dx:err+=dx;y0+=sy

def fish(stage):
    im=blank(42,42)
    ellipse(im,3,28,38,38,'K');ellipse(im,5,29,36,36,'U');ellipse(im,7,30,34,35,'C')
    end={0:35,1:28,2:20}[stage]
    if end>=30:
      poly(im,[(30,14),(38,9),(36,18),(39,24),(30,22)],'K');poly(im,[(30,15),(36,11),(34,18),(37,22),(30,21)],'O')
    poly(im,[(7,19),(12,12),(22,9),(31,12),(35,18),(31,25),(21,28),(12,25),(6,21)],'K')
    poly(im,[(8,19),(13,13),(22,10),(30,13),(33,18),(30,24),(21,26),(13,24),(8,21)],'g')
    poly(im,[(9,19),(14,14),(22,11),(29,14),(31,18),(29,22),(21,25),(14,23),(9,21)],'C')
    if end<35:
      for y in range(8,29):
        for x in range(end+1,42): im[y][x]='T'
      for cy in (14,19,24): ellipse(im,end-2,cy-2,end+2,cy+2,'T')
      for x,y in [(end+4,13),(end+6,19),(end+3,25)]:
        if x<41: put(im,x,y,'g')
    ellipse(im,11,17,14,20,'D');put(im,12,17,'W')
    line(im,17,14,min(26,end),14,'O');line(im,17,18,min(28,end),18,'O');line(im,17,22,min(25,end),22,'O')
    poly(im,[(18,11),(21,7),(24,11)],'P');put(im,9,14,'W');put(im,15,10,'W')
    return im

def meal(stage):
    im=blank(42,42)
    ellipse(im,4,12,37,37,'K');ellipse(im,6,13,35,35,'C')
    rect(im,7,24,34,29,'B');rect(im,10,29,31,33,'b');rect(im,14,34,27,35,'W')
    ellipse(im,5,10,36,21,'K');ellipse(im,7,11,34,19,'W')
    levels={0:18,1:16,2:14};ytop=levels[stage];ellipse(im,8,ytop-5,33,19,'p')
    pieces=[(11,11,'S'),(19,10,'O'),(27,12,'R')][:3-stage]
    for x,y,c in pieces: ellipse(im,x,y,x+7,y+6,c)
    if stage<=1: rect(im,25,9,27,14,'L');rect(im,23,10,30,12,'L')
    if stage==0: poly(im,[(12,11),(16,7),(21,10),(18,15),(12,14)],'C')
    if stage==0: line(im,12,5,14,2,'U');line(im,25,5,27,2,'U')
    elif stage==1: line(im,21,5,22,2,'U')
    return im

def dessert(stage):
    im=blank(42,42)
    ellipse(im,8,13,34,35,'K');ellipse(im,10,14,32,33,'C')
    rect(im,11,24,31,31,'B');rect(im,14,31,28,34,'C');rect(im,19,34,23,38,'B');rect(im,15,38,27,39,'K');rect(im,17,38,25,38,'g')
    tops={0:5,1:9,2:13};top=tops[stage];ellipse(im,12,top,30,21,'W');ellipse(im,15,top-2,27,16,'C')
    if stage<2:
      cy=11 if stage==0 else 14
      poly(im,[(21,cy-7),(23,cy-2),(28,cy-2),(24,cy+1),(26,cy+6),(21,cy+3),(16,cy+6),(18,cy+1),(14,cy-2),(19,cy-2)],'G')
      put(im,21,cy-2,'W')
    berries=[(12,17),(29,18),(16,22)][:3-stage]
    for x,y in berries: ellipse(im,x,y,x+4,y+4,'P')
    put(im,11,15,'W');put(im,30,16,'W')
    return im

def hand():
    im=blank(36,28)
    poly(im,[(1,19),(10,15),(15,19),(14,27),(1,27)],'K');poly(im,[(2,19),(10,16),(13,20),(12,27),(2,27)],'B')
    poly(im,[(9,17),(13,12),(16,4),(19,3),(21,5),(20,11),(22,4),(25,4),(26,6),(24,12),(26,7),(29,8),(29,10),(26,14),(31,12),(34,14),(33,17),(27,21),(20,23),(14,21)],'K')
    poly(im,[(10,17),(14,12),(17,5),(19,4),(20,6),(18,13),(21,14),(23,5),(25,5),(24,13),(26,14),(28,9),(29,9),(27,15),(28,16),(32,14),(33,15),(31,17),(26,20),(20,22),(15,20)],'H')
    line(im,15,17,24,18,'h');put(im,19,5,'p');put(im,24,6,'p');put(im,29,10,'p');put(im,32,15,'p');put(im,12,18,'W');put(im,17,10,'W')
    return im

def heart():
    im=blank(28,28)
    ellipse(im,4,4,14,15,'K');ellipse(im,13,4,24,15,'K');poly(im,[(4,10),(24,10),(14,25)],'K')
    ellipse(im,6,5,13,13,'P');ellipse(im,14,5,22,13,'P');poly(im,[(6,10),(22,10),(14,22)],'P')
    ellipse(im,8,6,11,9,'W');put(im,20,13,'p');put(im,5,17,'g');put(im,23,4,'B')
    return im

def sparkle():
    im=blank(22,22)
    poly(im,[(11,1),(13,8),(20,10),(13,12),(11,20),(9,12),(2,10),(9,8)],'G')
    poly(im,[(11,5),(12,9),(16,10),(12,11),(11,16),(10,11),(6,10),(10,9)],'W')
    put(im,3,4,'B');put(im,18,4,'P');put(im,18,17,'B')
    return im

def emit(lines,name,im):
    h=len(im);w=len(im[0]);data=[]
    for row in im:
        for key in row:
            r,g,b,a=PAL[key];data.extend((b,g,r,a))
    arr=f'{name}_map'
    lines.append(f'alignas(4) static const uint8_t {arr}[] = {{')
    for i in range(0,len(data),16): lines.append('    '+', '.join(f'0x{x:02X}' for x in data[i:i+16])+',')
    lines += ['};',f'static const lv_image_dsc_t {name} = {{',
              f'    {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_ARGB8888, 0, {w}, {h}, {w*4}, 0}},',
              f'    sizeof({arr}),',f'    {arr},','    nullptr,','    nullptr','};','']

lines=['#pragma once','','#include <lvgl.h>','#include <cstddef>','',
       '// Auto-generated by scripts/generate_mochi_action_assets_15l.py.',
       '// Large reaction art for 15L: food consumption stages, petting hand, heart and sparkle.',
       '// Cat art itself stays on the hardware-tested 80x80 Mochi emotions so scale/proportions never drift.','']
foods=[('fish',fish),('meal',meal),('dessert',dessert)]
for name,fn in foods:
    for stage in range(3): emit(lines,f'mochi_action_{name}_{stage}',fn(stage))
emit(lines,'mochi_action_hand',hand())
emit(lines,'mochi_action_heart',heart())
emit(lines,'mochi_action_sparkle',sparkle())
lines += [
'static const lv_image_dsc_t* const kMochiActionFoodStages[3][3] = {',
'    {&mochi_action_fish_0, &mochi_action_fish_1, &mochi_action_fish_2},',
'    {&mochi_action_meal_0, &mochi_action_meal_1, &mochi_action_meal_2},',
'    {&mochi_action_dessert_0, &mochi_action_dessert_1, &mochi_action_dessert_2},',
'};','',
'static const lv_image_dsc_t* const kMochiActionHand = &mochi_action_hand;',
'static const lv_image_dsc_t* const kMochiActionHeart = &mochi_action_heart;',
'static const lv_image_dsc_t* const kMochiActionSparkle = &mochi_action_sparkle;','']
OUT.write_text('\n'.join(lines),encoding='utf-8')
print(f'Generated {OUT} ({OUT.stat().st_size} bytes)')
