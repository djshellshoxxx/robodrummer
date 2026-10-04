#include "core/GrooveGenerator.h"
#include <array>
#include <cassert>
int main() {
    using namespace robodrummer;
    GrooveGenerator g;
    Style style=Style::basicRock();
    GrooveContext c;
    c.seed=42;
    c.intensity=0.5f;
    std::array<DrumEvent,64> a{},b{};
    auto na=g.generateBar(style,c,a);
    auto nb=g.generateBar(style,c,b);
    assert(na==nb && na>0);
    for(size_t i=0;i<na;++i){
        assert(a[i].instrument==b[i].instrument);
        assert(a[i].sampleOffset==b[i].sampleOffset);
        assert(a[i].sampleOffset>=0 && a[i].sampleOffset<96000);
    }
    bool snare2=false,snare4=false;
    for(size_t i=0;i<na;++i){
        if(a[i].instrument==DrumInstrument::Snare && a[i].sampleOffset==24000) snare2=true;
        if(a[i].instrument==DrumInstrument::Snare && a[i].sampleOffset==72000) snare4=true;
    }
    assert(snare2 && snare4);
    GrooveContext low=c, high=c;
    low.intensity=0.0f;
    high.intensity=1.0f;
    size_t lowKick=0, highKick=0;
    for(unsigned seed=1; seed<200; ++seed){
        low.seed=seed; high.seed=seed;
        na=g.generateBar(style,low,a);
        for(size_t i=0;i<na;++i) lowKick += a[i].instrument==DrumInstrument::Kick;
        nb=g.generateBar(style,high,b);
        for(size_t i=0;i<nb;++i) highKick += b[i].instrument==DrumInstrument::Kick;
    }
    assert(highKick > lowKick);
}
