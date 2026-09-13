// eggnano_weights.h —— 由 scripts/nano/quantize_i8.py 自动生成，勿手改！
// 重新生成: python scripts/nano/quantize_i8.py
#ifndef EGGNANO_WEIGHTS_H
#define EGGNANO_WEIGHTS_H

#include <stdint.h>

#define NANO_VOCAB   256
#define NANO_SEQ     48
#define NANO_EMBD    32
#define NANO_HEADS   4
#define NANO_HEADDIM 8
#define NANO_LAYERS  3
#define NANO_FFN     96

// 量化张量：int8 数据 + per-tensor f32 scale（反量化 w = scale * q）
typedef struct {
    const int8_t *data;   // 行主序 [rows, cols]（torch Linear weight 布局）
    uint16_t rows;        // out_features（tok_emb: rows=vocab）
    uint16_t cols;        // in_features
    float    scale;
} nano_qtensor_t;

typedef struct {
    const nano_qtensor_t *qkv, *out, *fc1, *fc2;
    const float *ln1_w, *ln1_b, *ln2_w, *ln2_b;   // f32（§2.5 norm-f32 结论）
    const float *qkv_b, *out_b, *fc1_b, *fc2_b;   // f32 bias（加在反量化后）
} nano_block_t;

// tok_emb 双角色（weight tying）：输入查表（行）+ lm_head（列）共用同一份 int8
extern const nano_qtensor_t nano_tok_emb;
extern const nano_block_t   nano_blocks[NANO_LAYERS];
extern const int8_t         nano_pos_emb[NANO_SEQ * NANO_EMBD];   // int8（逐位置量化）
extern const float          nano_pos_emb_scale[NANO_SEQ];         // 每位置一个标度
extern const float          nano_ln_f_w[NANO_EMBD];
extern const float          nano_ln_f_b[NANO_EMBD];

#endif // EGGNANO_WEIGHTS_H
