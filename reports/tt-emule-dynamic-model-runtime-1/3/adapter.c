#include "adapter.h"
#include "push.adapter.h"
static int valid(const FgModelEvent *e) { return e->a<=UINT32_MAX && e->b<=UINT32_MAX; }
static void fill(void *output,const FgCallHeader *h,const FgModelEvent *e) { struct FgNativeFrame *f=output; memset(f,0,sizeof(*f));f->header=*h;f->arg0.fg_73657175656e6365=e->sequence;f->arg0.fg_746872656164=e->thread;f->arg0.fg_7068617365=e->phase;f->arg1.fg_78=e->x;f->arg1.fg_79=e->y;f->arg2=(uint32_t)e->a;f->arg3=(uint32_t)e->b;}
static size_t expected(FgRecord *r,const FgModelEvent *e) { r[0]=(FgRecord){e->phase,e->sequence};return 1;}
static int matched(const FgModelEvent *e) {return !e->x && !e->y && e->thread && !e->a && e->b>0;}
const FgModelAdapter fg_dyn_3={sizeof(struct FgNativeFrame),fg_generated_encode,valid,fill,expected,matched};
