// Lossless preparation for the actual Strikers MusyX mixer arithmetic.
#include "port/native_audio_record.hpp"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <sys/stat.h>
#include <dirent.h>
static void put16(std::vector<uint8_t>& b,int16_t v){b.push_back(uint16_t(v)&255);b.push_back(uint16_t(v)>>8);}
int main(int argc,char** argv){
 if(argc!=3){std::fprintf(stderr,"usage: strikers_compile_native_audio REQUEST_DIRECTORY OUTPUT_DIRECTORY\n");return 2;}
 DIR* dir=opendir(argv[1]);if(!dir)return 1;unsigned compiled=0;
 while(const auto* entry=readdir(dir)){
  std::string name=entry->d_name;if(name.size()<4||name.substr(name.size()-4)!=".awq")continue;
  std::ifstream in(std::string(argv[1])+"/"+name,std::ios::binary);std::vector<uint8_t> req((std::istreambuf_iterator<char>(in)),{});
  if(req.size()<52||req.size()>64*1024*1024||std::memcmp(req.data(),"STAUR001",8)){closedir(dir);return 1;}
  const size_t bytes=port::audio_word(req.data()+8),samples=port::audio_word(req.data()+12),frames=(samples+13)/14;
  if(!samples||bytes!=frames*8||req.size()!=52+bytes){closedir(dir);return 1;}
  int16_t y1=port::audio_short(req.data()+48),y2=port::audio_short(req.data()+50);
  for(size_t start=0;start<frames;start+=1024){
   const size_t count=std::min(size_t(1024),frames-start),raw=count*8,total=std::min(count*14,samples-start*14);
   std::vector<uint8_t> out(req.begin(),req.begin()+52);std::memcpy(out.data(),"STPCM001",8);
   for(unsigned i=0;i<4;++i){out[8+i]=uint8_t(raw>>(8*i));out[12+i]=uint8_t(total>>(8*i));}
   out[48]=uint16_t(y1)&255;out[49]=uint16_t(y1)>>8;out[50]=uint16_t(y2)&255;out[51]=uint16_t(y2)>>8;
   out.insert(out.end(),req.data()+52+start*8,req.data()+52+(start+count)*8);out.reserve(52+raw+count*32+8);
   for(size_t frame=start;frame<start+count;++frame){
    put16(out,y1);put16(out,y2);const auto* p=req.data()+52+frame*8;const int32_t scale=1<<(p[0]&15),c1=port::audio_short(req.data()+16+((p[0]>>4)&7)*4),c2=port::audio_short(req.data()+18+((p[0]>>4)&7)*4);
    for(unsigned i=0;i<14;++i){int32_t nib=(i&1)?p[1+i/2]&15:p[1+i/2]>>4;if(nib>=8)nib-=16;
     // The current ARM mixer uses 32-bit accumulation before the arithmetic
     // shift. Unsigned accumulation defines its wrap without signed-shift UB.
     const uint32_t bits=(uint32_t(nib*scale)<<11)+1024u+uint32_t(c1*y1)+uint32_t(c2*y2);int32_t value;std::memcpy(&value,&bits,4);value>>=11;
     const auto pcm=int16_t(std::max(-32768,std::min(32767,value)));put16(out,pcm);y2=y1;y1=pcm;
    }
   }
   const auto hash=port::audio_hash(out.data(),out.size());for(unsigned i=0;i<8;++i)out.push_back(uint8_t(hash>>(8*i)));
   const auto path=std::string(argv[2])+"/"+name.substr(0,name.size()-4)+"-"+std::to_string(start)+".spcm";
   std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),out.size());if(!f){closedir(dir);return 1;}++compiled;
  }
 }
 closedir(dir);std::printf("{\"compiled_audio_blocks\":%u}\n",compiled);
}
