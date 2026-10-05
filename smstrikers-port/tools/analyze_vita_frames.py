#!/usr/bin/env python3
"""Validate finite native-device frame captures; report elapsed rate and tails."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

def analyze(path, expected=None, require_live_play=False):
    path=Path(path)
    data=path.read_bytes()
    lines=data.decode("utf-8").splitlines()
    comments=[s for s in lines if s.startswith("#")]
    live_play_only="# play_only=1 counter=live_play" in comments
    if require_live_play and not live_play_only:
        raise ValueError("Capture does not establish live-play state")
    reader=csv.DictReader(s for s in lines if s and not s.startswith("#"))
    fields=["sample","match","match_frame","frame_us","tasks_us","present_us","sleep_us"]
    if reader.fieldnames!=fields:
        raise ValueError("Unexpected capture columns")
    rows=[]
    for r in reader:
        if set(r)!=set(fields) or any(r[k] is None for k in fields):
            raise ValueError("Malformed row")
        try:r={k:int(v) for k,v in r.items()}
        except ValueError as e:raise ValueError("Non-integer capture value") from e
        if r["sample"]!=len(rows) or r["match"] not in (0,1) or r["frame_us"]<=0 or any(v<0 for v in r.values()):
            raise ValueError("Invalid sample sequence/duration")
        if r["frame_us"]+2<r["tasks_us"]+r["present_us"]+r["sleep_us"]:
            raise ValueError("Phases exceed whole-frame time")
        if rows and r["match"]==rows[-1]["match"]==1 and r["match_frame"]!=rows[-1]["match_frame"]+1:
            raise ValueError("Nonconsecutive gameplay frames")
        rows.append(r)
    if not rows or (expected is not None and len(rows)!=expected):
        raise ValueError("Incomplete capture")
    frames=sorted(r["frame_us"] for r in rows)
    def percentile(p):return frames[max(0,math.ceil(len(frames)*p)-1)]/1000.
    return {"path":str(path.resolve()),"sha256":hashlib.sha256(data).hexdigest(),"metadata":comments,
            "samples":len(rows),"all_gameplay":all(r["match"]==1 for r in rows),"live_play_only":live_play_only,
            "match_frame_first":rows[0]["match_frame"],"match_frame_last":rows[-1]["match_frame"],
            "elapsed_fps":len(rows)*1e6/sum(frames),"mean_ms":statistics.mean(frames)/1000.,
            "median_ms":statistics.median(frames)/1000.,"p95_ms":percentile(.95),"p99_ms":percentile(.99),
            "deadline_60_pct":100*sum(t<=16667 for t in frames)/len(frames),
            "deadline_30_pct":100*sum(t<=33333 for t in frames)/len(frames),
            "tasks_median_ms":statistics.median(r["tasks_us"] for r in rows)/1000.,
            "present_median_ms":statistics.median(r["present_us"] for r in rows)/1000.}

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("paths",type=Path,nargs="+");p.add_argument("--expected",type=int);p.add_argument("--out",type=Path)
    p.add_argument("--require-live-play",action="store_true")
    a=p.parse_args();result=[analyze(path,a.expected,a.require_live_play) for path in a.paths]
    text=json.dumps(result,indent=2)+"\n"
    if a.out:a.out.write_text(text)
    print(text)
