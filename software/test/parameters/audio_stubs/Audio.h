#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cassert>
constexpr int AUDIO_BLOCK_SAMPLES=128;
constexpr float AUDIO_SAMPLE_RATE_EXACT=44100.0f;
#include "arm_math.h"
struct audio_block_t { int16_t data[AUDIO_BLOCK_SAMPLES]{}; };
class AudioStream {
 public:
  AudioStream(int=0,audio_block_t** =nullptr) {}
  virtual void update() {}
  virtual ~AudioStream()=default;
  audio_block_t inputs[4]{},result{};bool present[4]{};
  bool fail_allocate=false,transmitted=false;
  audio_block_t* receiveReadOnly(int ch){assert(ch>=0&&ch<4);if(!present[ch])return nullptr;present[ch]=false;return &inputs[ch];}
  audio_block_t* allocate(){return fail_allocate?nullptr:&storage;}
  void release(audio_block_t*){}
  void transmit(audio_block_t* b){assert(b);result=*b;transmitted=true;}
  void Feed(int ch,int16_t v){assert(ch>=0&&ch<4);for(auto& x:inputs[ch].data)x=v;present[ch]=true;}
 private: audio_block_t storage{};
};
