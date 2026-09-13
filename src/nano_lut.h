// nano_lut.h —— 由 scripts/nano/make_lut.py 自动生成，勿手改！
#ifndef NANO_LUT_H
#define NANO_LUT_H

#include <stdint.h>

// GELU 查表：x ∈ [-8.0, 8.0] 均匀 161 点（步长 0.1），表外按线性尾部处理
#define NANO_GELU_LUT_N    161
#define NANO_GELU_LUT_LO   (-8.0f)
#define NANO_GELU_LUT_STEP (0.1f)

extern const float nano_gelu_lut[NANO_GELU_LUT_N];

#endif // NANO_LUT_H
