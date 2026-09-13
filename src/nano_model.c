// nano_model.c —— egg-nano int8 推理核（纯 C99，无 HAL 依赖）
//
// 前向（与 eggnano_weights.c 头部配方、Python 参考 forward_int8 逐行对应）：
//   x = tok_emb 查表反量化 + pos_emb
//   每层位置 t：LN1 → 量化 → QKV int8 GEMM → 量化存 K/V → int8 点积算分数
//               → f32 softmax → f32 加权 V → 量化 → out_proj GEMM → 残差
//               → LN2 → 量化 → fc1 GEMM → GELU 查表 → 量化 → fc2 GEMM → 残差
//   末位置：LN_f → tied lm_head（tok_emb 列视角）→ int32 acc 直接 argmax
//
// 溢出核算：|acc| ≤ 127×127×Cin ≤ 127×127×96 ≈ 1.55M ≪ 2^31（安全 1300 倍）
//   → 不需要 __SSAT 饱和，普通 int32 累加即安全（计划书里留的 __SSAT 属过度设计）
// 舍入纪律：away-from-zero（floor(|v|+0.5)·sign），与 Python 参考一致（禁用 banker's）
// 激活标度：fmaxf(amax, 1e-12)/127，与 Python 参考一致
// 非线性：GELU 用 161 点查表（make_lut.py 生成，误差 1e-3 < int8 噪声 4e-3）；
//         softmax 用 libm expf（每字节约 1.2K 次，可接受）；LN 用 sqrtf

#include <math.h>
#include <string.h>

#include "eggnano_weights.h"
#include "nano_lut.h"
#include "nano_model.h"

// ===================== 工作区（RAM ≈ 12.4KB） =====================
// ===================== 工作区（RAM ≈ 13KB） =====================
// KV cache：跨步持久，每层一份（因果性保证历史 K/V 不变，算过一次就能一直复用）
static int8_t  s_kc[NANO_LAYERS][NANO_SEQ][NANO_EMBD];  // 4608 B  K 缓存（int8）
static int8_t  s_vc[NANO_LAYERS][NANO_SEQ][NANO_EMBD];  // 4608 B  V 缓存（int8）
static float   s_kcs[NANO_LAYERS][NANO_SEQ];            // 576 B   K 每位置标度
static float   s_vcs[NANO_LAYERS][NANO_SEQ];            // 576 B   V 每位置标度
static float   s_x[NANO_EMBD];                          // 128 B   残差流（只留当前位置）
static float   s_sc[NANO_HEADS][NANO_SEQ];      // 768 B  分数 → softmax 权重（原址）
static int8_t  s_q[NANO_EMBD];                  // 32 B   Q 行（量化）
static float   s_qs;                            // Q 行标度
static int8_t  s_act[NANO_FFN];                 // 96 B   GEMM 输入激活（int8）
static float   s_row[NANO_FFN];                 // 384 B  行缓冲（LN 输出/QKV/gelu）
static float   s_proj[NANO_EMBD];               // 128 B  out_proj / fc2 输出
static int32_t s_logits[NANO_VOCAB];            // 1024 B 末位置 logits（int32）
static int     s_argmax;

// ===================== 基础算子 =====================

// per-row 动态量化：int8 + 标度（EPS 下限与 Python 参考一致）
static float quant_row(const float *x, int n, int8_t *q)
{
    float amax = 0.0f;
    for (int i = 0; i < n; i++) {
        float a = fabsf(x[i]);
        if (a > amax) amax = a;
    }
    if (amax < 1e-12f) amax = 1e-12f;
    float inv = 127.0f / amax;
    for (int i = 0; i < n; i++) {
        float v = x[i] * inv;
        int32_t iv = (int32_t)(fabsf(v) + 0.5f);   // away-from-zero
        if (iv > 127) iv = 127;
        q[i] = (int8_t)(v < 0.0f ? -iv : iv);
    }
    return amax / 127.0f;
}

// 单行 int8 GEMM：out[j] = (Σ_c xq[c]·wq[j][c]) · sx · w->scale + bias[j]
static void gemm_row(const int8_t *xq, float sx, const nano_qtensor_t *w,
                     const float *bias, float *out)
{
    for (int j = 0; j < (int)w->rows; j++) {
        const int8_t *wr = w->data + (uint32_t)j * w->cols;
        int32_t acc = 0;
        for (int c = 0; c < (int)w->cols; c++)
            acc += (int32_t)xq[c] * (int32_t)wr[c];
        float y = (float)acc * sx * w->scale;
        out[j] = bias ? y + bias[j] : y;
    }
}

