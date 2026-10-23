// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <map>
#include <future>
#include "mm3d/photo_capture.h"
#include "mm3d/photo_save_queue.h"
using namespace MM3D::PhotoCapture;
struct Memory {
    std::map<U32,U32> words;
    unsigned reads=0;
    std::optional<U32> Read(U32 at){++reads;auto i=words.find(at);return i==words.end()?std::nullopt:std::optional<U32>{i->second};}
};
struct Fake : Backend {
    unsigned captures=0,bindings=0,resets=0,keeps=0,loads=0,eye=0;
    U32 address=0;bool available=true;
    std::vector<std::uint8_t> saved;
    void ResetPhoto()override{++resets;}
    void CapturePhoto(U32 p,unsigned e)override{++captures;address=p;eye=e;}
    bool BindPhoto(U32 p,unsigned e,const Probes&)override{++bindings;address=p;eye=e;return available;}
    void KeepPhoto(std::span<const std::uint8_t> bytes)override{++keeps;saved.assign(bytes.begin(),bytes.end());}
    void LoadPhoto(std::span<const std::uint8_t> bytes)override{++loads;saved.assign(bytes.begin(),bytes.end());}
};
int main(int argc,char** argv) {
    assert(!HasPhotoBinding({0,0,0,0}));
    assert(!HasPhotoBinding({1,2,0,0})); // Pause backdrops retain their separate replay policy.
    assert(HasPhotoBinding({3,0,0,0}) && HasPhotoBinding({0,4,0,0}) && HasPhotoBinding({0,0,3,0}));
    assert(!HasPhotoBinding({0,0,0,3})); // Reserved slot is not a bound texture.
    {
        std::promise<void> started,release;
        const auto gate=release.get_future().share();
        std::vector<int> completed;
        const auto caller=std::this_thread::get_id();
        {
            SaveQueue queue;
            queue.Submit([&] {
                assert(std::this_thread::get_id()!=caller);
                started.set_value();gate.wait();completed.push_back(1);
            });
            assert(started.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
            queue.Submit([&]{completed.push_back(2);});
            queue.Submit([&]{completed.push_back(3);}); // Only newest queued photo is retained.
            release.set_value();
        } // Shutdown drains the active and newest accepted save.
        assert((completed==std::vector<int>{1,3}));
    }
    Memory m;
    for(unsigned i=0;i<PCs.size();++i)m.words[PCs[i]]=Words[i];
    for(unsigned i=0;i<MM3D::PauseCapture::PCs.size();++i)m.words[MM3D::PauseCapture::PCs[i]]=MM3D::PauseCapture::Words[i];
    m.words[0x120658]=0xe3550000;m.words[0x141d94]=0xec800a07;m.words[0x19886c]=0xe5900000;
    m.words[0x180074]=0x6754;m.words[0x180058]=0x8363;
    m.words[0x188fac]=0xeb05dc68;m.words[0x609fc4]=0xe8820060;m.words[0x17dafc]=0xe585004c;
    if(argc>1) {
        std::ifstream file(argv[1],std::ios::binary);assert(file);
        for(auto& [pc,word]:m.words) {
            U32 actual=0;file.seekg(pc-0x100000);file.read(reinterpret_cast<char*>(&actual),4);
            assert(actual==word);word=actual;
        }
    }
    assert(CodeMatches(m));m.words[PCs[0]]^=1;assert(!CodeMatches(m));m.words[PCs[0]]^=1;
    for(unsigned i=0;i<PCs.size();++i) {
        assert(Instruction(PCs[i],Words[i])==(0xef000000|FirstSVC+i));
        assert(Instruction(PCs[i],Words[i]^1)==(Words[i]^1));
    }
    Fake fake;backend=&fake;Hook h;
    constexpr U32 owner=0x800000,manager=0x810000,pair=0x820000,desc=0x830000,raw=0x840000,photo_layout=0x890000;
    m.words[manager+0xc]=0x14109120;
    h.Convert(m,manager,0,0,0);assert(!fake.captures);
    h.Begin(owner);h.Convert(m,manager,0,0,1);assert(!fake.captures); // Pause remains independent.
    h.Convert(m,manager,0,0,0);assert(fake.captures==1 && fake.address==0x20109120);
    h.Convert(m,manager,2,0,0);assert(fake.captures==1);
    m.words[owner+8]=pair;m.words[pair+0x4c]=desc;
    m.words[desc+8]=0x1907;m.words[desc+12]=0x8363;m.words[desc+16]=0x1000200;
    m.words[desc+0x14]=raw;m.words[desc+0x20]=0x14b1e480;
    for(auto point:MM3D::PauseCapture::ProbePoints) {
        const U32 at=raw+2*(point[1]*512+point[0]);m.words[at&~3u]=0x1234<<((at&2)*8);
    }
    const auto before=m.words;
    h.Bind(m,photo_layout,pair,pair+0x58);
    assert(fake.bindings==1 && layout==photo_layout && m.words==before);
    h.Bind(m,photo_layout,pair+4,pair+0x5c);assert(fake.bindings==1);
    m.words[desc+12]=0x1401;h.Bind(m,photo_layout,pair,pair+0x58);assert(fake.bindings==1);m.words=before;
    m.words[desc+8]=0x6754;
    for(auto point:MM3D::PauseCapture::ProbePoints) {
        constexpr U32 interleave[8]={0,1,4,5,16,17,20,21};
        const U32 pixel=(point[1]/8)*4096+(point[0]/8)*64+interleave[point[0]%8]+2*interleave[point[1]%8];
        const U32 at=raw+2*pixel;m.words[at&~3u]=0x5678<<((at&2)*8);
    }
    h.Bind(m,photo_layout,pair,pair+0x58);assert(fake.bindings==2);
    // The private surface is normalized independently of native JPEG tiling.
    static_assert(SurfaceOrientation==2 && TextureBinding==4);
    // Keep/reload uses the complete native JPEG identity, without touching saves.
    m.words[0x7788a4]=5;m.words[0x7788a8]=0x1234d8ff;m.words[0x7788ac]=0xab;
    const auto saved=m.words;h.Keep(m);
    assert(fake.keeps==1 && fake.saved==std::vector<std::uint8_t>({255,216,52,18,171}) && m.words==saved);
    h.Keep(m);assert(fake.keeps==1);
    h.Destroy(owner+4);assert(h.owner==owner);
    h.Destroy(owner);assert(!h.owner && !layout);
    h.Import(m,owner,0x7788a8,5);assert(fake.loads==1 && !h.pending);
    fake.available=false;h.Bind(m,photo_layout,pair,pair+0x58);assert(!layout); // Missing host PNG falls back.
    h.Import(m,owner,0x7788a8,65353);assert(!h.owner && fake.loads==1);
    m.words[0x7788a8]=0;h.Import(m,owner,0x7788a8,5);assert(!h.owner && fake.loads==1);
    // Rejecting a shutter capture cannot commit it later.
    h.Begin(owner);h.Destroy(owner);h.Keep(m);assert(fake.keeps==1);
    // Disabled sessions never inspect photo pixels/JPEGs or enter the HD
    // capture, binding, PNG loading or save pipeline. Guest state is unchanged.
    enabled=false;
    const auto native=m.words;
    const auto captures=fake.captures,bindings=fake.bindings,keeps=fake.keeps,loads=fake.loads;
    m.reads=0;
    h.Begin(owner);
    h.Convert(m,manager,0,0,0);
    h.Bind(m,photo_layout,pair,pair+0x58);
    h.Keep(m);
    h.Import(m,owner,0x7788a8,5);
    // Even stale ownership must not reach an HD backend while disabled.
    h.owner=owner;h.pending=true;
    h.Convert(m,manager,0,0,0);
    h.Bind(m,photo_layout,pair,pair+0x58);
    h.Keep(m);h.Reset();
    assert(!m.reads && m.words==native && !layout);
    assert(fake.captures==captures && fake.bindings==bindings && fake.keeps==keeps && fake.loads==loads);
    enabled=true;
    h.Begin(owner);h.Convert(m,manager,0,0,0);
    assert(fake.captures==captures+1);h.Reset();
    backend=nullptr;
}
