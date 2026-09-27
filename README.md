# egg-nano

在一颗 **STM32F103C8**（Cortex-M3 @72MHz，**64KB Flash / 20KB RAM**）上跑一个 3 层 int8 自回归语言模型：
**串口收问题 → 片上推理 → OLED 显示回答**。全部推理在 MCU 本地完成，无外部算力、无操作系统。

```
问：你好            <- 串口逐字回显
nano> 你好！很高兴认识你。   <- 边生成边流出，同时刷 OLED
[03.7s 39B]
```

## 项目特点

- **模型**：byte-level（vocab=256）GPT 式解码器，3 层 / 隐藏维 32，约 **4.2 万参数**，int8 量化，贪心解码
- **能力**：中文短句问答（训练语料风格），**不是通用助手**。回答 ≤60 字节，遇 `0xFF`(EOS) 停止；
  训练过的问题按记忆回答，不懂的会诚实拒答（算术说"自己算。"，不会的说"我不知道。"）
- **显示**：SSD1306 128×64 OLED（软件 IIC），12×12 中文子集字库，边生成边刷屏
- **极限**：int8 权重 + f32 伴随数据约 **48.9KB**，连同推理代码塞进 64KB Flash
- 模型超参数定义在 `src/eggnano_weights.h`（`NANO_*` 宏）

---

## 硬件与接线


| 项目 | 参数                                                                          |
| ------ | ------------------------------------------------------------------------------- |
| MCU  | STM32F103C8（Cortex-M3 @72MHz，64KB Flash，20KB RAM，LQFP48）                 |
| 串口 | USART1：**PA9 = TX**、**PA10 = RX**，115200 8N1                               |
| OLED | SSD1306 128×64，软件 IIC：**SCL = PA6 / SDA = PA5**（开漏输出），地址 `0x78` |
| 调试 | SWD（JLink 或 ST-Link + OpenOCD）                                             |

---

## 快速开始

**环境**：VS Code + [EIDE](https://marketplace.visualstudio.com/items?itemName=cl.eide) 插件 + `arm-none-eabi-gcc`（开发时用 10.2.1）。

1. 用 VS Code 打开 **`egg-nano.code-workspace`**（已配好扩展推荐与任务）
2. `Ctrl+Shift+B` 构建（或 EIDE 的 Build 按钮）；烧录用 EIDE 的 Upload / 任务 `flash`
3. 产物在 `build/Debug/`：


| 文件                         | 说明                             |
| ------------------------------ | ---------------------------------- |
| `stm32f1xx_gcc.hex` / `.bin` | 烧录镜像                         |
| `stm32f1xx_gcc.elf`          | 调试用（`launch.json` 默认加载） |
| `stm32f1xx_gcc.map`          | 链接映射表（核对内存占用）       |

**使用**：串口助手 115200 8N1（**关闭本地回显**，以 `\r\n` 结尾），输入一行中文问题回车即可。
生成期间 OLED 页眉右侧有 `.` `..` `...` 动画，回答随字符逐个上屏。

---

## 目录结构

```
egg-nano/
├─ .eide/                EIDE 工程配置（芯片/宏/头文件路径/烧录器）
├─ .vscode/              tasks（build/flash/clean）+ launch（调试）
├─ src/                  全部工程代码
│  ├─ main.c             主循环：收行 → 拼 prompt → 生成 → 串口/OLED 输出
│  ├─ nano_model.c/.h    int8 推理核（纯 C99，无 HAL 依赖）
│  ├─ nano_uart.c/.h     USART1 逐行输入 + 计时 + 回显
│  ├─ nano_ui.c/.h       OLED 排版（折行、页眉、思考动画）
│  ├─ eggnano_weights.c/.h  【生成】int8 量化权重（勿手改）
│  ├─ nano_lut.c/.h      【生成】GELU 查表（勿手改）
│  ├─ font_hz12.c/.h     【生成】12×12 中文子集字库（勿手改）
│  ├─ OLED.c/.h          SSD1306 驱动 + 6×8 ASCII 字模 + 帧缓冲
│  ├─ IIC.c/.h           软件 IIC 主机
│  ├─ Delay.c/.h         SysTick 忙等延时
│  └─ startup_*.s 等      启动文件、异常模板、标准外设库配置
├─ hal/STM32F10x_StdPeriph_Driver/   ST 标准外设库 V3.5.0
├─ lib/cmsis/            CMSIS Cortex-M3 头文件
└─ build/                【生成，已 gitignore】编译中间件与产物
```

---

## 注意

- **Flash 贴顶**：当前镜像约 **64.3KB / 64KB**，余约 **1.2KB**。链接脚本已按 `FLASH LENGTH = 64K` 设置，超限会**直接链接报错**（不会烧飞）。加功能前先看 `build/Debug/*.map`。
- **生成文件勿手改**：`eggnano_weights.*` / `nano_lut.*` / `font_hz12.*` 由 PC 端脚本（`scripts/nano/quantize_i8.py` 等）生成，**工具链不在本仓库**；没有工具链就不要动模型结构或字库。
- **串口输出格式**：回显问题 → `nano> ` 前缀 → 逐字节回答 → `[时长s 字节数B]` 状态行；`dbg ` 开头的是诊断行，不影响上位机解析。
- **编译优化用 `-Os`**（`.eide/eide.yml` 的 `level-size`）：比 `-Og` 省约 700B Flash。烧录后建议跑一次 MCU 数值校验（`check_mcu_math.py`）确认浮点行为未漂移。
