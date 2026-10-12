#pragma once
#include "../audio_stubs/arm_math.h"
// Explicit seam: force the real filter's linear fallback. Do not simulate or
// claim validation of its default CMSIS polyphase FIR audio path.
struct arm_fir_interpolate_instance_f32 {};
struct arm_fir_decimate_instance_f32 {};
template<class... A>int arm_fir_interpolate_init_f32(A...){return 1;}
template<class... A>int arm_fir_decimate_init_f32(A...){return 1;}
template<class... A>void arm_fir_interpolate_f32(A...){assert(false);}
template<class... A>void arm_fir_decimate_f32(A...){assert(false);}
