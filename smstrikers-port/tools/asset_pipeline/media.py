"""Strikers-specific PCM preparation and portable checked GXM cache packaging."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

MASK64=(1<<64)-1

def fnv(data):
    h=14695981039346656037
    for v in data: h=((h^v)*1099511628211)&MASK64
    return h

def u32(b,at):return struct.unpack_from('>I',b,at)[0]
def s16(b,at):return struct.unpack_from('>h',b,at)[0]
def extent(b,at,n):
    if at<0 or n<0 or at>len(b) or n>len(b)-at:raise ValueError('audio extent outside source')
    return b[at:at+n]

def audio_request(raw,samples,coefficients,y1=0,y2=0):
    if not 0<samples<=(64*1024*1024//8)*14 or len(coefficients)!=16:raise ValueError('invalid PCM source geometry')
    size=((samples+13)//14)*8
    if len(raw)<size-7:raise ValueError('truncated ADPCM source')
    raw=raw[:size]+bytes(max(0,size-len(raw)))
    return b'STAUR001'+struct.pack('<II16h2h',size,samples,*coefficients,y1,y2)+raw

def bank_requests(directory,samples):
    """On-disc SDIR_DATA_INTER; reproduce the current mixer's zero/loop starts."""
    pos=0;terminated=False
    while pos+2<=len(directory):
        ident=struct.unpack_from('>H',directory,pos)[0]
        if ident==0xffff:terminated=True;break
        entry=extent(directory,pos,32);off=u32(entry,4);packed=u32(entry,16);kind=packed>>24;count=packed&0xffffff;extra=u32(entry,28)
        if kind in (0,1) and count:
            info=extent(directory,extra,40);coef=struct.unpack_from('>16h',info,8);raw=extent(samples,off,((count+13)//14)*8)
            yield {'sample_id':ident,'kind':'bank','start':0},audio_request(raw,count,coef)
            loop=u32(entry,20);loop_length=u32(entry,24)
            if loop_length and loop<count:
                block=loop//14
                yield {'sample_id':ident,'kind':'bank-loop','start':block*14},audio_request(raw[block*8:],count-block*14,coef,s16(info,6),s16(info,4))
        pos+=32
    if not terminated:raise ValueError('missing SDIR terminator')

def dsp_header(b,at):
    head=extent(b,at,96);samples=u32(head,0);rate=u32(head,8)
    if not samples or not 4000<=rate<=192000 or struct.unpack_from('>H',head,14)[0]:raise ValueError('unsupported DSP header')
    return samples,struct.unpack_from('>16h',head,28),s16(head,64),s16(head,66)

def dsp_requests(data):
    if data[:4]==b'IDSP':
        interleave,length=u32(data,4),u32(data,8)
        if not interleave or interleave%8 or not length or length>32*1024*1024:raise ValueError('invalid IDSP interleave')
        headers=[dsp_header(data,12+i*96) for i in range(2)];raw=[bytearray(),bytearray()];pos=204
        for off in range(0,length,interleave):
            n=min(interleave,length-off)
            for channel in range(2):raw[channel]+=extent(data,pos,n);pos+=n
        # GCStream reads exactly StreamLength bytes per channel from offset
        # 0xcc. Some authored files carry an unused 64-byte trailer.
        if len(data)-pos not in (0,64):raise ValueError('unexpected IDSP tail')
        for channel,(samples,coef,y1,y2) in enumerate(headers):
            # The original stereo stream ends at StreamLength, even when its
            # DSP header describes a partial frame beyond that extent.
            prepared_samples=min(samples,(length//8)*14)
            yield {'kind':'idsp','channel':channel,'source_samples':samples,'prepared_samples':prepared_samples},audio_request(raw[channel],prepared_samples,coef,y1,y2)
    else:
        samples,coef,y1,y2=dsp_header(data,0)
        yield {'kind':'dsp','channel':0},audio_request(data[96:],samples,coef,y1,y2)

def prepare_audio(roots,compiler,workspace,*,mode='bank'):
    if mode not in ('bank','all'):raise ValueError('unsupported audio mode')
    compiler=compiler.resolve()
    if not compiler.is_file():raise ValueError('native audio compiler is missing')
    dest=workspace/'native/v1/audio';dest.mkdir(parents=True,exist_ok=True)
    known=set();sources=[];fallback=[]
    with tempfile.TemporaryDirectory(prefix='native-audio-',dir=workspace) as name:
        work=Path(name);requests=work/'requests';compiled=work/'compiled';requests.mkdir();compiled.mkdir()
        for root in roots:
            for path in sorted(root.rglob('*')):
                suffix=path.suffix.lower()
                if not path.is_file() or suffix not in ('.sdir','.dsp','.idsp') or (suffix!='.sdir' and mode=='bank'):continue
                if path.stat().st_size>64*1024*1024:raise ValueError('audio source exceeds limit')
                before=set(known);source_count=len(sources)
                try:
                    data=path.read_bytes()
                    iterator=bank_requests(data,path.with_suffix('.samp').read_bytes()) if suffix=='.sdir' else dsp_requests(data)
                    for meta,req in iterator:
                        digest=hashlib.sha256(req).hexdigest()
                        if digest not in known:(requests/(digest+'.awq')).write_bytes(req);known.add(digest)
                        sources.append({'path':path.relative_to(root).as_posix(),**meta,'request_sha256':digest})
                except (ValueError,OSError,struct.error) as e:
                    for digest in known-before:(requests/(digest+'.awq')).unlink()
                    known=before
                    del sources[source_count:]
                    fallback.append({'path':path.relative_to(root).as_posix(),'reason':str(e)})
        result=subprocess.run([str(compiler),str(requests),str(compiled)],capture_output=True,text=True,check=True)
        records=[];unique=set()
        for path in sorted(compiled.glob('*.spcm')):
            b=path.read_bytes();raw,samples=struct.unpack_from('<II',b,8);frames=(samples+13)//14
            if b[:8]!=b'STPCM001' or not 0<frames<=1024 or raw!=frames*8 or len(b)!=52+raw+frames*32+8 or fnv(b[:-8])!=struct.unpack_from('<Q',b,len(b)-8)[0]:raise ValueError('invalid PCM compiler output')
            digest=hashlib.sha256(b).digest()
            if digest in unique:continue
            target=dest/(digest.hex()+'.spcm')
            if target.exists() and target.read_bytes()!=b:raise ValueError('PCM content collision')
            target.write_bytes(b);unique.add(digest)
            key=fnv(b[52:60]+b[16:48]+b[48:52]);records.append((key,len(b),digest))
        if len(records)>90000:raise ValueError('PCM catalogue exceeds runtime limit')
        records.sort();index=b'STAIDX01'+struct.pack('<II',1,len(records))
        for key,size,digest in records:index+=struct.pack('<QI',key,size)+digest
        (dest.parent/'audio-index.bin').write_bytes(index)
    return {'mode':mode,'requests':len(known),'occurrences':len(sources),'records':len(records),
            'uncompressed_bytes':sum(n for _,n,_ in records),'compiler':json.loads(result.stdout),'sources':sources,'fallbacks':fallback}

def prepare_shader_cache(source,workspace):
    """GXP is compiled on Vita once; the host validates/packages those binaries."""
    source=source.resolve()
    if (source/'gxm-cg-gxp-v1').is_dir():source=source/'gxm-cg-gxp-v1'
    if not source.is_dir():raise ValueError('GXM shader cache directory is missing')
    dest=workspace/'native/v1/shaders/gxm-cg-gxp-v1';dest.mkdir(parents=True,exist_ok=True);rows=[]
    for path in sorted(source.glob('*.gxp')):
        match=re.fullmatch(r'([vf])-([0-9a-f]{16})\.gxp',path.name)
        if not match or path.is_symlink():raise ValueError('invalid GXM cache filename')
        b=path.read_bytes()
        if len(b)<48 or len(b)>4*1024*1024+32:raise ValueError('invalid GXM cache extent')
        magic,version,source_hash,binary_hash,length,stage=struct.unpack_from('<IIQQII',b)
        expected_stage=1 if match[1]=='v' else 2
        if (magic,version,source_hash,stage)!=(0x41564758,1,int(match[2],16),expected_stage) or length!=len(b)-32 or fnv(b[32:])!=binary_hash:raise ValueError('invalid GXM cache header/checksum')
        (dest/path.name).write_bytes(b);rows.append({'name':path.name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
    return {'abi':'aurora-gxm-cg-gxp-v1','programs':len(rows),'bytes':sum(r['bytes'] for r in rows),'files':rows,
            'policy':'Vita compiled programs; source/stage/ABI/checksum and sceGxmProgramCheck at runtime; original compiler fallback'}
