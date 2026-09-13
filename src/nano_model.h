// nano_model.h —— egg-nano int8 推理核（纯 C99，无 HAL 依赖）
//
// 规格书 = scripts/nano/quantize_i8.py::forward_int8（Python 参考实现，Gate A/B 已通过）
// 与 MCU 的差异（有意为之，为省 RAM）：
//   K/V 以 int8 + per-position 标度存储（参考实现用 f32）
//   误差约 0.4%（与权重量化同量级；Gate A 实测 margin 是噪声的 23 倍，不影响行为）
#ifndef NANO_MODEL_H
#define NANO_MODEL_H

#include <stdint.h>

#define NANO_EOS 0xFF

// 自回归生成：prompt[0..plen) → out[0..max_out)
// 实现用 KV cache（K/V 跨步持久，只处理新位置），输出与全量重算逐字节一致。
// on_byte 非空时每生成一字节回调一次（用于边生成边刷屏）
// on_tick  非空时生成期间周期性回调（推理热路径上，每位置一次；只做轻活）
// 返回实际生成字节数（不含 EOS）
int Nano_Generate(const uint8_t *prompt, int plen, uint8_t *out, int max_out,
                  void (*on_byte)(uint8_t), void (*on_tick)(void));

// 诊断用：取最近一次前向的 logits top-n 字节值（降序），返回实际写入个数。
// 与 PC 侧 scripts/nano/check_mcu_math.py 的 top-5 对照，可定位数值是否从第一步就错。
int Nano_DebugTop(uint8_t *bytes, int n);

#endif // NANO_MODEL_H
