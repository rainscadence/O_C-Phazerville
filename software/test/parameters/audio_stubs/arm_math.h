#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cassert>
// Host seams for ARM instructions/CMSIS vector kernels, not a timing or
// instruction-bit-exact emulation. Scalar float_to_q15 is production code.
using q15_t=int16_t;using q31_t=int32_t;
inline q31_t __SSAT(q31_t v,int bits){const int32_t high=(1<<(bits-1))-1;return std::clamp(v,-high-1,high);}
inline q15_t HostQ15(float v){assert(std::isfinite(v));return int16_t(std::clamp(v*32768.0f,-32768.0f,32767.0f));}
inline void arm_q15_to_float(const q15_t* a,float* b,int n){while(n--)*b++=*a++/32768.0f;}
inline void arm_float_to_q15(const float* a,q15_t* b,int n){while(n--)*b++=HostQ15(*a++);}
inline void arm_fill_f32(float v,float* a,int n){while(n--)*a++=v;}
inline void arm_scale_f32(const float* a,float v,float* b,int n){while(n--)*b++=*a++*v;}
inline void arm_add_f32(const float* a,const float* b,float* c,int n){while(n--)*c++=*a++ + *b++;}
inline void arm_mult_f32(const float* a,const float* b,float* c,int n){while(n--)*c++=*a++ * *b++;}
inline void arm_add_q15(const q15_t* a,const q15_t* b,q15_t* c,size_t n){while(n--)*c++=__SSAT(int32_t(*a++)+*b++,16);}
inline void arm_sub_q15(const q15_t* a,const q15_t* b,q15_t* c,size_t n){while(n--)*c++=__SSAT(int32_t(*a++)-*b++,16);}
inline void arm_mult_q15(const q15_t* a,const q15_t* b,q15_t* c,size_t n){while(n--)*c++=__SSAT((int32_t(*a++) * *b++)>>15,16);}
inline void arm_scale_q15(const q15_t* a,q15_t scale,int shift,q15_t* b,size_t n){while(n--)*b++=__SSAT((int32_t(*a++)*scale)>>(15-shift),16);}
