from PIL import Image, ImageDraw
img=Image.new('RGBA',(512,512),(255,255,255,255))
d=ImageDraw.Draw(img)
d.ellipse((64,64,448,448), fill=(255,0,0,255))
img.save('assets/test_rocket.png')
print('test image saved at assets/test_rocket.png')