// LayerNorm（有偏方差，eps=1e-5，与 nn.LayerNorm 默认一致）
static void ln_row(const float *x, const float *w, const float *b, float *y)
{
    float mean = 0.0f;
    for (int i = 0; i < NANO_EMBD; i++) mean += x[i];
    mean /= (float)NANO_EMBD;
    float var = 0.0f;
    for (int i = 0; i < NANO_EMBD; i++) {
        float d = x[i] - mean;
        var += d * d;
    }
    var /= (float)NANO_EMBD;
    float rstd = 1.0f / sqrtf(var + 1e-5f);
    for (int i = 0; i < NANO_EMBD; i++)
        y[i] = (x[i] - mean) * rstd * w[i] + b[i];
}

// GELU 查表 + 线性插值（表外：左侧取 0，右侧按斜率 1 延伸——gelu(x)→x）
static float gelu_fast(float x)
{
    const float hi = NANO_GELU_LUT_LO + (float)(NANO_GELU_LUT_N - 1) * NANO_GELU_LUT_STEP;
    if (x <= NANO_GELU_LUT_LO) return nano_gelu_lut[0];
    if (x >= hi) return nano_gelu_lut[NANO_GELU_LUT_N - 1] + (x - hi);
    float t = (x - NANO_GELU_LUT_LO) * (1.0f / NANO_GELU_LUT_STEP);
    int i = (int)t;
    float f = t - (float)i;
    return nano_gelu_lut[i] + (nano_gelu_lut[i + 1] - nano_gelu_lut[i]) * f;
}

// ===================== 前向 =====================

static void (*s_tick)(void);        // 生成期间的心跳回调（推理热路径上，回调里别做重活）

// 处理一个位置 t（t = 缓存内索引）：嵌入 → 3 层 → 该位置 K/V 入缓存。
// 与全量重算版逐位等价：因果掩码下位置 t 只依赖 ≤ t 的输入，
// 缓存里的 K/V 就是处理那些位置时算出的同一批数值。
//
// 【窗口滑动时缓存会失效】位置嵌入是绝对位置（0..47），窗口滑动会把所有
// 幸存字节的位置索引整体 -1（pos_emb 变了），K/V 必须重算 —— 所以
// Nano_Generate 在窗口满时走全量重算的慢路径，保证与旧版逐位一致。
static void forward_one(int t, uint8_t byte)
{
    const float att_scale = 1.0f / sqrtf((float)NANO_HEADDIM);
    const int8_t *e = nano_tok_emb.data + (uint32_t)byte * NANO_EMBD;
    const int8_t *pe = nano_pos_emb + (uint32_t)t * NANO_EMBD;
    float pe_s = nano_pos_emb_scale[t];
    for (int c = 0; c < NANO_EMBD; c++)
        s_x[c] = (float)e[c] * nano_tok_emb.scale + (float)pe[c] * pe_s;

    for (int L = 0; L < NANO_LAYERS; L++) {
        const nano_block_t *b = &nano_blocks[L];
        float qs;

        // ---- 子层 1：注意力 ----
        ln_row(s_x, b->ln1_w, b->ln1_b, s_row);
        qs = quant_row(s_row, NANO_EMBD, s_act);
        gemm_row(s_act, qs, b->qkv, b->qkv_b, s_row);       // [Q|K|V]
        s_qs = quant_row(s_row, NANO_EMBD, s_q);
        s_kcs[L][t] = quant_row(s_row + NANO_EMBD, NANO_EMBD, s_kc[L][t]);
        s_vcs[L][t] = quant_row(s_row + 2 * NANO_EMBD, NANO_EMBD, s_vc[L][t]);

        // 分数（int8 点积 → 标度还原）+ f32 softmax（因果：只到 s ≤ t，K/V 来自缓存）
        for (int h = 0; h < NANO_HEADS; h++) {
            const int8_t *qh = s_q + h * NANO_HEADDIM;
            float mx = -1e30f;
            for (int s = 0; s <= t; s++) {
                const int8_t *kh = s_kc[L][s] + h * NANO_HEADDIM;
                int32_t acc = 0;
                for (int d = 0; d < NANO_HEADDIM; d++)
                    acc += (int32_t)qh[d] * (int32_t)kh[d];
                float sc = (float)acc * s_qs * s_kcs[L][s] * att_scale;
                s_sc[h][s] = sc;
                if (sc > mx) mx = sc;
            }
            float sum = 0.0f;
            for (int s = 0; s <= t; s++) {
                float e = expf(s_sc[h][s] - mx);
                s_sc[h][s] = e;
                sum += e;
            }
            float inv = 1.0f / sum;
            for (int s = 0; s <= t; s++) s_sc[h][s] *= inv;
        }

        // 加权 V（f32：V 用标度现场反量化）
        for (int c = 0; c < NANO_EMBD; c++) s_row[c] = 0.0f;
        for (int h = 0; h < NANO_HEADS; h++) {
            float *o = s_row + h * NANO_HEADDIM;
            for (int s = 0; s <= t; s++) {
                float wv = s_sc[h][s] * s_vcs[L][s];
                const int8_t *vh = s_vc[L][s] + h * NANO_HEADDIM;
                for (int d = 0; d < NANO_HEADDIM; d++)
                    o[d] += wv * (float)vh[d];
            }
        }

        qs = quant_row(s_row, NANO_EMBD, s_act);
        gemm_row(s_act, qs, b->out, b->out_b, s_proj);
        for (int c = 0; c < NANO_EMBD; c++) s_x[c] += s_proj[c];

        // ---- 子层 2：前馈 ----
        ln_row(s_x, b->ln2_w, b->ln2_b, s_row);
        qs = quant_row(s_row, NANO_EMBD, s_act);
        gemm_row(s_act, qs, b->fc1, b->fc1_b, s_row);
        for (int i = 0; i < NANO_FFN; i++) s_row[i] = gelu_fast(s_row[i]);
        qs = quant_row(s_row, NANO_FFN, s_act);
        gemm_row(s_act, qs, b->fc2, b->fc2_b, s_proj);
        for (int c = 0; c < NANO_EMBD; c++) s_x[c] += s_proj[c];
    }
}

