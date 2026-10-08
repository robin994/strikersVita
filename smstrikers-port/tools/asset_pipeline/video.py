"""Reusable THP envelope adapter, shared format with WiiCompiled's WAVC v1.
Video is H.264 Baseline IDR; original audio, frame count/rate and dimensions stay exact.
Derived from wiicompiled-vita/scripts/convert_mkw_video.py (2026-10-08).
"""
from __future__ import annotations
import hashlib
from fractions import Fraction
import mmap
from pathlib import Path
import re
import struct
import subprocess
import time

VIDEO_HEADER = struct.Struct(">4sIIIII")  # magic, version, width, height, ES bytes, flags
START = re.compile(b"\x00\x00(?:\x00)?\x01")


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""): h.update(chunk)
    return h.hexdigest()


def u32(data, pos): return struct.unpack_from(">I", data, pos)[0]


def thp_info(data):
    if len(data) < 80 or data[:4] != b"THP\0" or u32(data, 4) not in (0x10000, 0x11000):
        raise ValueError("unsupported THP header")
    info, table, first = (u32(data, p) for p in (32, 36, 40))
    if table or info < 48 or info + 28 > first or first > len(data) or first % 16:
        raise ValueError("unsupported THP component/offset table")
    count = u32(data, info)
    if not 1 <= count <= 2 or data[info+4:info+4+count] != bytes(range(count)):
        raise ValueError("expected video, optionally followed by audio")
    width, height = struct.unpack_from(">II", data, info+20)
    if not 16 <= width <= 704 or not 16 <= height <= 768 or width % 16 or height % 16:
        raise ValueError(f"unsupported geometry {width}x{height}")
    frames = u32(data, 20)
    fps = struct.unpack_from(">f", data, 16)[0]
    if not 0 < frames <= 100000 or not 0 < fps <= 60.01:
        raise ValueError("invalid frame count/rate")
    return dict(count=count, width=width, height=height, frames=frames, fps=fps, first=first)


def thp_frames(data, info):
    pos, size, previous = info["first"], u32(data, 24), None
    first_size = size
    for frame in range(info["frames"]):
        if size < 8+4*info["count"] or pos+size > len(data) or size % 32:
            raise ValueError(f"invalid THP frame extent {frame}")
        next_size, previous_size = struct.unpack_from(">II", data, pos)
        if previous is not None and previous_size != previous:
            raise ValueError(f"invalid previous frame size {frame}")
        sizes = struct.unpack_from(">"+"I"*info["count"], data, pos+8)
        at = pos+8+4*info["count"]
        if sum(sizes) > size-(at-pos) or any(n % 4 for n in sizes):
            raise ValueError("invalid THP component lengths")
        components = []
        for length in sizes:
            components.append((at, length)); at += length
        yield pos, size, components
        previous, pos, size = size, pos+size, next_size
    if size != first_size or pos != len(data) or u32(data, 44) != pos-previous:
        raise ValueError("invalid THP final extent/loop size")


def access_units(data: bytes):
    marks = list(START.finditer(data))
    if not marks or marks[0].start() != 0:
        raise ValueError("invalid Annex B stream")
    starts = [m.start() for m in marks if m.end() < len(data) and data[m.end()] & 31 == 9]
    if not starts or starts[0] != 0:
        raise ValueError("H.264 access units need AUD delimiters")
    starts.append(len(data))
    for first, last in zip(starts, starts[1:]):
        unit = data[first:last]
        types = [unit[m.end()] & 31 for m in START.finditer(unit) if m.end() < len(unit)]
        if not {7, 8, 9, 5}.issubset(types) or 1 in types or len(unit) > 1024*1024-24:
            raise ValueError("each AU must contain SPS/PPS/AUD and an independent IDR")
        for m in START.finditer(unit):
            if unit[m.end()] & 31 == 7 and (m.end()+1>=len(unit) or unit[m.end()+1] != 66):
                raise ValueError("expected AVC Baseline profile")
        yield unit


