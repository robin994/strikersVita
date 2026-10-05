#include "NL/nlVector.h"
#include "Game/PoseAccumulator.h"
#include "dolphin/mtx.h"
#include "port/audio_sample_view.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

static unsigned failures=0, checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while(0)

// Required by unused rotation helpers in the real platvmath translation unit.
void nlSinCos(float* s,float* c,unsigned short angle) {
    const float radians=float(angle)*(6.283185307179586f/65536.f);
    *s=std::sin(radians);*c=std::cos(radians);
}
struct TestAllocator {
    template<class T> static T* New(int n,const char*) { return new T[n]; }
    template<class T> static void Delete(T* p) { delete[] p; }
};
struct Assigned {
    int value=0;
    static unsigned assignments;
    Assigned& operator=(const Assigned& rhs) { value=rhs.value;++assignments;return *this; }
};
unsigned Assigned::assignments=0;
static_assert(std::is_trivially_copyable<nlMatrix4>::value, "Matrix copy must use the plain-data path");
static_assert(std::is_trivially_copyable<RotAccum>::value, "Rotation accumulator copy must use the plain-data path");
static_assert(std::is_trivially_copyable<ScaleAccum>::value, "Scale accumulator copy must use the plain-data path");
static_assert(std::is_trivially_copyable<TransAccum>::value, "Translation accumulator copy must use the plain-data path");

static void matrix_aliases() {
    for(unsigned n=0;n<256;++n) {
        nlMatrix4 a,b,expected,actual;
        for(unsigned i=0;i<16;++i) {
            a.e[i]=float(int((i*17+n*3)%71)-35)*0.125f;
            b.e[i]=float(int((i*13+n*7)%53)-26)*0.25f;
        }
        if(n==0) a.e[0]=-0.0f;
        C_MTX44Concat(a.e2,b.e2,expected.e2);
        nlMultMatrices(actual,a,b);
        CHECK(std::memcmp(&actual,&expected,sizeof(actual))==0);
        actual=a;nlMultMatrices(actual,actual,b);
        CHECK(std::memcmp(&actual,&expected,sizeof(actual))==0);
        actual=b;nlMultMatrices(actual,a,actual);
        CHECK(std::memcmp(&actual,&expected,sizeof(actual))==0);
        C_MTX44Concat(a.e2,a.e2,expected.e2);
        actual=a;nlMultMatrices(actual,actual,actual);
        CHECK(std::memcmp(&actual,&expected,sizeof(actual))==0);
    }
}
static void vector_copies() {
    Vector<nlMatrix4,TestAllocator> src(37,"test"),dst(50,"test");
    for(int i=0;i<src.mSize;++i)for(unsigned j=0;j<16;++j)src.mData[i].e[j]=float(i*16+j);
    auto* storage=dst.mData;
    dst=src;
    CHECK(dst.mData==storage&&dst.mSize==37&&dst.mCapacity==50);
    CHECK(std::memcmp(dst.mData,src.mData,sizeof(nlMatrix4)*37)==0);
    dst=dst;CHECK(dst.mData==storage&&dst.mSize==37);
    Vector<nlMatrix4,TestAllocator> copy(src);
    CHECK(copy.mData!=src.mData&&std::memcmp(copy.mData,src.mData,sizeof(nlMatrix4)*37)==0);
    Vector<nlMatrix4,TestAllocator> empty(0,"empty");
    dst=empty;CHECK(dst.mSize==0&&dst.mCapacity==50);
    dst=src; // Preserve the existing growth rule based on mSize, not capacity.
    CHECK(dst.mSize==37&&dst.mCapacity==37);
    CHECK(std::memcmp(dst.mData,src.mData,sizeof(nlMatrix4)*37)==0);
    Vector<Assigned,TestAllocator> objects(7,"objects"),target(9,"objects");
    for(int i=0;i<7;++i)objects.mData[i].value=i*23;
    Assigned::assignments=0;target=objects;
    CHECK(Assigned::assignments==7);
    for(int i=0;i<7;++i)CHECK(target.mData[i].value==i*23);
    Assigned::assignments=0;Vector<Assigned,TestAllocator> another(objects);
    CHECK(Assigned::assignments==7&&another.mData[6].value==138);
    Vector<RotAccum,TestAllocator> rotations(29,"rotations"),snapshot(29,"rotations");
    for(int i=0;i<rotations.mSize;++i) {
        auto& r=rotations.mData[i];
        r.q.x=float(i);r.q.y=float(i+1);r.q.z=float(i+2);r.q.w=float(i+3);
        r.quatAccumulatedWeight=0.5f;r.rotAroundZ=uint16_t(i*37);
        r.rotAroundZAccumulatedWeight=0.25f;r.bIdentity=i%2;
    }
    snapshot=rotations;
    for(int i=0;i<rotations.mSize;++i) {
        const auto& r=snapshot.mData[i];const auto& original=rotations.mData[i];
        CHECK(r.q.x==original.q.x&&r.q.w==original.q.w&&r.rotAroundZ==original.rotAroundZ&&
              r.quatAccumulatedWeight==original.quatAccumulatedWeight&&r.bIdentity==original.bIdentity);
    }
}
static void pcm_samples() {
    const uint8_t pcm16[]={0x80,0,0xff,0xff,0,0,0x7f,0xff};
    const int32_t expected[]={-32768,-1,0,32767};
    port::AudioSampleView sample(pcm16,nullptr,4,2);
    for(unsigned i=0;i<4;++i)CHECK(sample.read_pcm(i)==expected[i]);
    CHECK(sample.read_pcm(99)==32767);
    const uint8_t pcm8[]={0x80,0xff,0,0x7f};
    port::AudioSampleView byteSample(pcm8,nullptr,4,3);
    for(unsigned i=0;i<4;++i)CHECK(byteSample.read_pcm(i)==int32_t(int8_t(pcm8[i]))*256);
    CHECK(byteSample.read_pcm(99)==32512);
    // Stream length is maintained by the ring producer, not a clamp for reads.
    port::AudioSampleView stream(pcm16,nullptr,1,6);
    CHECK(stream.read_pcm(3)==32767&&stream.bounded_index(3)==3);
    port::AudioSampleView missing(nullptr,nullptr,4,2);CHECK(missing.read_pcm(0)==0);
    const uint8_t replacement[]={0,42};
    sample=port::AudioSampleView(replacement,nullptr,1,2);
    CHECK(sample.read_pcm(0)==42);
}
int main() {
    matrix_aliases();vector_copies();pcm_samples();
    std::printf("CPU trace hotpaths: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