// 末位置：final LN + tied lm_head → 贪心（int32 累加直接比大小）
static void final_logits(void)
{
    ln_row(s_x, nano_ln_f_w, nano_ln_f_b, s_row);
    float qs = quant_row(s_row, NANO_EMBD, s_act);
    (void)qs;
    int best = 0;
    int32_t bestv = INT32_MIN;
    for (int j = 0; j < NANO_VOCAB; j++) {
        const int8_t *wr = nano_tok_emb.data + (uint32_t)j * NANO_EMBD;
        int32_t acc = 0;
        for (int c = 0; c < NANO_EMBD; c++)
            acc += (int32_t)s_act[c] * (int32_t)wr[c];
        s_logits[j] = acc;
        if (acc > bestv) {
            bestv = acc;
            best = j;
        }
    }
    s_argmax = best;
}

// 预填：处理 ids[0..T)，K/V 全部入缓存；只调用一次，之后由 Nano_Generate 逐字节 decode
static void forward(const uint8_t *ids, int T)
{
    for (int t = 0; t < T; t++) {
        if (s_tick) s_tick();                            // 心跳：约每 20~40ms 一次
        forward_one(t, ids[t]);
    }
    final_logits();
}

// 诊断：最近一次前向的 logits top-n（不建辅助数组，直接多趟选最大）
int Nano_DebugTop(uint8_t *bytes, int n)
{
    int32_t ceil_ = INT32_MAX;
    int k = 0;
    for (; k < n; k++) {
        int32_t bv = INT32_MIN;
        int bi = -1;
        for (int j = 0; j < NANO_VOCAB; j++) {
            int32_t v = s_logits[j];
            if (v < ceil_ && v > bv) { bv = v; bi = j; }
        }
        if (bi < 0) break;
        bytes[k] = (uint8_t)bi;
        ceil_ = bv;
    }
    return k;
}

int Nano_Generate(const uint8_t *prompt, int plen, uint8_t *out, int max_out,
                  void (*on_byte)(uint8_t), void (*on_tick)(void))
{
    uint8_t ctx[NANO_SEQ];
    int n = 0, k = 0;

    s_tick = on_tick;

    // 与 PC 侧 ctx[-48:] 同语义：只保留最后 NANO_SEQ 字节
    int start = plen > NANO_SEQ ? plen - NANO_SEQ : 0;
    for (int i = start; i < plen; i++) ctx[n++] = prompt[i];

    forward(ctx, n);                    // 预填：n 个位置入缓存 + 出首字节

    while (k < max_out) {
        uint8_t b = (uint8_t)s_argmax;
        if (b == NANO_EOS) break;
        out[k++] = b;
        if (on_byte) on_byte(b);
        if (n < NANO_SEQ) {
            // 快路径：窗口未满 → 位置号=绝对位置、缓存全部有效 → 只算新位置
            ctx[n++] = b;
            if (s_tick) s_tick();
            forward_one(n - 1, b);
            final_logits();
        } else {
            // 慢路径：窗口滑动 → 所有位置重排（绝对位置嵌入变了）、缓存失效
            // → 全量重算，与旧版（每次重算整个窗口）逐位一致
            memmove(ctx, ctx + 1, NANO_SEQ - 1);
            ctx[NANO_SEQ - 1] = b;
            forward(ctx, NANO_SEQ);
        }
    }

    s_tick = 0;
    return k;
}
