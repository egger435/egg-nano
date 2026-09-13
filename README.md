# egg-nano

> 在一颗 **STM32F103C8**（Cortex-M3 @72MHz，**64KB Flash / 20KB RAM**）上跑一个 3 层 int8 自回归语言模型：
> **串口收问题 → 片上推理 → OLED 显示回答**。全部推理在 MCU 本地完成，无外部算力、无操作系统。

```
问：你好            <- 串口逐字回显
nano> 你好！很高兴认识你。   <- 边生成边流出，同时刷 OLED
[03.7s 39B]
dbg clk sysclk=72000000 hclk=72000000 pclk2=72000000
dbg prompt[19]=e9 97 ae ef bc 9a ...
dbg top3=228 229 225
dbg hex=e4 bd a0 e5 a5 bd ...
```

---

## 1. 项目做什么

| 环节 | 实现 |
| --- | --- |
| 输入 | USART1（PA9/PA10）115200 8N1，逐行读取，带本地回显与退格 |
| 模型 | 3 层 GPT 式解码器，**byte-level（vocab=256）**，int8 量化，贪心解码 |
| 推理 | 纯 C99 实现，**不依赖 HAL**，KV cache 复用，边生成边输出 |
| 显示 | SSD1306 128×64 OLED（软件 IIC），12×12 中文子集字库，UTF-8 折行/滚动 |
| 诊断 | DWT 计时、时钟树、prompt 原始字节、首字节 logits top-3、输出十六进制 |

模型规模很小（约 **4.2 万参数**），能力是"训练语料风格的中文短句问答"，属于
**"把语言模型塞进 64KB"** 的极限工程演示，不是通用助手。回答长度上限 60 字节，遇到 `0xFF`（EOS）提前停止。

---

## 2. 硬件与接线

| 项目 | 参数 |
| --- | --- |
| MCU | STM32F103C8（Cortex-M3，72MHz，64KB Flash，20KB RAM，LQFP48） |
| 串口 | USART1：**PA9 = TX**（复用推挽）、**PA10 = RX**（浮空输入），115200 8N1 |
| OLED | SSD1306 128×64，**软件 IIC：SCL = PA6 / SDA = PA5**（开漏输出，模块自带上拉），从机地址 `0x78` |
| 调试 | SWD（JLink 或 ST-Link + OpenOCD） |
| 定时 | 业务计时用 **DWT->CYCCNT ÷ 72000**；`Delay.c` 占用 SysTick 做忙等（仅 OLED 上电延时用） |

> 两套时间基准互不干扰：SysTick 归 `Delay.c`，DWT 归 `nano_uart.c`。
> 软件 IIC 自身不加额外延时，靠 72MHz 下的 GPIO 翻转速度即可满足 SSD1306 时序。

---

## 3. 快速开始

### 3.1 环境

