"""Bounded Strikers GLT/GLG adapter for Aurora's versioned native asset compiler.

Original assets and all callbacks remain unchanged. Unsupported/animated meshes
keep the GX path. No lossy texture recompression or material replacement.
"""
from __future__ import annotations
import json
from pathlib import Path
import struct
import subprocess
import tempfile

LIMIT = 16 * 1024 * 1024

def u32(b, at): return struct.unpack_from('>I', b, at)[0]
def u16(b, at): return struct.unpack_from('>H', b, at)[0]
def words(*values): return struct.pack('<'+'I'*len(values), *values)

def encoded_size(w, h, fmt, levels):
    tile = {0:(8,8,32),1:(8,4,32),2:(8,4,32),3:(4,4,32),4:(4,4,32),5:(4,4,32),6:(4,4,64),
            7:(8,8,32),8:(8,4,32),9:(4,4,32),10:(8,8,32)}[fmt]
    tw, th, size = tile
    return sum(((max(1,w>>n)+tw-1)//tw)*((max(1,h>>n)+th-1)//th)*size for n in range(levels))

def texture_requests(data):
    if len(data)<32: raise ValueError('truncated GLT header')
    count=u32(data,4)
    headers=[header for header in (32,16) if header+count*16<=len(data) and
             all(u32(data,header+i*16+4)+u32(data,header+i*16+8)<=len(data)-header-count*16
                 for i in range(count))]
    if not headers: raise ValueError('invalid GLT dictionary')
    header=headers[0];base=header+count*16
    mapping={0:4,1:5,2:10,3:6,4:1,5:0,6:1,7:3}
    for i in range(count):
        at=header+i*16;off,size=u32(data,at+4),u32(data,at+8)
        if off>len(data)-base or size<32 or size>len(data)-base-off: raise ValueError('invalid GLT entry')
        tex=data[base+off:base+off+size]
        levels,original=u32(tex,0),u32(tex,4);w,h=u16(tex,14),u16(tex,16);pal=u32(tex,20)
        if original not in mapping or pal or not w or not h or w>4096 or h>4096 or not 1<=levels<=13: continue
        fmt=mapping[original];n=encoded_size(w,h,fmt,levels)
        if n>len(tex)-32: continue # Legacy 16/24-byte texture headers use the original runtime path.
        yield words(1,w,h,fmt,0,levels,0,n,0)+tex[32:32+n]

def tpl_requests(data):
    """Nintendo TPL images and exact palettes, including mip-prefix bindings."""
    if len(data)<12 or u32(data,0)!=0x0020af30: raise ValueError('invalid TPL header')
    count,table=u32(data,4),u32(data,8)
    if count>65536 or table>len(data) or count*8>len(data)-table: raise ValueError('invalid TPL table')
    mapping={**{n:n for n in range(7)},8:7,9:8,10:9,14:10}
    for i in range(count):
        image,palette=u32(data,table+i*8),u32(data,table+i*8+4)
        if image>len(data) or len(data)-image<36: raise ValueError('truncated TPL image')
        h,w=u16(data,image),u16(data,image+2);original=u32(data,image+4);pixels=u32(data,image+8)
        levels=1+data[image+34];pf=0;pal=b''
        if original not in mapping or not 0<w<=4096 or not 0<h<=4096 or levels>13: raise ValueError('unsupported TPL image')
        if palette:
            if palette>len(data) or len(data)-palette<12: raise ValueError('truncated TPL palette')
            entries=u16(data,palette);pf=u32(data,palette+4)+1;at=u32(data,palette+8)
            if pf>3 or at>len(data) or entries*2>len(data)-at: raise ValueError('invalid TPL palette extent')
            pal=data[at:at+entries*2]
        fmt=mapping[original]
        if fmt in (7,8,9) and not pal: raise ValueError('missing TPL palette')
        for level in range(1,levels+1):
            n=encoded_size(w,h,fmt,level)
            if pixels>len(data) or n>len(data)-pixels: raise ValueError('truncated TPL pixels')
            yield words(1,w,h,fmt,pf,level,0,n,len(pal))+data[pixels:pixels+n]+pal

def chunks(data, start, end):
    while start<end:
        if end-start<8: raise ValueError('truncated GLG chunk')
        chunk_id,size=u32(data,start),u32(data,start+4);next_at=start+8+size
        if next_at>end: raise ValueError('GLG chunk outside container')
        shift=(chunk_id>>24)&127
        if shift>16: raise ValueError('invalid GLG alignment')
        payload=start+8
        if shift: payload=(payload+(1<<shift)-1)&~((1<<shift)-1)
        if payload>next_at: raise ValueError('GLG aligned payload outside chunk')
        yield chunk_id&~0x7f000000,payload,next_at,start+8
        start=next_at

def geometry_requests(data):
    if len(data)<8: raise ValueError('truncated GLG')
    first=next(chunks(data,0,len(data)))
    outer_start,outer_end=(8,first[2]) if first[0]==0x8001b100 else (0,len(data))
    # GX attribute order, preserving glx_SwitchStreams and disc array bounds.
    for outer,payload,end,raw_start in chunks(data,outer_start,outer_end):
        if outer!=0x8001b000: raise ValueError('unexpected GLG container')
        sections={kind:data[a:b] for kind,a,b,_ in chunks(data,raw_start,end)}
        if any(kind in sections for kind in (0x8001b008,0x1b011)): continue
        if not all(kind in sections for kind in (0x1b004,0x1b005,0x1b006,0x1b007)): continue
        packets,streams,arrays,indices=(sections[k] for k in (0x1b004,0x1b005,0x1b006,0x1b007))
        if len(packets)%74 or len(streams)%6: raise ValueError('invalid GLG packet/stream table')
        offsets=sorted({u32(streams,i) for i in range(0,len(streams),6)})
        bounds={off:next((x for x in offsets if x>off),len(arrays)) for off in offsets}
        for p in range(0,len(packets),74):
            count=u16(packets,p+8);prim=packets[p+10];nstreams=packets[p+11];streamoff=u32(packets,p+12);indexoff=u32(packets,p+4)
            if not count or prim>3 or not 1<=nstreams<=9 or streamoff%6 or streamoff+nstreams*6>len(streams): continue
            if indexoff+count*2>len(indices): raise ValueError('GLG index range')
            selected=[];seen=set();skip=False
            for at in range(streamoff,streamoff+nstreams*6,6):
                off,sid,stride=u32(streams,at),streams[at+4],streams[at+5]
                if sid>8 or sid in seen or off>=len(arrays) or bounds[off]<=off: skip=True;break
                seen.add(sid)
                semantic=9 if sid==0 else 10 if sid==1 else 13 if sid==2 else 15+sid-3
                component,components,frac=(4,3,0) if sid in (0,1) and stride==12 else (3,3,8) if sid==0 else (1,3,6) if sid==1 else (10,4,0) if sid==2 else (3,2,10)
                selected.append((semantic,component,components,frac,stride,arrays[off:bounds[off]]))
            if skip or 0 not in seen: continue
            selected.sort(key=lambda item:item[0])
            stride=2*nstreams;raw=b''.join(indices[indexoff+i*2:indexoff+i*2+2]*nstreams for i in range(count))
            # glx dlMakeDisplayList repeats the same model-wide index per stream.
            request=words(2,count,{0:1,1:2,2:3,3:0}[prim],stride,0,len(selected),len(raw))
            for i,(semantic,component,components,frac,array_stride,array) in enumerate(selected):
                request+=words(semantic,3,component,components,frac,i*2,0,array_stride,0,len(array))+array
            request+=raw
            if len(request)<=LIMIT: yield request

def prepare_native(roots: list[Path], compiler: Path, workspace: Path, *, include_rgba=False,
                   gpu_static=False) -> tuple[Path,dict]:
    compiler=compiler.resolve()
    if not compiler.is_file(): raise ValueError('native compiler is missing')
    output=workspace/'native';output.mkdir()
    requested=0;skipped=0;bundles=[]
    with tempfile.TemporaryDirectory(prefix='native-requests-',dir=workspace) as tmp:
        temp=Path(tmp)
        for root in roots:
            for asset in sorted(root.rglob('*')):
                if not asset.is_file() or asset.suffix.lower() not in ('.glt','.glg','.tpl'): continue
                if asset.stat().st_size>LIMIT: skipped+=1;continue
                data=asset.read_bytes();before=requested
                parser={'.glt':texture_requests,'.glg':geometry_requests,'.tpl':tpl_requests}[asset.suffix.lower()]
                iterator=parser(data)
                reason=None
                try:
                    for request in iterator:
                        (temp/f'{requested:06d}.avrq').write_bytes(request);requested+=1
                except ValueError as exc:
                    # Legacy/foreign layouts retain original GX bytes. Do not
                    # publish a partially converted malformed bundle.
                    reason=str(exc)
                    for number in range(before,requested): (temp/f'{number:06d}.avrq').unlink()
                    requested=before
                bundles.append({'path':asset.relative_to(root).as_posix(),'requests':requested-before,'fallback_reason':reason})
        command=[str(compiler),str(temp),str(workspace)]
        if include_rgba: command.append('--include-rgba')
        if gpu_static: command.append('--gpu-static')
        result=subprocess.run(command,capture_output=True,text=True,check=True)
    report=json.loads(result.stdout)
    report.update({'version':1,'requested':requested,'skipped_large':skipped,'bundles':bundles,'include_rgba':include_rgba,
                   'gpu_static':gpu_static,
                   'policy':'exact source match; original assets retained; canonical static GLG and native GLT layouts'})
    return output,report
