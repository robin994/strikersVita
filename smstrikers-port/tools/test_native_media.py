"""Native media contracts, using synthetic assets and an independent PCM oracle."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from asset_pipeline.media import audio_request,fnv,prepare_shader_cache,bank_requests,dsp_requests
from asset_pipeline.video import thp_info,thp_frames,access_units

COMPILER=None

def pcm_reference(raw,coef,y1,y2):
    result=[]
    for off in range(0,len(raw),8):
        source=raw[off:off+8];start=(y1,y2);values=[]
        c1,c2=coef[((source[0]>>4)&7)*2:((source[0]>>4)&7)*2+2]
        for packed in source[1:]:
            for nibble in (packed>>4,packed&15):
                nibble=nibble-16 if nibble>=8 else nibble
                value=nibble*(2**(source[0]&15))*2048+c1*y1+c2*y2+1024
                value=((value+(1<<31))%(1<<32))-(1<<31)
                value=max(-32768,min(32767,value//2048))
                values.append(value);y2,y1=y1,value
        result.append((start,values))
    return result

class NativeMediaTests(unittest.TestCase):
    def test_pcm_matches_wrapping_saturation_history_and_split(self):
        if COMPILER is None:self.skipTest('compiler argument required')
        coef=[32767,-32768,2048,0,-2048,1024,0,0]*2
        raw=bytes([0x0f,0x87,0x8f,0x78,0xff,0x80,0x70,0x0f])*1025
        expected=pcm_reference(raw,coef,-32768,32767)
        with tempfile.TemporaryDirectory() as name:
            work=Path(name);(work/'req').mkdir();(work/'out').mkdir()
            (work/'req/source.awq').write_bytes(audio_request(raw,1025*14-3,coef,-32768,32767))
            subprocess.run([str(COMPILER),str(work/'req'),str(work/'out')],check=True,stdout=subprocess.PIPE)
            records=sorted((work/'out').glob('*.spcm'),key=lambda p:int(p.stem.rsplit('-',1)[1]))
            self.assertEqual(len(records),2);start=0
            for p in records:
                b=p.read_bytes();source_bytes,samples=struct.unpack_from('<II',b,8);count=(samples+13)//14
                self.assertEqual(fnv(b[:-8]),struct.unpack_from('<Q',b,len(b)-8)[0]);self.assertEqual(source_bytes,count*8)
                self.assertEqual(struct.unpack_from('<hh',b,48),expected[start][0])
                for i in range(count):
                    at=52+source_bytes+i*32
                    self.assertEqual(struct.unpack_from('<hh',b,at),expected[start+i][0])
                    self.assertEqual(list(struct.unpack_from('<14h',b,at+4)),expected[start+i][1])
                start+=count
            self.assertEqual(start,1025)
    def test_bank_loop_uses_current_mixer_history_order(self):
        directory=bytearray(74);struct.pack_into('>H',directory,0,5)
        struct.pack_into('>I',directory,16,28);struct.pack_into('>III',directory,20,14,14,32)
        struct.pack_into('>hh',directory,36,123,-456);struct.pack_into('>16h',directory,40,*range(16))
        directory[72:74]=b'\xff\xff'
        # Terminator follows the entry, before the extra-data region.
        directory[32:34]=b'\xff\xff'
        rows=list(bank_requests(directory,bytes(16)));self.assertEqual(len(rows),2)
        self.assertEqual(struct.unpack_from('<hh',rows[0][1],48),(0,0))
        self.assertEqual(struct.unpack_from('<hh',rows[1][1],48),(-456,123))
    def test_dsp_channels_and_malformed_interleave(self):
        header=bytearray(96);struct.pack_into('>III',header,0,14,16,32000)
        struct.pack_into('>hh',header,64,-2,3)
        b=b'IDSP'+struct.pack('>II',8,8)+header+header+bytes(range(8))+bytes(range(8,16))
        rows=list(dsp_requests(b));self.assertEqual(len(rows),2)
        self.assertEqual(rows[1][1][52:],bytes(range(8,16)))
        self.assertEqual(list(dsp_requests(b+bytes(range(64)))),rows)
        partial=bytearray(b);struct.pack_into('>I',partial,12,15);struct.pack_into('>I',partial,108,15)
        partial_rows=list(dsp_requests(partial+bytes(64)))
        self.assertEqual(partial_rows[0][0]['prepared_samples'],14)
        self.assertEqual(struct.unpack_from('<I',partial_rows[0][1],12)[0],14)
        with self.assertRaises(ValueError):list(dsp_requests(b[:-1]))
    def test_gxp_abi_stage_source_hash_and_payload_validation(self):
        with tempfile.TemporaryDirectory() as name:
            work=Path(name);cache=work/'cache';cache.mkdir();workspace=work/'work';workspace.mkdir()
            payload=bytes(range(16));path=cache/'f-0000000000000042.gxp'
            header=struct.pack('<IIQQII',0x41564758,1,0x42,fnv(payload),16,2)
            path.write_bytes(header+payload);self.assertEqual(prepare_shader_cache(cache,workspace)['programs'],1)
            self.assertEqual((workspace/'native/v1/shaders/gxm-cg-gxp-v1'/path.name).read_bytes(),header+payload)
            path.write_bytes(header+payload[:-1]+b'\xff')
            with self.assertRaises(ValueError):prepare_shader_cache(cache,workspace)
            path.write_bytes(struct.pack('<IIQQII',0x41564758,1,0x42,fnv(payload),16,1)+payload)
            with self.assertRaises(ValueError):prepare_shader_cache(cache,workspace)
    def test_thp_envelope_preserves_audio_and_rejects_frame_extents(self):
        head=bytearray(96);head[:4]=b'THP\0';struct.pack_into('>I',head,4,0x11000)
        struct.pack_into('>f',head,16,29.97)
        for at,value in ((20,1),(24,32),(28,32),(32,48),(40,96),(44,96)):struct.pack_into('>I',head,at,value)
        struct.pack_into('>I',head,48,2);head[52:54]=b'\0\1';struct.pack_into('>II',head,68,16,16)
        data=head+struct.pack('>IIII',32,0,4,4)+b'jpeg'+b'dsp!'+bytes(8)
        info=thp_info(data);frames=list(thp_frames(data,info));self.assertEqual(frames[0][2],[(112,4),(116,4)])
        with self.assertRaises(ValueError):list(thp_frames(data[:-1],info))
        with self.assertRaises(ValueError):list(access_units(b'\0\0\0\1\x09\xf0\0\0\0\1\x61'))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--audio-compiler',type=Path)
    args,rest=parser.parse_known_args();COMPILER=args.audio_compiler
    unittest.main(argv=[__file__,*rest])
