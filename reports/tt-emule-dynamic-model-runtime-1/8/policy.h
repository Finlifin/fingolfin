#define FG_MODEL_FIELDS 3u
static int fg_model_valid(uint64_t a,uint64_t b,uint64_t c) { (void)a;(void)b;(void)c; return a<=UINT32_MAX && c<=UINT32_MAX; }
static void fg_model_records(uint64_t *values,uint32_t *tags,uint64_t sequence,uint64_t a,uint64_t b,uint64_t c) { (void)sequence;(void)a;(void)b;(void)c;values[0]=sequence;tags[0]=(uint32_t)a;values[1]=b;tags[1]=17;values[2]=sequence;tags[2]=(uint32_t)c; }
static void fg_model_fill(struct FgNativeFrame *frame,uint64_t a,uint64_t b,uint64_t c) { (void)a;(void)b;(void)c;frame->arg2=(uint32_t)a;frame->arg3=(uint64_t)b;frame->arg4=(uint32_t)c; }
