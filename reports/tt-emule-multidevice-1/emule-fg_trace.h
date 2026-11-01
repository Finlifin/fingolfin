#pragma once
#include <stdint.h>
extern "C" void fg_tt_scope_event(uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t,uint64_t) __attribute__((weak));
extern "C" void fg_tt_scope_event_v2(uint32_t,uint32_t,uint64_t,uint32_t,uint32_t,uint64_t,uint64_t,uint64_t) __attribute__((weak));
struct FgTraceScope {
  uint32_t id,x,y; uint64_t device,a,b,c;
  FgTraceScope(uint32_t i,uint64_t d,uint32_t xx,uint32_t yy,uint64_t aa,uint64_t bb,uint64_t cc):id(i),x(xx),y(yy),device(d),a(aa),b(bb),c(cc) { emit(0); }
  void emit(uint32_t phase) {
    if (fg_tt_scope_event_v2) fg_tt_scope_event_v2(id,phase,device,x,y,a,b,c);
    else if (fg_tt_scope_event) fg_tt_scope_event(id,phase,x,y,a,b,c);
  }
  ~FgTraceScope() { emit(1); }
  FgTraceScope(const FgTraceScope&)=delete;
};
#define FG_TRACE_SCOPE(id,a,b,c) FgTraceScope fg_trace_scope(id, __emule_self ? __emule_self->chip_id : UINT64_MAX, __emule_self && __emule_self->core ? __emule_self->core->logical_x : UINT32_MAX, __emule_self && __emule_self->core ? __emule_self->core->logical_y : UINT32_MAX, (uint64_t)(a),(uint64_t)(b),(uint64_t)(c))
