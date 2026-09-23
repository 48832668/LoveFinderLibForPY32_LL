# LoveFinderLibForPY32_LL

面向 **PY32F003 + LL 库（无 HAL）** 的 C++17 外设驱动库。

设计参照 `LoveFinderLibForSTM32_HAL`（库组织约定）与
`LoveFinder830-PickSoul`（ST7735 / FontLib 的 API 与字符级编译字库），
底层实现从 STM32 HAL 移植到 PY32 LL。

## 模块一览

| 模块 | 内容 | 硬件依赖 |
|------|------|---------|
| `ST7735/` | ST7735 彩屏驱动（C++17，完整图形 API） | SPI + 4 根 GPIO |
| `BUTTON/` | EXTI 按键驱动（单击/双击/长按 + 长按阈值可调 + 充能） | 1 根带 EXTI 的 GPIO |
| `FontLib/` | 字符级编译字库（驱动无关） | 无 |

## ⚠️ 架构：库共享 —— 像素数据在库，编译清单在工程

本仓库的用法是 **一个共享库 + 每个工程自带一份「要编译哪些字」的清单**：

```
LoveFinder851-EcoCheatOneT1/                 ← 仓库根
├── LoveFinderLibForPY32_LL/                ← 共享库（唯一副本）
│   ├── ST7735/                             ← 驱动：所有工程共用这一份
│   └── FontLib/                            ← ★ 字形像素数据：所有工程共用这一份
│       ├── font_manifest.json              ← 唯一真相源（字体名册 + 全部像素）
│       ├── font_data.cpp                   ← 全部字形的逐字符数组 + 查表（生成物）
│       └── font.h                          ← 固定接口：字体名册 + 查表 API（生成物）
└── examples/LL/LCD_Test/                   ← 一个例程 = 一个完整工程
    └── LoveFinderLib/
        └── FontLib/
            └── font_config.hpp             ← ★ 本工程专属：只声明「要编译哪些字」
```

**工程里唯一的字库文件就是 `font_config.hpp`**，它只有两部分：
`USE_FONT_*` 开关 + `FONT_*_CHARS(X)` 字符清单。没有任何字形像素数据。

### 为什么像素数据能放在库里

库的 `font_data.cpp` 持有该字体**全部**字形的 `static` 数组，但查表函数的
switch 只展开工程清单里的字符：

```cpp
static const uint16_t* font_lookup_7x10(uint8_t ch) {
    switch (ch) {
#define FONT_CASE(cp) case cp: return g_7x10_cp##cp;
        FONT_7X10_CHARS(FONT_CASE)      // <- 来自工程的 font_config.hpp
#undef FONT_CASE
        default: return nullptr;
    }
}
```

未被清单引用的 `static` 数组**由编译器直接丢弃**，不占 Flash ——
不需要链接脚本、不需要构建前脚本。

> **已实测**（armclang V6.24 / Cortex-M0+ / `-O2`，库侧持有全部 95 个字形）：
>
> | 工程清单 | map 中实际放置的字形 | 字形 RO |
> |---|---|---|
> | 25 字符 | 25 个 | 500 B |
> | 95 字符 | 95 个 | 1900 B |
>
> 关掉 LTO 结果相同（`-O2` 下编译器自己就删了未引用的 `static const`）。

### 铁律：库的 `FontLib/` 里绝不能出现 `font_config.hpp`

库内文件写 `#include "font_config.hpp"`。因为库目录里**没有**这个文件，
它会经 `-I` 路径解析到**当前工程**的清单 —— 这就是「完整剥离」的接口。

一旦库目录里也放一份 `font_config.hpp`，它会因同目录优先而被命中，
所有工程就会共用同一份清单 —— 编译清单的工程独立性立刻失效。

**工程的 include path 应当同时包含三者**：

```
<工程>/LoveFinderLib/FontLib                 ← 工程的编译清单（font_config.hpp）
<仓库根>/LoveFinderLibForPY32_LL/FontLib     ← 共享的像素数据与接口
<仓库根>/LoveFinderLibForPY32_LL/ST7735      ← 共享驱动
```

