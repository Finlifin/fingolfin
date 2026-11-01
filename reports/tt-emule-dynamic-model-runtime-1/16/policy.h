#define FG_MODEL_FIELDS 1u
static int fg_model_valid(uint64_t a,uint64_t b,uint64_t c) { (void)a;(void)b;(void)c; return b<=UINT32_MAX; }
static void fg_model_records(uint64_t *values,uint32_t *tags,uint64_t sequence,uint64_t a,uint64_t b,uint64_t c) { (void)sequence;(void)a;(void)b;(void)c;values[0]=sequence;tags[0]=(uint32_t)b; }
static void fg_model_fill(struct FgNativeFrame *frame,uint64_t a,uint64_t b,uint64_t c) { (void)a;(void)b;(void)c;frame->arg2=(uint32_t)b; }
