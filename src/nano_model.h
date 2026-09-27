// nano_model.h —— egg-nano int8 推理核

#ifndef NANO_MODEL_H
#define NANO_MODEL_H

#include <stdint.h>

#define NANO_EOS 0xFF

int Nano_Generate(const uint8_t *prompt, int plen, uint8_t *out, int max_out,
                  void (*on_byte)(uint8_t), void (*on_tick)(void));

int Nano_DebugTop(uint8_t *bytes, int n);

#endif // NANO_MODEL_H