### Keil 工程怎么接

- 文件组里 `font_data.cpp` 指向 **`../../../../LoveFinderLibForPY32_LL/FontLib/font_data.cpp`**
- **不要**再往工程里放 `fonts.cpp` / `font.h` / `font_data.h` / `font_manifest.json`

### PickSoul

PickSoul 用**库的** `font_manifest.json` 编辑字形；改了库里的像素，
**所有引用该库的例程重新编译后一起更新** —— 这正是这套架构的目的。

> 实测：`examples/LL/LCD_Test`（25 字符）与 `examples/LL/LCD_DMA_Test`（62 字符）
> 编译的是**同一个** `font_data.cpp`，固件里各自只出现自己清单里的字形。

## 目录结构

```
LoveFinderLibForPY32_LL/
├── ST7735/
│   ├── st7735.hpp        # 常量(namespace ST7735) + 平台移植点 + API
│   ├── st7735.cpp        # 实现：初始化 / 字符 / 图形 / SPI
│   ├── icons.c / icons.h # 图标库（数据，保持 C）
│   └── README.md
├── BUTTON/
│   ├── BUTTON.hpp        # 宏默认值 + 平台移植点 + Button 类
│   ├── BUTTON.cpp        # 实现：EXTI 配置 / 状态机 / 充能 / 中断分发
│   └── README.md
├── FontLib/              # ★ 字形像素数据（所有工程共享的唯一副本）
│   ├── font.h            # 固定接口：字体名册 + 查表 API（生成物）
│   ├── font_data.cpp     # 全部字形的逐字符数组 + 查表（生成物）
│   └── font_manifest.json# PickSoul (FontHub Editor) 的唯一真相源
│   ⚠ 本目录【绝不能】出现 font_config.hpp —— 那是工程的文件
├── README.md
└── LICENSE               # MIT
```

各工程的 `LoveFinderLib/FontLib/` 里只有一个 `font_config.hpp`（编译清单）。

生成方式：

```powershell
python tools\gen_fontlib.py        # 从 FontLib/font_manifest.json 生成
                                   #   FontLib/font.h + FontLib/font_data.cpp
```

## 约定

- **命名空间**：常量放 `namespace ST7735 { constexpr ... }`，
  同时提供 `ST7735_XXX` 宏别名以兼容旧写法
- **平台移植点**：每个模块头部单独标注「唯一需要修改的地方」，用宏绑定硬件，
  **没有运行时配置函数**
- **字库是生成物**：库的 `font.h` / `font_data.cpp` 由 `tools/gen_fontlib.py`
  从 `FontLib/font_manifest.json` 生成，**不要手工编辑**；
  工程的 `font_config.hpp` 是**手写/由 PickSoul 维护**的编译清单，可以改
- **厂商无关性**：库不依赖 HAL，也不依赖任何工程私有的 `main.h` 内容，
  只要求工程提供 `main.hpp`（LL 驱动头）与 `gpio.hpp`（板级引脚）

## 快速上手（Keil 为例）

1. 把 `ST7735/st7735.cpp`、`ST7735/icons.c`、`FontLib/font_data.cpp` 加入工程
   （要用按键再加 `BUTTON/BUTTON.cpp`）
   - `.cpp` 的 `<FileType>` 必须是 **8**（C++），`.c` 是 1
   - `font_data.cpp` 指向 **库里的那一份**（所有工程共享），**不要**往工程里复制
2. Include 路径加三处：`<工程>/LoveFinderLib/FontLib`（编译清单）、
   库的 `FontLib/`（像素与接口）、库的 `ST7735/`（驱动）
   （要用按键再加库的 `BUTTON/`）
3. 工程 C/C++ 选项加 **`-Wno-register`**（厂商 LL 头用了 C++17 已删除的
   `register` 关键字），并把 `<v6LangP>` 设为 **9**（`-std=gnu++17`）
