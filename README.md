# mousepass
For write password used your mouse

```bash
yay -S python-pygame
```

```bash
python pass.py
```

<img src="https://github.com/oditynet/mousepass/blob/main/screen.png" title="example" width="800" />

And screensave on graphics pass
F3 - load your password
F2 - unlock screen
default keyboard pass is "123"

C code:

```bash
gcc mousepass.c -o mousepass -lX11 -ldbus-1 -I /usr/include/dbus-1.0/ -I /usr/lib32/dbus-1.0/include/  -I /usr/include/freetype2/  -Wall -O2 -lX11 -lXext -lXft -lImlib2 -lm
```
<img src="https://github.com/oditynet/mousepass/blob/main/image.gif" title="example" width="800" />
