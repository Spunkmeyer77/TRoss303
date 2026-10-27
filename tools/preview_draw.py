import json, sys
from PIL import Image, ImageDraw, ImageFont
ops = json.load(open(sys.argv[1])); dev = sys.argv[2]; out = sys.argv[3]
S = 2
W, H = 760, 169
img = Image.new('RGB', (W*S, H*S), (60,60,60)); d = ImageDraw.Draw(img, 'RGBA')
def col(c): return tuple(int(max(0,min(1,v))*255) for v in c[:3]) + (int(c[3]*255),)
def fnt(sz, b):
    p = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if b else '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    try: return ImageFont.truetype(p, int(sz*S*0.95))
    except: return ImageFont.load_default()
for o in ops:
    if o['t'] in ('rect','rrect','ell'):
        x,y,w,h = [v*S for v in (o['x'],o['y'],o['w'],o['h'])]
        if o.get('grad'):
            g=o['grad']; st=g['stops']
            for yy in range(int(y), int(y+h)):
                t = (yy/S - g['y0'])/max(1e-6,(g['y1']-g['y0'])); t=max(0,min(1,t))
                c=[st[0][1][i]+(st[-1][1][i]-st[0][1][i])*t for i in range(4)]
                d.line([(x,yy),(x+w,yy)], fill=col(c))
            continue
        c = col(o['col'])
        if o['t']=='rect': d.rectangle([x,y,x+w,y+h], fill=c)
        elif o['t']=='rrect': d.rounded_rectangle([x,y,x+w,y+h], radius=o['r']*S, fill=c)
        else: d.ellipse([x,y,x+w,y+h], fill=c)
    else:
        f = fnt(o['size'], o['bold']); bb = d.textbbox((0,0), o['s'], font=f)
        d.text((o['x']*S, o['y']*S - (bb[3])), o['s'], font=f, fill=col(o['col']))
# overlay live.* objects
raw = open(dev,'rb').read(); j = json.loads(raw[32:].rstrip(b'\x00\n').decode())
for b in j['patcher']['boxes']:
    b=b['box']
    if b['maxclass'].startswith('live.') and 'presentation_rect' in b:
        x,y,w,h=[v*S for v in b['presentation_rect']]
        mc=b['maxclass']; name=b['saved_attribute_attributes']['valueof']['parameter_longname']
        if mc=='live.dial':
            r=min(w,h)*0.42; cx=x+w/2; cy=y+h/2-2
            d.arc([cx-r,cy-r,cx+r,cy+r],135,405,fill=(45,45,48),width=int(3*S))
            d.arc([cx-r,cy-r,cx+r,cy+r],135,270,fill=(204,51,25),width=int(3*S))
            d.line([cx,cy,cx,cy-r],fill=(240,240,235),width=S)
        else:
            d.rounded_rectangle([x,y,x+w,y+h],radius=2*S,fill=(43,43,46),outline=(204,51,25))
            v=b['saved_attribute_attributes']['valueof']
            lab = b.get('text') or (v.get('parameter_enum') or [''])[ int((v.get('parameter_initial') or [0])[0]) if v.get('parameter_enum') else 0] or str((v.get('parameter_initial') or [''])[0])
            if mc=='live.tab': lab='SAW | SQR'
            f=fnt(7,True); d.text((x+3*S,y+2*S),str(lab),font=f,fill=(235,235,230))
img.save(out)
