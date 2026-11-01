#include "adapter.h"
#include "push.adapter.h"
#include "policy.h"
static int valid(const FgModelEvent *e) { return fg_model_valid(e->a,e->b,e->c); }
static void fill(void *output,const FgCallHeader *h,const FgModelEvent *e) { struct FgNativeFrame *f=output; memset(f,0,sizeof(*f));f->header=*h;f->arg0.fg_73657175656e6365=e->sequence;f->arg0.fg_746872656164=e->thread;f->arg0.fg_7068617365=e->phase;f->arg1.fg_78=e->x;f->arg1.fg_79=e->y;fg_model_fill(f,e->a,e->b,e->c);}
static size_t expected(FgRecord *r,const FgModelEvent *e) { r[0]=(FgRecord){e->phase,e->sequence};uint64_t v[FG_MODEL_FIELDS];uint32_t t[FG_MODEL_FIELDS];fg_model_records(v,t,e->sequence,e->a,e->b,e->c);for(size_t i=0;i!=FG_MODEL_FIELDS;i++)r[i+1]=(FgRecord){t[i],v[i]};return FG_MODEL_FIELDS+1;}
static int matched(const FgModelEvent *e) {return !e->x && !e->y && e->thread;}
const FgModelAdapter fg_dyn_8={sizeof(struct FgNativeFrame),fg_generated_encode,valid,fill,expected,matched};
