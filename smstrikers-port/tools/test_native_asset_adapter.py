from asset_pipeline.native import texture_requests, geometry_requests, tpl_requests
import struct
import unittest

def chunk(kind,data): return struct.pack('>II',kind,len(data))+data
class NativeAdapterTest(unittest.TestCase):
    def test_tpl_palette_mip_prefix_and_bounds(self):
        b=bytearray(132);struct.pack_into('>III',b,0,0x0020af30,1,12)
        struct.pack_into('>II',b,12,20,56)
        struct.pack_into('>HHII',b,20,8,8,8,68) # CI4 8x8, one 32-byte level
        b[20+34]=1 # plus a 4x4 level, also one padded tile
        struct.pack_into('>H',b,56,2);struct.pack_into('>II',b,60,1,128)
        b[128:132]=b'\xff\xff\x00\x00'
        requests=list(tpl_requests(b));self.assertEqual(len(requests),2)
        self.assertEqual(struct.unpack('<9I',requests[-1][:36]),(1,8,8,7,2,2,0,64,4))
        self.assertEqual(requests[-1][-4:],b[128:132])
        with self.assertRaises(ValueError):list(tpl_requests(b[:-1]))
    def test_texture_bounds_and_explicit_mips(self):
        texture=bytearray(32);struct.pack_into('>II',texture,0,1,4)
        struct.pack_into('>HH',texture,14,8,4)
        texture+=bytes(range(32))
        bundle=bytearray(32);struct.pack_into('>II',bundle,0,0x50544c47,1)
        bundle+=struct.pack('>IIII',1,0,len(texture),0)+texture
        request=list(texture_requests(bundle));self.assertEqual(len(request),1)
        self.assertEqual(struct.unpack('<9I',request[0][:36]),(1,8,4,1,0,1,0,32,0))
        self.assertEqual(request[0][36:],bytes(range(32)))
        with self.assertRaises(ValueError): list(texture_requests(bundle[:-1]))
    def test_static_geometry_original_indices_and_skin_exclusion(self):
        packet=bytearray(74);struct.pack_into('>HBBI',packet,8,3,0,1,0)
        stream=struct.pack('>IBB',0,0,12)
        positions=struct.pack('>9f',0,0,0,1,0,0,0,1,0)
        indices=struct.pack('>3H',0,1,2)
        sections=b''.join(chunk(k,data) for k,data in [(0x1b004,packet),(0x1b005,stream),(0x1b006,positions),(0x1b007,indices)])
        data=chunk(0x8001b100,chunk(0x8001b000,sections))
        request=list(geometry_requests(data));self.assertEqual(len(request),1)
        self.assertEqual(struct.unpack('<7I',request[0][:28]),(2,3,1,2,0,1,6))
        self.assertEqual(request[0][-6:],indices)
        self.assertIn(positions,request[0])
        data=chunk(0x8001b100,chunk(0x8001b000,sections+chunk(0x8001b008,b'unchanged animation')))
        self.assertEqual(list(geometry_requests(data)),[])
        with self.assertRaises(ValueError): list(geometry_requests(data[:-1]))
if __name__=='__main__': unittest.main()
