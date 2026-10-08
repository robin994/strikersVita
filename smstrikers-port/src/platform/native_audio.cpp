#if defined(PORT_VITA)
#include "port/native_audio.hpp"
#include "port/native_audio_record.hpp"
#include "port/asset_archive.h"
#include "port/config.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <string>
namespace {
constexpr size_t Budget=2*1024*1024;
struct Entry{uint64_t key;uint32_t size;std::array<uint8_t,32> digest;};
struct Record{std::vector<uint8_t> bytes;port::NativeAudioRecord view;};
struct Binding{uintptr_t base;std::shared_ptr<Record> record;};
struct Cached{std::array<uint8_t,32> digest;std::shared_ptr<Record> record;};
PortAssetArchive* archive=nullptr;std::vector<Entry> entries;std::list<Cached> records;
std::vector<Binding> bindings;std::mutex audioMutex;size_t used=0;uint64_t hits=0,misses=0,rejected=0;
bool read(const char* path,std::vector<uint8_t>& bytes,size_t maximum){
 const int entry=archive?port_asset_archive_find(archive,path):-1;if(entry<0)return false;
 const uint64_t size=port_asset_archive_size(archive,entry);if(!size||size>maximum)return false;
 bytes.resize(size);return port_asset_archive_read(archive,entry,bytes.data(),bytes.size(),0)==long(bytes.size());
}
std::shared_ptr<Record> load(const Entry& entry){
 for(auto it=records.begin();it!=records.end();++it)if(it->digest==entry.digest){auto out=it->record;records.splice(records.begin(),records,it);return out;}
 constexpr char digits[]="0123456789abcdef";std::string path="native/v1/audio/";path.reserve(84);
 for(auto c:entry.digest){path+=digits[c>>4];path+=digits[c&15];}path+=".spcm";
 auto out=std::make_shared<Record>();if(!read(path.c_str(),out->bytes,64*1024)||out->bytes.size()!=entry.size||!out->view.parse(out->bytes)){++rejected;return {};}
 while(used+out->bytes.size()>Budget&&!records.empty()){
  const auto old=records.back().record;bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const Binding& b){return b.record==old;}),bindings.end());used-=old->bytes.size();records.pop_back();
 }
 used+=out->bytes.size();records.push_front({entry.digest,out});return out;
}
bool match(const Binding& binding,const uint8_t* source,const int16_t* coef,int16_t y1,int16_t y2,int16_t* pcm){
 const auto address=reinterpret_cast<uintptr_t>(source);const auto& view=binding.record->view;
 if(address<binding.base||address-binding.base>=view.sourceBytes||(address-binding.base)%8)return false;
 if(!view.frame((address-binding.base)/8,source,coef,y1,y2,pcm))return false;
 ++hits;
 if(PortDiagnosticsEnabled()&&(hits<=4||(hits&(hits-1))==0))
  std::fprintf(stderr,"[native-audio] block_hits=%llu misses=%llu rejected=%llu cache_bytes=%u\n",(unsigned long long)hits,(unsigned long long)misses,(unsigned long long)rejected,unsigned(used));
 return true;
}
void clear(){entries.clear();records.clear();bindings.clear();used=0;if(archive)port_asset_archive_close(archive);archive=nullptr;}
}
void PortNativeAudioInitialize() noexcept {
 std::lock_guard<std::mutex> lock(audioMutex);clear();hits=misses=rejected=0;
 const char* enable=std::getenv("STRIKERS_VITA_NATIVE_AUDIO");if(!enable||std::strcmp(enable,"1"))return;
 const char* path=std::getenv("STRIKERS_NATIVE_ASSET_ARCHIVE");if(!path||!*path)path=std::getenv("STRIKERS_ASSET_ARCHIVE");if(!path||!*path)return;
 char error[256];archive=port_asset_archive_open(path,error,sizeof error);std::vector<uint8_t> b;
 if(!archive||!read("native/v1/audio-index.bin",b,4*1024*1024)||b.size()<16||std::memcmp(b.data(),"STAIDX01",8)||port::audio_word(b.data()+8)!=1){clear();return;}
 const size_t count=port::audio_word(b.data()+12);if(count>90000||b.size()!=16+count*44){clear();return;}
 entries.reserve(count);
 for(size_t i=0;i<count;++i){const auto* p=b.data()+16+i*44;Entry e{uint64_t(port::audio_word(p))|uint64_t(port::audio_word(p+4))<<32,port::audio_word(p+8),{}};
  if(e.size<60||e.size>64*1024||(!entries.empty()&&e.key<entries.back().key)){clear();return;}std::copy_n(p+12,32,e.digest.begin());entries.push_back(e);
 }
 bindings.reserve(128);std::fprintf(stderr,"[native-audio] indexed_blocks=%u catalogue_bytes=%u budget=%u exact_frame_validation=1\n",unsigned(entries.size()),unsigned(entries.size()*sizeof(Entry)),unsigned(Budget));
}
void PortNativeAudioShutdown() noexcept {
 std::lock_guard<std::mutex> lock(audioMutex);if(archive)std::fprintf(stderr,"[native-audio] block_hits=%llu misses=%llu rejected=%llu cache_bytes=%u\n",(unsigned long long)hits,(unsigned long long)misses,(unsigned long long)rejected,unsigned(used));clear();
}
bool PortNativeAudioBlock(const uint8_t* source,const int16_t* coef,int16_t y1,int16_t y2,int16_t* pcm) noexcept {
 if(!source||!coef||!pcm||!archive)return false;
 std::lock_guard<std::mutex> lock(audioMutex);
 for(const auto& binding:bindings)if(match(binding,source,coef,y1,y2,pcm))return true;
 const auto key=port::audio_frame_key(source,coef,y1,y2);
 auto it=std::lower_bound(entries.begin(),entries.end(),key,[](const Entry& e,uint64_t k){return e.key<k;});
 for(unsigned tried=0;it!=entries.end()&&it->key==key&&tried<8;++it,++tried){auto record=load(*it);if(!record)continue;Binding binding{reinterpret_cast<uintptr_t>(source),record};if(!match(binding,source,coef,y1,y2,pcm))continue;
  if(bindings.size()>=128)bindings.erase(bindings.begin());bindings.push_back(std::move(binding));return true;
 }
 ++misses;return false;
}
#endif