def convert_clip(source: Path, output: Path, ffmpeg: str, crf: int, threads: int):
    started = time.monotonic()
    with source.open("rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as original:
        info = thp_info(original)
        frames = list(thp_frames(original, info))
        elementary = output.with_suffix(".h264")
        # Keep original full-range YUV values. No frame dropping or audio resampling.
        command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
            # Reconstruct input video timestamps from the floating THP header
            # rate, since its demuxer advertises an integer stream time base.
            "-r", str(Fraction(str(round(info["fps"],3)))),
            "-i", str(source), "-map", "0:v:0", "-an", "-vf",
            "scale=in_range=full:out_range=full,format=yuv420p", "-fps_mode", "passthrough",
            "-c:v", "libx264", "-preset", "fast", "-crf", str(crf), "-threads", str(threads),
            "-profile:v", "baseline", "-level:v", "3.2", "-color_range", "pc",
            "-x264-params", "keyint=1:min-keyint=1:scenecut=0:bframes=0:ref=1:aud=1:repeat-headers=1:fullrange=on:force-cfr=1",
            "-f", "h264", str(elementary)]
        subprocess.run(command, check=True)
        units = list(access_units(elementary.read_bytes()))
        if len(units) != info["frames"]: raise ValueError("conversion changed the frame count")
        sizes = []
        for unit, (_, _, parts) in zip(units, frames):
            video_size = (VIDEO_HEADER.size+len(unit)+3) & ~3
            size = 8+4*info["count"]+video_size+sum(n for _, n in parts[1:])
            sizes.append((size+31) & ~31)
        prefix = bytearray(original[:info["first"]])
        for offset, value in ((8,max(sizes)),(24,sizes[0]),(28,sum(sizes)),
                              (44,info["first"]+sum(sizes[:-1]))):
            struct.pack_into(">I", prefix, offset, value)
        original_audio, converted_audio = hashlib.sha256(), hashlib.sha256()
        with output.open("wb") as target:
            target.write(prefix)
            for i, (unit, (_, _, parts)) in enumerate(zip(units, frames)):
                video = VIDEO_HEADER.pack(b"WAVC",1,info["width"],info["height"],len(unit),1)+unit
                video += b"\0"*((-len(video)) % 4)
                previous = sizes[i-1] if i or u32(original,info["first"]+4) else 0
                record = bytearray(struct.pack(">II", sizes[(i+1)%len(sizes)], previous))
                record += struct.pack(">"+"I"*info["count"], len(video), *(n for _, n in parts[1:]))
                record += video
                for at, length in parts[1:]:
                    audio = original[at:at+length]
                    original_audio.update(audio); record += audio; converted_audio.update(audio)
                record += b"\0"*(sizes[i]-len(record)); target.write(record)
        if original_audio.digest() != converted_audio.digest(): raise ValueError("audio changed")
    with output.open("rb") as f, mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as converted:
        converted_info=thp_info(converted)
        if info != converted_info: raise ValueError("video metadata changed")
        for i, (_, _, parts) in enumerate(thp_frames(converted, info)):
            at, length = parts[0]
            magic, version, w, h, es, flags = VIDEO_HEADER.unpack_from(converted, at)
            if (magic,version,w,h,flags) != (b"WAVC",1,info["width"],info["height"],1) or es+24 > length:
                raise ValueError("invalid converted component")
            if converted[at+24:at+24+es] != units[i]: raise ValueError("H.264 payload changed")
    elementary.unlink()
    return {**info,"source_bytes":source.stat().st_size,"converted_bytes":output.stat().st_size,
            "source_sha256":sha(source),"converted_sha256":sha(output),
            "audio_sha256":original_audio.hexdigest(),"audio_byte_identical":True,
            "seconds":time.monotonic()-started,"encoder_command":command}



def prepare_video(roots, workspace, *, ffmpeg="ffmpeg", crf=18, threads=4):
    if not 0 <= crf <= 30 or not 1 <= threads <= 16:
        raise ValueError("invalid H.264 encoder options")
    dest=workspace/"native/v1/video"
    rows=[];fallbacks=[]
    for root in roots:
        for source in sorted(root.rglob("*.thp")):
            # Runtime paths name files relative to the extracted files/ tree.
            if root.name != "files":
                continue
            name=source.relative_to(root).as_posix()
            output=dest/name; output.parent.mkdir(parents=True,exist_ok=True)
            try:
                if source.stat().st_size > 256*1024*1024:
                    raise ValueError("THP source exceeds runtime bound")
                report=convert_clip(source,output,ffmpeg,crf,threads)
                report["path"]=name;rows.append(report)
            except ValueError as exc:
                output.unlink(missing_ok=True)
                output.with_suffix(".h264").unlink(missing_ok=True)
                fallbacks.append({"path":name,"reason":str(exc)})
    return {"format":"WAVC v1 in original THP envelope","codec":"H264 Baseline all-intra, full range YUV420",
            "crf":crf,"hardware_validated":False,"originals_retained":True,"videos":rows,"fallbacks":fallbacks,
            "source_bytes":sum(r["source_bytes"] for r in rows),"converted_bytes":sum(r["converted_bytes"] for r in rows)}
