// Minimal retained compiler regression: inline/template metadata versus a
// non-inline function-local control, with the real production storage macro.
#include "parameters/Schema.h"
using phz::parameters::Option;
// The production slot script needs nonempty .data to establish DTCM's cursor.
__attribute__((used)) volatile unsigned RamAnchor = 1;
#ifdef PROBE_STACK_PRESSURE
__attribute__((used)) volatile unsigned char StackPressure[470 * 1024];
#endif
#ifdef PROBE_ORIGINAL_STORAGE
#define OPTIONS_STORAGE(name) __attribute__((section(".progmem.parameters")))
#else
#define OPTIONS_STORAGE(name) PHZ_PARAMETER_STORAGE
#endif
inline constexpr Option InlineOptions[] OPTIONS_STORAGE("inline")={{0,7},{1,9}};
template<int N> struct App {
 static PHZ_PARAMETER_CODE const Option* Table(){
  static constexpr Option TemplateOptions[] OPTIONS_STORAGE("template")={{0,N},{1,N+1}};
  return TemplateOptions;
 }
};
__attribute__((noinline)) const Option* Direct(){
 static constexpr Option DirectOptions[] OPTIONS_STORAGE("direct")={{0,11},{1,12}};
 return DirectOptions;
}
extern "C" __attribute__((used,noinline)) const Option* Select(unsigned n){
 if(n==0)return InlineOptions;
 if(n==1)return App<17>::Table();
 return Direct();
}
