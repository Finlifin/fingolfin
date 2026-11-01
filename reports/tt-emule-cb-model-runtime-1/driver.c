#include <stdint.h>
void fg_cb_model_1(uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c);
void fg_cb_model_2(uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c);
void fg_cb_model_3(uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c);
void fg_cb_model_4(uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c);
void fg_cb_model_5(uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c);
void fg_cb_model_6(uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c);
void fg_tt_scope_event(uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c) {
 fg_cb_model_1(hook,phase,x,y,a,b,c);
 fg_cb_model_2(hook,phase,x,y,a,b,c);
 fg_cb_model_3(hook,phase,x,y,a,b,c);
 fg_cb_model_4(hook,phase,x,y,a,b,c);
 fg_cb_model_5(hook,phase,x,y,a,b,c);
 fg_cb_model_6(hook,phase,x,y,a,b,c);
}
