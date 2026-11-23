#!/usr/bin/env python3
"""Render five compact, translucent drinking-glass icons for MediaBloom controls.

No external Python packages; assets generated only inside the disposable
MediaBloom build checkout. The same icons are used on Music and Video buttons.
WidgetKit ultimately decides tint/appearance in Control Center.
"""
from __future__ import annotations

import json
import math
import struct
import zlib
from pathlib import Path

TARGET = Path("ManzanaExpiryWidget/MediaBloomGlass.xcassets")
STEPS = (0, 25, 50, 75, 100)
ACTIONS = ("Plus", "Minus", "Mute")
SIZE = 112
AA = 3


def segment_distance(px, py, ax, ay, bx, by):
    vx, vy = bx-ax, by-ay
    t = min(1.0, max(0.0, ((px-ax)*vx+(py-ay)*vy)/(vx*vx+vy*vy)))
    return math.hypot(px-ax-t*vx, py-ay-t*vy)


def paint(level: int, action: str) -> bytes:
    # Premultiplied-ish grayscale with alpha coverage: monochrome line work
    # is adaptable to the system accent color and remains legible at 25 px.
    n = SIZE * AA
    rgba = bytearray(n*n*4)
    top, bottom = 18.0, 101.0
    for iy in range(n):
        y=(iy+.5)/AA
        for ix in range(n):
            x=(ix+.5)/AA
            left = 24.5+(y-top)*0.15
            right=87.5-(y-top)*0.15
            # Cup sides, rim, and short curved base. No tiny text/shading.
            edge=min(
                segment_distance(x,y,24.5,top,37.0,bottom),
                segment_distance(x,y,87.5,top,75.0,bottom),
                segment_distance(x,y,37,bottom,75,bottom),
                segment_distance(x,y,24.5,top,87.5,top)
            )
            outline = max(0.0,min(1.0,(2.1-edge)*1.25))
            # Narrow 3 px inset ensures liquid does not cover glass outline.
            inside=(top+3 < y < bottom-3 and left+3 < x < right-3)
            fill=0.0
            if inside and level>0:
                # Gently curved surface suggests water, not a rectangular bar.
                height=(bottom-4)-(top+4)
                surface=(bottom-4)-height*(level/100)
                surface+=0.7*math.sin((x-24)/9.0)
                if y>=surface: fill=0.94
            alpha=max(outline*.95,fill*.93)
            # Distinguish +, − and mute even at small Control Center sizes.
            # Badge is lower-right, with 5-level fill remaining unobstructed.
            badgeDistance=math.hypot(x-90.0,y-85.0)
            badge=badgeDistance<=17.0
            thick=3.4
            symbol=False
            if action in ("Plus","Minus") and abs(y-85.0)<thick and 81.0<x<100.0:
                symbol=True
            if action=="Plus" and abs(x-90.0)<thick and 76.0<y<94.0:
                symbol=True
            if action=="Mute" and abs((x-90.0)+(y-85.0))<4.0 and 79.0<x<101.0:
                symbol=True
            if badge: alpha=max(alpha,0.95)
            if alpha<=0: continue
            off=(iy*n+ix)*4
            # Pale violet, low-noise; iOS may adopt it as a monochrome glyph.
            if badge and symbol:
                val=(255,255,255)
            elif badge:
                val=(93,57,166)
            else:
                val=(232,219,255) if fill>outline else (252,249,255)
            rgba[off:off+4]=bytes((*val,round(255*alpha)))
    # Average supersampled subpixels into an RGBA raster.
    out=bytearray(SIZE*SIZE*4)
    for y in range(SIZE):
        for x in range(SIZE):
            sums=[0,0,0,0]
            for dy in range(AA):
                for dx in range(AA):
                    o=(((y*AA+dy)*n)+(x*AA+dx))*4
                    for ch in range(4): sums[ch]+=rgba[o+ch]
            p=(y*SIZE+x)*4
            for ch in range(4):out[p+ch]=round(sums[ch]/(AA*AA))
    return png_rgba(SIZE,SIZE,out)


def chunk(name: bytes, data: bytes) -> bytes:
    return (struct.pack("!I",len(data))+name+data+
            struct.pack("!I",zlib.crc32(name+data)&0xffffffff))


def png_rgba(width: int,height: int,pixels: bytes) -> bytes:
    scan=b"".join(b"\0"+pixels[y*width*4:(y+1)*width*4]
                  for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"+
            chunk(b"IHDR",struct.pack("!IIBBBBB",width,height,8,6,0,0,0))+
            chunk(b"IDAT",zlib.compress(scan,9))+chunk(b"IEND",b""))


def main() -> None:
    TARGET.mkdir(parents=True,exist_ok=True)
    for level in STEPS:
        for action in ACTIONS:
            name=f"MediaBloomGlass{level}{action}"
            folder=TARGET/f"{name}.imageset"
            folder.mkdir(parents=True,exist_ok=True)
            (folder/"glass.png").write_bytes(paint(level,action))
            (folder/"Contents.json").write_text(json.dumps({
                "images":[{"filename":"glass.png","idiom":"universal"}],
                "info":{"author":"xcode","version":1}
            },indent=2)+"\n")
    print("PASS five liquid-fill stages × three action badges for MediaBloom")


if __name__=="__main__":
    main()