4. 在 `<工程>/LoveFinderLib/FontLib/font_config.hpp` 里声明要编译哪些字
5. 外设初始化完成后调用：

```cpp
#include "st7735.hpp"

ST7735_Init();                                    // 含背光使能 + 面板反色
ST7735_FillScreen(ST7735::BLACK);
ST7735_WriteString(40, 12, "Hello", Font_7x10, ST7735::WHITE, ST7735::BLACK);
ST7735_FillCircle(80, 40, 20, ST7735::GREEN);
ST7735_DrawRoundRect(10, 60, 60, 16, 5, ST7735::CYAN);
```

要用按键：

```cpp
#include "BUTTON.hpp"

LoveFinderLib::Button btn;
btn.init();                                       // 板级 KEY_INT(PA1)，自动配 EXTI + NVIC

// 主循环 (1-10ms 一次)
LoveFinderLib::e_BUTTON_Event evt = btn.update();
btn.setLongPressMs(1500);                         // 运行期改长按阈值

// 工程的 EXTI 中断里分发 (详见 BUTTON/README.md)
void EXTI0_1_IRQHandler(void) { LoveFinderLib::Button::dispatchExti(LL_EXTI_LINE_1); }
```

## 平台移植点

`st7735.hpp` 里的这一段是**换板子唯一要改的地方**：

```cpp
#define ST7735_SPI_INSTANCE   SPI1      // LL 用外设实例，不是 HAL 句柄

#define ST7735_CS_Port        LCD_CS_Port     // 来自工程 gpio.hpp
#define ST7735_CS_Pin         LCD_CS_Pin
#define ST7735_DC_Port        LCD_DC_Port
#define ST7735_DC_Pin         LCD_DC_Pin
#define ST7735_RES_Port       LCD_RESET_Port
#define ST7735_RES_Pin        LCD_RESET_Pin
#define ST7735_EN_Port        LCD_EN_Port     // 背光，可选
#define ST7735_EN_Pin         LCD_EN_Pin
```

面板几何 / 方向 / 反色在同一个文件的 `namespace ST7735` 里：

```cpp
constexpr uint8_t  XSTART   = 1;      // 列偏移
constexpr uint8_t  YSTART   = 26;     // 行偏移
constexpr uint16_t WIDTH    = 160;
constexpr uint16_t HEIGHT   = 80;
constexpr uint8_t  ROTATION = (MADCTL_MX | MADCTL_MV | MADCTL_BGR);  // 0x68
constexpr bool     INVERT   = true;   // 需要 INVON
```

> 当前值是本开发板（PY32F003F18U6-E + 0.96" 160x80）实测可用配置。
> 参照工程 PickSoul 的另一批面板是 `XSTART=0, YSTART=24, ROTATION=0xA8, INVERT=false`。
> **换屏后显示错位 / 颜色反了，先动这几个值。**

## 从 HAL 移植时的改动（对照 PickSoul / STM32 版）

| STM32 HAL 写法 | PY32 LL 写法 |
|----------------|-------------|
| `HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET)` | `LL_GPIO_ResetOutputPin(port, pin)` |
| `HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET)` | `LL_GPIO_SetOutputPin(port, pin)` |
| `HAL_SPI_Transmit(&hspi1, buf, len, HAL_MAX_DELAY)` | 库内 `st7735_spi_write(buf, len)` |
| `HAL_Delay(ms)` | `LL_mDelay(ms)` |
| `HAL_SPI_Transmit_DMA(...)` | **暂未实现**，见下 |

### `st7735_spi_write()` 为什么这么写

这是 `HAL_SPI_Transmit` 的 LL 等价实现，三件事缺一不可
（都是 HAL 内部做了、手写 LL 时容易漏的）：

1. **传输前后关/开 SPE** —— 复位 SPI 内部 TX/RX FIFO 与 OVR 状态
2. **结束等 `BSY` 清零** —— 否则紧接着翻 CS 会截断最后一个字节，表现为偶发花屏
3. **清 OVR** —— 全双工模式从不读 DR，RX FIFO 会满并置 OVR

