#define FG_MODEL_FIELDS 1u
static int fg_model_valid(uint64_t a,uint64_t b,uint64_t c) { (void)a;(void)b;(void)c; return a<=UINT32_MAX; }
static void fg_model_values(uint32_t *values,uint64_t a,uint64_t b,uint64_t c) { (void)values;(void)a;(void)b;(void)c;values[0]=(uint32_t)a;}
static void fg_model_fill(struct FgNativeFrame *frame,uint64_t a,uint64_t b,uint64_t c) { (void)frame;(void)a;(void)b;(void)c;frame->arg2=(uint32_t)a;}
