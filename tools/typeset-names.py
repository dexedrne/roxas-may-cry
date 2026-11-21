"""Regenerate the release's three isolated subtitle sprites (Pillow maintainer tool)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def render(text, ink):
    font = ImageFont.truetype(str(ROOT/'fonts/GreatVibes-Regular.ttf'), 260)
    left,top,right,bottom = ImageDraw.Draw(Image.new('L',(1,1))).textbbox(
        (0,0),text,font=font,stroke_width=6)
    im = Image.new('RGBA',(right-left+40,bottom-top+40))
    ImageDraw.Draw(im).text((20-left,20-top),text,font=font,fill=ink+(255,),
                            stroke_width=6,stroke_fill=(250,244,238,255))
    im = im.crop(im.getbbox())
    return im.resize((571,round(571*im.height/im.width)),Image.Resampling.LANCZOS)


if __name__=='__main__':
    for name,text in [('roxas','Roxas May Cry'),('xion','Xion Always Cries'),('xkb','Xion Knows Best')]:
        render(text,(110,12,20) if name=='roxas' else (20,46,112)).save(ROOT/'art'/(name+'-name.png'))
