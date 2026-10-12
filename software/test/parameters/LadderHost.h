#pragma once
#include "RoutingHost.h"
struct LadderStream:AudioStream {
 using AudioStream::AudioStream;
 audio_block_t* receiveWritable(int ch){return receiveReadOnly(ch);}
};
struct LadderHostBase:RoutingHostBase {
 template<class... A>void gfxPrintPitchHz(A...){}
};
#define private public
#define AudioStream LadderStream
#include PHZ_LADDER_HEADER
#include PHZ_LADDER_SOURCE
#undef AudioStream
#define HemisphereAudioApplet LadderHostBase
#include "audio_applets/LadderApplet.h"
#undef HemisphereAudioApplet
#undef private