> 这三条是踩坑换来的，详见 `examples/LL/LCD_Test/README.md`。

## 字符级编译字库（本项目统一使用）

本库的字体**全部是字符级编译** —— 不存在「完整字库 / 子集字库」两套并存的字体，
一套字体就一套像素，编译多少由工程清单决定。

- **只有**工程 `font_config.hpp` 里 `FONT_7X10_CHARS(X)` 列出的字符会被编译进固件
- 未列出的字符渲染时**自动跳过**（不显示也不占位），其余字符不受影响
- 库的 `font_data.cpp` 里保存着该字体的**全部 95 个字形**，未引用的由编译器丢弃

```c
/* <工程>/LoveFinderLib/FontLib/font_config.hpp —— 工程里唯一的字库文件 */
#define USE_FONT_7X10        1                                  /* 启用该字体 */
#define FONT_7X10_CHARS_STR  " !1CDEHLOTW_acdehlnoprstx"        /* 可读形式 */
#define FONT_7X10_CHARS(X)   X(32) X(33) X(49) ... X(120)       /* 编译用 */
```

渲染时用 `Font_7x10`（库内默认字体 `ST7735_DEFAULT_FONT` 已指向它）：

```cpp
ST7735_WriteString(40, 12, "exp1_LCD_Test", Font_7x10, ST7735::WHITE, ST7735::BLACK);
```

### 效果（实测）

| 工程 | 清单字符数 | 固件中字形 | 字形 RO |
|------|-----------|-----------|---------|
| `examples/LL/LCD_Test` | 25 | 25 | 500 B |
| `examples/LL/LCD_DMA_Test` | 62 | 62 | 1240 B |
| （若清单列满 95 字符） | 95 | 95 | 1900 B |

字符集越小收益越大 —— 这是 PickSoul 的核心价值。

### 增删字符

**改库里的像素**（影响所有例程）：用 PickSoul 编辑库，然后
`python tools\gen_fontlib.py` 重新生成库的 `font.h` / `font_data.cpp`。

**只改本工程编译哪些字**（不影响别的例程）：直接编辑该工程的
`font_config.hpp`，改完重新编译即可 —— 无需任何生成步骤。

## 已知限制

- **`_DMA` 后缀函数目前退化为阻塞传输**。PY32 的 DMA（DMA1_Channel3 → SPI1_TX）
  尚未接入，`ST7735_FillScreen_DMA` 等 API 功能正确但没有性能收益。
  不用 DMA 的例程（如 `examples/LL/LCD_Test`）不受影响。
  等 `examples/HAL/LCD_DMA_Test` 迁移时再补上真正的 DMA 实现。

## 历史陷阱：字库生成物的「行拼接」

**现象**：屏幕文字全部错位一格 —— `exp1_LCD_Test` 显示成 `fyq2`MDE`Uftu`，
而且是从反斜杠 `\`（ASCII 0x5C）这个字符之后开始错。

**根因**：旧生成器为反斜杠字符输出的注释是 `// \`。
C/C++ 里**行尾的反斜杠是「行拼接」（line splicing，翻译阶段 2）**，
而它**早于注释移除（翻译阶段 3）**执行 —— 于是下一行整个被拼进注释里，
字模数据被静默吃掉。

**现状：已从构造上免疫。** `tools/gen_fontlib.py` 把字符写在**块注释**里，
行末永远不会落在反斜杠上：

```c
static const uint16_t g_7x10_cp92[10] = {   /* \ */     ← 行末是 '/'，安全
    0x2000, 0x2000, 0x1000, 0x1000,
    ...
```

**验证方法**（保留下来做回归检查）：查 map 里字形数组的数量与总大小。
例如 95 字符 × 10 行 × 2 字节应当正好是 **1900** 字节；
少 20 字节就说明有字形被吃掉。`tools\fix_font_line_splice.ps1`
仅对**旧格式**的字库文件还有意义。

## 许可证

MIT License，见 `LICENSE`。
