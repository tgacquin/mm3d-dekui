// SPDX-License-Identifier: GPL-2.0-or-later
// Reads the user's original GMSG archives and installed code; writes no assets.
#include "mm3d/single_screen_text.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
using namespace MM3D::SingleScreenText;
struct Memory {
    std::vector<unsigned char> data;
    std::unordered_map<U32,U32> words;
    U32 denied=0;unsigned writes=0;
    static constexpr U32 Base=0x200000,Self=0x180000;
    std::optional<U32> Read(U32 p) const {
        if(p==denied)return {};
        if(p>=Base&&p-Base+4<=data.size()) {
            U32 v=0;for(unsigned i=0;i<4;++i)v|=U32(data[p-Base+i])<<(8*i);return v;
        }
        auto it=words.find(p);return it==words.end()?std::nullopt:std::optional<U32>(it->second);
    }
    void Write(U32 p,U32 v) {
        ++writes;
        assert(p>=Base&&p-Base+4<=data.size());
        for(unsigned i=0;i<4;++i)data[p-Base+i]=v>>(8*i);
    }
    explicit Memory(const std::vector<unsigned char>& bytes):data(bytes) {
        words={{Self+4,Base},{Self+8,Base+16},{Self+12,U32(data.size())},{Self+16,20}};
    }
    U32 Entry(U32 id) const {
        for(U32 i=0;i<*Read(Base+8);++i)if((*Read(Base+16+i*20)&65535)==id)return Base+16+i*20;
        return 0;
    }
};
std::vector<unsigned char> Load(const char* name) {
    std::ifstream f(name,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};
}
int main(int argc,char** argv) {
    assert(argc==3||argc==5);const auto original=Load(argv[1]);
    const auto check=[](const auto& replacements,const auto& original) {
    for(const auto& r:replacements) {
        Memory m(original);U32 entry=m.Entry(r.id);assert(entry);
        const U32 offset=*m.Read(entry+12),size=*m.Read(entry+16);
        Apply(m,Memory::Self,r.id);assert(m.writes>0);
        for(U32 i=0;i<original.size();++i) {
            unsigned char expected=original[i];
            if(i>=offset&&i<offset+size)expected=i-offset<r.text.size()?r.text[i-offset]:0;
            assert(m.data[i]==expected);
        }
        auto once=m.data;m.writes=0;Apply(m,Memory::Self,r.id);assert(m.data==once&&m.writes==0);
        enabled.store(false);
        Memory disabled(original);Apply(disabled,Memory::Self,r.id);
        assert(disabled.data==original&&disabled.writes==0);
        Apply(m,Memory::Self,r.id);assert(m.data==original);
        m.writes=0;Apply(m,Memory::Self,r.id);assert(m.writes==0);
        enabled.store(true);Apply(m,Memory::Self,r.id);assert(m.data==once);
        // Failure must leave the entire file and neighboring messages untouched.
        for(unsigned scenario=0;scenario<8;++scenario) {
            Memory bad(original);
            switch(scenario) {
            case 0:bad.words[Memory::Self+16]=24;break;
            case 1:bad.data[12]=99;break; // unsupported language
            case 2:bad.data[0]=0;break;
            case 3:bad.words[Memory::Self+12]=16;break;
            case 4:bad.data[offset]^=1;break; // custom/modded text
            case 5:bad.denied=(Memory::Base+offset+size-1)&~3u;break;
            case 6:bad.words[Memory::Self+8]+=4;break;
            case 7:bad.data[entry-Memory::Base+16]--;break;
            }
            auto before=bad.data;Apply(bad,Memory::Self,r.id);assert(bad.data==before&&bad.writes==0);
        }
    }
    };
    check(Replacements,original);
    if(argc==5){check(FrenchReplacements,Load(argv[3]));check(SpanishReplacements,Load(argv[4]));}
    const auto modes=[](const auto& modern,const auto& legacy,const auto& original) {
        for(const auto& r:modern) {
            Memory m(original);const U32 entry=m.Entry(r.id),offset=*m.Read(entry+12),size=*m.Read(entry+16);
            const Replacement* old=nullptr;for(const auto& candidate:legacy)if(candidate.id==r.id)old=&candidate;
            assert(old);
            for(bool active:{true,false,true})for(bool clicks:{true,false,true,false}) {
                enabled=active;MM3D::StickControls::Configure(clicks);
                assert(Apply(m,Memory::Self,r.id));
                const auto desired=active?(clicks?r.text:old->text):old->original;
                for(U32 i=0;i<original.size();++i) {
                    unsigned char expected=original[i];
                    if(i>=offset&&i<offset+size)expected=i-offset<desired.size()?desired[i-offset]:0;
                    assert(m.data[i]==expected);
                }
                m.writes=0;assert(Apply(m,Memory::Self,r.id)&&m.writes==0);
                Memory bad(original);bad.data[offset]^=1;
                const auto before=bad.data;assert(!Apply(bad,Memory::Self,r.id)&&bad.data==before&&bad.writes==0);
            }
        }
        enabled=true;MM3D::StickControls::Configure(false);
    };
    modes(StickReplacements,Replacements,original);
    if(argc==5) {
        modes(StickFrenchReplacements,FrenchReplacements,Load(argv[3]));
        modes(StickSpanishReplacements,SpanishReplacements,Load(argv[4]));
    }
    Memory untouched(original);
    for(U32 id:{0x887,0x1273,0x191f,0x1f41,0x1f42,0x2ac1,0x5145,0xffff})Apply(untouched,Memory::Self,id);
    assert(untouched.data==original&&untouched.writes==0); // ordinary dialogue remains native
    Memory code({});auto binary=Load(argv[2]);
    for(size_t i=0;i+4<=binary.size();i+=4) {
        U32 v=0;for(unsigned j=0;j<4;++j)v|=U32(binary[i+j])<<(j*8);
        code.words[0x100000+U32(i)]=v;
    }
    assert(CodeMatches(code));assert(Instruction(PC,Word)==(0xef000000|SVC));
    code.words[PC+4]^=1;assert(!CodeMatches(code));
    std::cout<<"Text replacements: full-file isolation, idempotence, malformed input and code guards passed\n";
    return 0;
}
