// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include "audio_core/hle/accuracy.h"
using namespace AudioCore::HLE;
int main(int argc,char** argv) {
    // Synthetic bytes exercise bounded matching without bundling game audio.
    std::array<std::uint8_t,64> bytes{};
    for(unsigned i=0;i<bytes.size();++i)bytes[i]=std::uint8_t(i*37);
    const RupeeAudio::Signature synthetic{0,RupeeAudio::Hash(bytes),RupeeAudio::Hash(std::span(bytes).first(8)),905,9};
    assert(RupeeAudio::Matches(synthetic,905,bytes));
    assert(RupeeAudio::Matches(synthetic,896,bytes));
    assert(RupeeAudio::Matches(synthetic,9,std::span(bytes).first(8)));
    assert(!RupeeAudio::Matches(synthetic,905,std::span(bytes).first(63)));
    assert(!RupeeAudio::Matches(synthetic,9,std::span(bytes).first(7)));
    assert(!RupeeAudio::Matches(synthetic,904,bytes));
    bytes[0]^=1;assert(!RupeeAudio::Matches(synthetic,905,bytes));
    assert(!RupeeAudio::Find({},905));
    // Optional local validation against the user's extracted CWAVs. Data is
    // read only during this check; no audio assets are copied into the project.
    for(int arg=1;arg<argc;++arg) {
        std::ifstream input(argv[arg],std::ios::binary);
        const std::vector<std::uint8_t> wav{std::istreambuf_iterator<char>(input),{}};
        assert(wav.size()>0xe0 && std::string(wav.begin(),wav.begin()+4)=="CWAV");
        const auto word=[&](unsigned at){return unsigned(wav[at])|(unsigned(wav[at+1])<<8)|
            (unsigned(wav[at+2])<<16)|(unsigned(wav[at+3])<<24);};
        std::array<std::int16_t,16> coefficients{};
        for(unsigned i=0;i<16;++i)coefficients[i]=std::int16_t(wav[0x7c+2*i]|(wav[0x7d+2*i]<<8));
        const auto start=word(0x50),samples=word(0x54);
        const auto* signature=RupeeAudio::Find(coefficients,samples);assert(signature);
        const auto data=std::span(wav).subspan(0xe0);
        assert(RupeeAudio::Matches(*signature,samples,data));
        assert(RupeeAudio::Matches(*signature,samples-start,data.subspan(start/14*8)));
        coefficients[0]^=1;assert(!RupeeAudio::Find(coefficients,samples));
    }
    std::cout<<"Rupee sample identification, loop tails, truncation and mismatches passed\n";
}
