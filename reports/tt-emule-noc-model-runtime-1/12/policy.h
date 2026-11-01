#define FG_MODEL_FIELDS 8u
static int fg_model_valid(uint64_t a,uint64_t b,uint64_t c) { (void)a;(void)b;(void)c; return a<=UINT8_MAX; }
static void fg_model_records(uint64_t *values,uint32_t *tags,uint64_t sequence,uint64_t a,uint64_t b,uint64_t c) { (void)sequence;(void)a;(void)b;(void)c;values[0]=(a>>0)&1u;tags[0]=32;values[1]=(a>>1)&1u;tags[1]=33;values[2]=(a>>2)&1u;tags[2]=34;values[3]=(a>>3)&1u;tags[3]=35;values[4]=(a>>4)&1u;tags[4]=36;values[5]=(a>>5)&1u;tags[5]=37;values[6]=(a>>6)&1u;tags[6]=38;values[7]=(a>>7)&1u;tags[7]=39; }
static void fg_model_fill(struct FgNativeFrame *frame,uint64_t a,uint64_t b,uint64_t c) { (void)a;(void)b;(void)c;frame->arg2=(uint8_t)a; }
