#pragma once
#include <stdint.h>
extern "C" void fg_tt_scope_event(uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t) __attribute__((weak));
struct FgTraceScope {
  uint32_t id,x,y; uint64_t a,b,c;
  FgTraceScope(uint32_t i,uint32_t xx,uint32_t yy,uint64_t aa,uint64_t bb,uint64_t cc):id(i),x(xx),y(yy),a(aa),b(bb),c(cc) {
    if (fg_tt_scope_event) fg_tt_scope_event(id,0,x,y,a,b,c);
  }
  ~FgTraceScope() { if (fg_tt_scope_event) fg_tt_scope_event(id,1,x,y,a,b,c); }
  FgTraceScope(const FgTraceScope&)=delete;
};
#define FG_TRACE_SCOPE(id,a,b,c) FgTraceScope fg_trace_scope(id, __emule_self && __emule_self->core ? __emule_self->core->logical_x : UINT32_MAX, __emule_self && __emule_self->core ? __emule_self->core->logical_y : UINT32_MAX, (uint64_t)(a),(uint64_t)(b),(uint64_t)(c))
