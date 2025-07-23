# mouse password

Wiki  - https://github.com/oditynet/mousepassword/wiki

(it is a work program , but only for funny used. Security pass save in json file :) )

F3 - load your password

F2 - unlock screen

default keyboard pass is "123"

C code:

```bash
gcc mousepass.c -o mousepass -lX11 -ldbus-1 -I /usr/include/dbus-1.0/ -I /usr/lib32/dbus-1.0/include/  -I /usr/include/freetype2/  -Wall -O2 -lX11 -lXext -lXft -lImlib2 -lm
```
Result:

<img src="https://github.com/oditynet/mousepass/blob/main/image.gif" title="example" width="800" />

Folow code is a test logics on a python code

```bash
yay -S python-pygame
```

```bash
python pass.py
```

<img src="https://github.com/oditynet/mousepass/blob/main/screen.png" title="example" width="800" />

2) It is a work program screensave on graphics pass
   