- **VS Code + [EIDE](https://marketplace.visualstudio.com/items?itemName=cl.eide)（`cl.eide`）**：本工程的构建/烧录全部由 EIDE 驱动
- **arm-none-eabi-gcc**（开发时使用 10.2.1）
- 调试：`marus25.cortex-debug`（`launch.json` 提供 `jlink` 与 `openocd` 两套配置）
- 烧录器：OpenOCD（stlink）或 JLink，都从 `0x08000000` 起

### 3.2 打开与构建

用 VS Code 打开 **`egg-nano.code-workspace`**（推荐，里面已配好文件关联与扩展推荐），然后：

```
Ctrl+Shift+B                    # 任务 build（= eide.project.build）
任务: flash / build and flash   # 或直接按 EIDE 的 Build / Upload 按钮
```

`.vscode/tasks.json` 提供 5 个任务：`build` / `flash` / `build and flash` / `rebuild` / `clean`。

产物位于 `build/Debug/`：

| 文件 | 说明 |
| --- | --- |
| `stm32f1xx_gcc.elf` | 带调试信息，`launch.json` 默认加载它 |
| `stm32f1xx_gcc.hex` / `.bin` | 烧录镜像 |
| `stm32f1xx_gcc.map` | 链接映射表（核对内存占用就看它） |

### 3.3 使用

打开串口助手（115200 8N1，**关闭本地回显**，发送时用 `\r\n` 结尾），输入一行中文问题回车即可。
生成期间 OLED 页眉右侧会走 `.` `..` `...` 动画，正文随字符逐个上屏。

---

## 4. 目录结构

```
egg-nano/
├─ .clang-format                代码风格（Microsoft 基座，4 空格，不限列宽）
├─ .gitignore
├─ egg-nano.code-workspace      VS Code 工作区（EIDE 配置提供者 + 扩展推荐）
├─ stm32f1x_64KB_flash.ld      链接脚本（FLASH/RAM 布局，见 §9.1 注意事项）
├─ .eide/
│  ├─ eide.yml                 工程定义：芯片/宏/头文件路径/排除源文件/存储器/烧录器
│  └─ files.options.yml        按文件的额外编译选项（当前为空）
├─ .vscode/
│  ├─ tasks.json               build / flash / rebuild / clean
│  └─ launch.json              cortex-debug：jlink、openocd（本地文件，默认已 gitignore）
├─ src/                        应用与驱动（全部工程代码）
│  ├─ main.c                   主循环：收行 → 拼 prompt → 生成 → 串口 / OLED 输出
│  ├─ nano_model.c/.h          int8 推理核（纯 C99，无 HAL 依赖）
│  ├─ nano_uart.c/.h           USART1 逐行输入 + DWT 毫秒计时 + 回显
│  ├─ nano_ui.c/.h             OLED 排版（UTF-8 折行、页眉、思考动画）
│  ├─ eggnano_weights.c/.h     【生成】量化权重 int8 + f32 标度/bias/LN
│  ├─ nano_lut.c/.h            【生成】GELU 查表（161 点）
│  ├─ font_hz12.c/.h           【生成】12×12 中文子集字库（303 字）
│  ├─ OLED.c/.h                SSD1306 驱动 + 6×8 ASCII 字模 + 帧缓冲
│  ├─ IIC.c/.h                 软件 IIC 主机
│  ├─ Delay.c/.h               SysTick 忙等延时
│  ├─ startup_stm32f10x_md.s   启动文件（MD 密度器件）
│  ├─ stm32f10x_it.c/.h        Cortex-M3 异常处理模板
│  └─ stm32f10x_conf.h         标准外设库配置
├─ hal/STM32F10x_StdPeriph_Driver/   ST 标准外设库 V3.5.0（inc + src）
├─ lib/cmsis/                  CMSIS Cortex-M3 头文件（core_cm3.h、arm_math.h 等）
└─ build/                      【生成，已 gitignore】编译中间件与产物
```

---

## 5. 模型规格

### 5.1 超参数（定义在 `src/eggnano_weights.h`）

| 宏 | 值 | 含义 |
| --- | --- | --- |
| `NANO_VOCAB` | 256 | 字节级词表（UTF-8 字节流直接当 token） |
| `NANO_SEQ` | 48 | 上下文窗口（字节） |
| `NANO_EMBD` | 32 | 隐藏维 |
| `NANO_HEADS` / `NANO_HEADDIM` | 4 / 8 | 注意力头数 / 每头维度 |
| `NANO_LAYERS` | 3 | Transformer 层数 |
| `NANO_FFN` | 96 | 前馈隐层维 |
| `NANO_EOS` | `0xFF` | 结束符（贪心解码遇到即停） |

参数量约 **41 664**：`tok_emb` 256×32 = 8192；每层 qkv 96×32 + out 32×32 + fc1 96×32 + fc2 32×96 = 10 240（×3 层 = 30 720）；
`pos_emb` 48×32 = 1536；LN / bias 共约 1216（f32）。

### 5.2 量化与前向配方

```
x = tok_emb 查表反量化 + pos_emb
每层：LN1 → per-row 量化 → QKV int8 GEMM → 量化存 K/V → int8 点积算分数
      → f32 softmax → f32 加权 V → 量化 → out_proj GEMM → 残差
      → LN2 → 量化 → fc1 GEMM → GELU 查表 → 量化 → fc2 GEMM → 残差
末位置：LN_f → tied lm_head（tok_emb 列视角）→ int32 acc 直接 argmax
```

- **权重量化**：per-tensor `int8` + f32 `scale`（`w = scale × q`），torch Linear 行主序布局
- **激活量化**：per-row 动态量化，`scale = max(amax, 1e-12) / 127`
- **舍入纪律**：away-from-zero —— `floor(|v| + 0.5) · sign(v)`，**禁用 banker's rounding**，与 PC 参考实现一致
- **权重绑定**：`tok_emb` 同时充当输入嵌入与 `lm_head`（列视角复用同一份 int8），省下 8KB
- **LayerNorm / bias**：保持 f32（对精度敏感且体积小）
- **溢出核算**：`|acc| ≤ 127×127×Cin ≤ 127×127×96 ≈ 1.55M ≪ 2^31`，**int32 普通累加即安全，不需要 `__SSAT` 饱和**
- **GELU**：161 点查表 + 线性插值（表外左侧取 0、右侧按斜率 1 延伸），误差约 `1e-3`，小于 int8 量化噪声 `4e-3`
- **softmax / LN**：用 `expf` / `sqrtf`（libm，nano.specs）

### 5.3 MCU 与 PC 参考实现的有意差异

K/V 缓存以 **int8 + per-position 标度**存储（PC 参考实现用 f32），为的是省 RAM。
误差约 0.4%，与权重量化本身同量级，不影响生成行为。

### 5.4 KV cache 与快慢路径

- 因果掩码保证历史位置的 K/V 不变 → 算过一次就能一直复用，缓存跨步持久（`s_kc` / `s_vc`）
- **快路径**：窗口未满，位置号 = 绝对位置，缓存全部有效 → 只前向新位置
- **慢路径**：窗口（48 字节）滑满后，位置嵌入是**绝对位置**，滑动会让所有幸存字节的索引整体 -1
  → 缓存整体失效 → 走**全量重算**，保证与旧版逐字节一致

输出与"每次重算整个窗口"的实现**逐位相同**。

---

## 6. 内存账本（实测，来自 `build/Debug/stm32f1xx_gcc.map`）

### 6.1 Flash（64KB = 65536 B）

| 段 | 大小 | 说明 |
| --- | --- | --- |
| `.isr_vector` | 268 B | 中断向量表 |
| `.text`（代码 + rodata） | 64 628 B | 见下方拆分 |
| `.data`（初值存于 flash） | 124 B | 启动时拷到 RAM |
| **镜像合计（`.bin`）** | **65 028 B** | **占 99.2%，仅余 508 B** |

`.text` 主要构成：

| 项目 | 大小 |
| --- | --- |
| int8 权重（tok_emb + pos_emb + 3 层 GEMM） | 40 448 B |
| f32 参数（LN / bias / pos scale / LN_f） | 5 056 B |
| 12×12 中文子集字库（码点表 + 点阵） | 7 878 B |
| GELU 查表 | 644 B |
| 6×8 ASCII 字模 | 570 B |
| 代码 + libm/libc（`expf`、`sqrtf` 等） | ≈ 10 032 B |

> **Flash 已经贴顶**：新增功能前请先看 §9.1。链接器还通过 `gc-sections` 丢弃了 246 个未引用段。

### 6.2 RAM（20KB = 20480 B）

| 项目 | 大小 |
| --- | --- |
| `.data` | 124 B |
| `.bss` | 14 192 B |
| **静态占用合计** | **14 316 B（0x37EC）** |
| 栈可用空间（`0x200037EC` → `0x20005000`） | 6 164 B |
| 链接期栈下限检查（`_Min_Stack_Size`） | 256 B |

`.bss` 拆分 —— 推理工作区（`nano_model.c`）共 **12 928 B ≈ 12.6KB**：

| 变量 | 大小 | 用途 |
| --- | --- | --- |
| `s_kc` / `s_vc` | 4608 + 4608 B | 3 层 × 48 位置 × 32 维 int8 K/V 缓存 |
| `s_kcs` / `s_vcs` | 576 + 576 B | 每位置的 K/V 标度 |
| `s_logits` | 1024 B | 末位置 int32 logits（256 词表） |
| `s_sc` | 768 B | 注意力分数 → softmax 权重（原址复用） |
| `s_x` / `s_row` / `s_proj` / `s_act` / `s_q` | 128 + 384 + 128 + 96 + 32 B | 残差流、行缓冲、投影输出、int8 激活、int8 Q 行 |
| 其他（`OLED_Buffer` 1024 B、串口缓冲 96 B、`main.c` 静态变量等） | 1 264 B | — |

---

## 7. 串口协议

### 7.1 输入

- 一行一条问题，以 `\r` 或 `\n` 结束；`\r\n` 会被识别为一次换行（不会触发两次空问题）
- 接收缓冲 `NANO_RX_MAX = 96` 字节，主循环侧 `question[64]`
- 支持退格（`0x08` / `0x7F`）；**收到即回显**，本地无回显的终端也能正常使用
- 空行不触发推理（`s_len > 0` 才置 ready）

### 7.2 输出时序（便于上位机同步解析）

```
回显(问题 + CRLF)      <- ISR 里逐字回显，回车时补 CRLF
"nano> "               <- 固定前缀，此后开始逐字节流出回答
回答正文(逐字节)         <- 每生成一字节立即发出，与 OLED 刷新同步
CRLF + "[<时长>s <字数>B]" + CRLF
dbg clk sysclk=.. hclk=.. pclk2=..
dbg prompt[<长度>]=<十六进制字节>       <- 实际喂给模型的 prompt（含"问：""\n答："标记）
dbg top3=<首字节 logits 前三>          <- 与 PC 侧 check_mcu_math.py 对照，定位数值偏差
dbg hex=<回答十六进制>
```

时长格式为 `dd.d`（百分之一秒位），例如 `03.7`，其后紧跟 `s`。诊断行统一以 `dbg ` 开头，**都在状态行之后**，不干扰上位机解析。

### 7.3 prompt 构造

```c
prompt = "问：" + 问题 + "\n答："      // 标记写成 UTF-8 字节数组，不依赖源码字符集
```

- 只保留**最后 48 字节**（`ctx[-48:]`），与 PC 端训练/验收时的裁剪方式完全一致
- 问题是字节流，UTF-8 被截断也照常处理（模型本身就是 byte-level）

---

## 8. OLED 排版

| 区域 | 行 | 内容 |
| --- | --- | --- |
| 页眉 | 0–7（第 0 页） | 左：`egg-nano` 标识；右：本轮时长+字节数，或思考动画（右对齐） |
| 正文 | 12–59（4 行 × 12px） | 上半：用户问题（`"> "` 前缀，最多 2 行，超出截断）；空一行后是回答 |

- 中文用 **12×12 子集字库**（303 字，横向取模，`bit15` = 最左像素）；ASCII 用 6×8
- **12px 行不落在 SSD1306 的页边界上**（一页 8 行），所以正文必须按**绝对 y 逐点写帧缓冲**；
  页眉刚好占满第 0 页，可以直接 `OLED_ShowString`
- 回答可见行数不足时**滚动显示最后几行**
- 刷新时机：`on_byte` 只在收到**新字符的首字节**（非 UTF-8 续字节 `10xxxxxx`）时刷一次屏，
  尾部不完整的半个汉字会被 `utf8_next` 丢弃。效果是**每个字在下一个字到来时上屏**，
  最后一个字由生成结束后的收尾刷新补上（`main.c` 在 `Nano_Generate` 返回后又调了一次 `NanoUI_Answer`）
- **思考动画只刷第 0 页**（约 7ms），全屏刷新约 60ms —— 省掉约 90% 的 IIC 开销

---

## 9. 注意事项与已知问题

### 9.1 Flash 只剩 508 字节 ⚠️

`stm32f1x_64KB_flash.ld` 里写的是 `FLASH LENGTH = 128K`，但目标芯片（STM32F103C8 / 器件配置 `STM32F102C8`）
**只有 64KB**。当前镜像 65 028 B 恰好塞进 64KB，**再加代码不会报链接错误，但烧进去会跑飞**。

改动时请以 `.bin` / `.map` 为准自行核对；若要长期开发，建议把链接脚本的 `FLASH LENGTH` 改成 `64K`,
让链接器帮忙把关。

### 9.2 生成的源码文件

以下文件头部都标了"**自动生成，勿手改**"，它们由 **PC 端脚本 `scripts/nano/` 生成/校验**：

| 生成物 | 脚本 |
| --- | --- |
| `src/eggnano_weights.c/.h` | `scripts/nano/quantize_i8.py`（内含 numpy 参考实现 `forward_int8`，即"规格书"） |
| `src/nano_lut.c/.h` | `scripts/nano/make_lut.py` |
| `src/font_hz12.c/.h` | `scripts/nano/make_oled_font.py`（字符集 = SFT 语料答区 ∪ 实测生成输出 ∪ 常用标点） |
| MCU 数值校验 | `scripts/nano/check_mcu_math.py`（与串口 `dbg top3` 对照） |

> ⚠️ **`scripts/` 目录不在本仓库内**（训练、量化、字库生成的完整工具链在别处）。
> 因此这些生成文件已直接入库，**没有它们就不要再改动模型结构或字库**。

### 9.3 其他小事

- `nano_uart.h` 里声明的 `NanoUart_IrqHandler` 是**历史遗留**：实际由 `nano_uart.c` 直接强定义
  `USART1_IRQHandler` 覆盖启动文件里的 weak 默认实现，无需改 `stm32f10x_it.c`
- `NanoUart_SendBytes()` 目前**未被调用**（已被 `gc-sections` 丢弃），保留备用
- `stm32f10x_it.c` 只实现了内核异常处理，外设 ISR 是注释模板
- `.eide/eide.yml` 排除了 15 个用不到的外设模块（bkp/can/cec/crc/dac/dbgmcu/dma/flash/fsmc/i2c/
  iwdg/pwr/rtc/sdio/wwdg），剩下的也基本被 `gc-sections` 回收
- 未添加 `LICENSE` 文件：仓库内的 ST 标准外设库、CMSIS、字模资源各有其原始许可，发布前请先确认

---

## 10. 构建配置速查

来自 `.eide/eide.yml`（target `Debug`）：

| 项目 | 值 |
| --- | --- |
| 工具链 | GCC（arm-none-eabi-gcc 10.2.1） |
| CPU | Cortex-M3，`softfp` |
| 宏 | `USE_STDPERIPH_DRIVER`、`STM32F10X_MD` |
| 头文件路径 | `src`、`lib/cmsis`、`hal/STM32F10x_StdPeriph_Driver`、`.../inc`、`.../src` |
| C 标准 / 优化 | `c11` / `level-debug`（`-Og`） |
| 警告 | `all-warnings` |
| 代码生成 | `-ffunction-sections -fdata-sections` + `--gc-sections` |
| 链接 misc | `--specs=nosys.specs --specs=nano.specs` |
| 库 | `-lm` |
| 分散加载 | 自定义 `.ld`（`stm32f1x_64KB_flash.ld`） |
| 烧录 | OpenOCD（interface `stlink`，target `stm32f1x`）；JLink 亦可，均自 `0x08000000` |

代码风格见 `.clang-format`：Microsoft 基座 + Linux 大括号 + 4 空格 + 不限列宽 + 连续赋值/宏对齐。

---

## 11. 第三方组件

| 组件 | 位置 | 来源 |
| --- | --- | --- |
| STM32F10x Standard Peripheral Library V3.5.0 | `hal/STM32F10x_StdPeriph_Driver/` | STMicroelectronics |
| CMSIS Cortex-M3 核心头文件（含 `arm_math.h`） | `lib/cmsis/` | ARM |
| 6×8 ASCII 字模、SSD1306 初始化序列、软件 IIC 结构 | `src/OLED.c`、`src/IIC.c`、`src/Delay.c` | 参照 "Mini-OLED" |
| 12×12 中文字模 | `src/font_hz12.c` | 由 `scripts/nano/make_oled_font.py` 生成 |

---

## 12. 设计要点小结

1. **量化纪律可复现**：舍入、EPS 下限、激活标度全部与 PC 参考实现逐条对齐，MCU 与 PC 输出可逐字节比对
2. **KV cache + 滑动窗口双路径**：快路径省算力，窗口满时退回全量重算，保证与最朴素实现逐位一致
3. **内存换 Flash**：`tok_emb` 权重绑定、12×12 子集字库、GELU 查表，把 4.2 万参数塞进 64KB
4. **片上自证**：串口诊断行（prompt 字节 / top-3 logits / hex 输出）让数值问题可定位到"是否第一步就错"
5. **交互体验优先**：边生成边刷屏（思考动画只刷页眉一页）、字节级流式输出、本地回显
