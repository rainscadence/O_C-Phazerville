#include "remote/Parser.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <cstdio>
using Parser=phz::remote::Parser;
using R=Parser::Result;
int main() {
 for(unsigned byte=0;byte<256;++byte) {
   Parser p;
   assert(p.Feed(byte,0)==(byte=='~'?R::Pending:R::Pass));
   Parser frame;frame.Feed('~',0);
   const auto middle=frame.Feed(byte,1);
   if(byte=='\n') {assert(middle==R::Invalid);continue;}
   assert(middle==R::Pending);
   const bool allowed=byte!=0 && std::string("?ABlr[]SJKNPCE").find(char(byte))!=std::string::npos;
   assert(frame.Feed('\n',2)==(allowed?R::Action:R::Invalid));
   if(allowed) assert(frame.selector()==char(byte));
 }
 for(const std::string frame:{"~\n","~~\n","~AA\n","~Q\n","~F\n","~Ar\n","~A\r\n"}) {
   Parser p; R result=R::Pass;
   for(char b:frame){result=p.Feed(b,5);assert(result!=R::Action && result!=R::Pass);}
   assert(result==R::Invalid);
 }
 Parser p;p.Feed('~',0);p.Feed('A',1);assert(p.Feed('\n',2)==R::Action);
 p.Feed('~',3);assert(p.Feed('\n',4)==R::Invalid); // never replay A
 for(uint32_t start:{uint32_t(0),UINT32_MAX-100}) {
   Parser q;q.Feed('~',start);q.Feed('r',start+1);
   assert(q.Feed('\n',start+249)==R::Action);
   Parser expired;expired.Feed('~',start);expired.Feed('r',start+1);
   assert(expired.Feed('C',start+250)==R::Pending);
   assert(expired.Feed('F',start+251)==R::Pending); // no escape to destructive debug input
   assert(expired.Feed('\n',start+252)==R::Invalid);
   assert(expired.Feed('S',start+253)==R::Pass);
 }
 puts("PASS: parser allowlist, framing, stale replay, timeout, wraparound, debug-tail isolation");
}
