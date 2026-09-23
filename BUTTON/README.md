# LoveFinderLib :: BUTTON — 基于 EXTI 的稳定按键库（PY32 LL 版）

> 适用环境：**PY32F003 + LL 库（无 HAL）+ C++17**。
> 命名空间：`LoveFinderLib`
> 移植自 `LoveFinderLibForSTM32_HAL/BUTTON`，API 与行为完全一致，底层换成 PY32 LL。

本库用于稳定识别按键的 **单击 / 双击 / 长按**，并附带一个「长按充能」进度值，
同时支持在 **MCU 运行期间动态修改** 长按阈值与双击间隔阈值。

例程见 `LoveFinder851-EcoCheatOneT1/examples_LL/Button_Test`。

---

## 1. 特性

- ✅ 硬性要求：KEY 引脚启用 **EXTI 外部中断**，初始化时自动配置为 **按下沿触发**
  （`activeLow=true` → 下降沿）并自动使能对应 **NVIC 分组中断**
  （`EXTI0_1` / `EXTI2_3` / `EXTI4_15`）。
  - 即使工程已在 `Studio_GPIO_Init()` 里配好 EXTI，`init()` 仍会再显式配置一次，
    结果一致，因此「是否已用 PyStudio 配置」都不影响正确性。
- ✅ 稳定区分 **单击 / 双击 / 长按**（含软件消抖、双击窗口判定）。
- ✅ 长按阈值、双击间隔阈值：
  - **编译期**：通过头文件宏设定默认值；
  - **运行期**：通过 `setLongPressMs()` / `setDoubleClickMs()` 动态修改。
- ✅ 附带「长按充能」进度（charge meter）：
  - 长按（达到阈值后）持续充能；
  - **填满一次 → 长按次数 +1**，并清零继续充能（持续按住可多次累加）；
  - **未填满就松开 → 进度慢慢消退**。
- ✅ 提供事件标志位（event flags）与事件计数命名访问器，便于 UI 非破坏读取。
- ✅ 提供 `Button::dispatchExti(extiLine)`：工程的 EXTI 中断只需一行即可分发到按键对象。
- ✅ 纯 C++17 接口，位于 `LoveFinderLib` 命名空间。

---

## 2. 硬件要求

- 按键 IO 需有 **外部上拉**（按键按下为低电平，本板 KEY_INT = PA1 接 3V3 上拉）。
  - 库内还会再开 `LL_GPIO_PULL_UP` 内部上拉作为兜底，防止悬空误触发。
- 若按键是「按下为高电平」，把配置中的 `activeLow` 设为 `false`
  （此时库会自动改用上升沿触发 + 内部下拉）。

---

## 3. 目录结构

```
BUTTON/
├── BUTTON.hpp    // 头文件：宏定义、枚举、结构体、Button 类、命名空间
├── BUTTON.cpp    // 实现：EXTI 配置、状态机、充能/消退逻辑、中断分发
└── README.md     // 本文档
```

---

## 4. 移植到你的工程（唯一需要改的地方）

本库绑定的是【GPIO 外设实例 + 引脚掩码】，不是 HAL 句柄。依赖工程提供两样东西：

| 工程需提供 | 用途 |
|-----------|------|
| `main.hpp` | LL 驱动头（GPIO / EXTI / BUS）+ `BSP_GetTick()` 毫秒时基 |
| `gpio.hpp` | 板级引脚宏；定义了 `KEY_INT_Port` / `KEY_INT_Pin` 即可用板级默认引脚 |

`BUTTON.hpp` 头部的平台段：

```cpp
#include "main.hpp"

#if defined(__has_include)
#  if __has_include("gpio.hpp")
#    include "gpio.hpp"
#  endif
#endif

#ifndef BUTTON_KEY_Port
#define BUTTON_KEY_Port   KEY_INT_Port     // 本板 = GPIOA
#endif
#ifndef BUTTON_KEY_Pin
#define BUTTON_KEY_Pin    KEY_INT_Pin      // 本板 = LL_GPIO_PIN_1 (PA1)
#endif
```

- 工程 `gpio.hpp` 里**没有** `KEY_INT_Port/KEY_INT_Pin` 也能用，只是不能用
  `btn.init()` / `btn.init(cfg)` 这两个板级默认重载，必须显式传端口与引脚。
- 引脚换了只改 `gpio.hpp`（或 `-D` 覆盖 `BUTTON_KEY_Port/Pin`），库本身不动。

把 `BUTTON.cpp` 加入编译（Keil 里 `<FileType>` 必须是 **8** = C++），
`BUTTON/` 目录加进 Include Path 即可。

### 与 STM32 HAL 版的对应关系

| STM32 HAL 写法 | PY32 LL 写法 |
|----------------|-------------|
| `HAL_GPIO_Init(port, &gpio)` + `GPIO_MODE_IT_FALLING` | `LL_GPIO_Init()` + `LL_EXTI_SetEXTISource()` + `LL_EXTI_Init()` |
| `HAL_GPIO_ReadPin(port, pin)` | `LL_GPIO_IsInputPinSet(port, pin)` |
| `HAL_GetTick()` | `BSP_GetTick()` |
| `HAL_NVIC_SetPriority/EnableIRQ` | `NVIC_SetPriority/EnableIRQ` |
| `HAL_GPIO_EXTI_Callback(pin)` 里 `btn.onExti()` | EXTI 中断里 `Button::dispatchExti(LL_EXTI_LINE_x)` |

> `EXTI → NVIC 中断号` 的映射在库内按 PY32F003 的分组固定实现
> （0-1 → `EXTI0_1_IRQn`，2-3 → `EXTI2_3_IRQn`，4-15 → `EXTI4_15_IRQn`）。

### 与 STM32 HAL 原版的差异（除平台调用外）

API 与状态机逐行对齐，只多了下面这几处修正 / 补充：

| # | 项 | 说明 |
|---|----|------|
| 1 | **`Button::dispatchExti(extiLine)`** | PY32 LL 没有 HAL 那样的统一 EXTI 回调，库内自带实例表，工程的 ISR 一行搞定 |
| 2 | **`RELEASE` 标志位现在真的会置位** | 原版定义了 `BUTTON_Flag::RELEASE` 却从未置位；本版在释放时 `setFlag(RELEASE)`，且**不覆盖**本次的 CLICK / DOUBLE_CLICK / LONG_PRESS 主事件 |
| 3 | **修掉「长按后残留双击等待标志」** | 原版 `m_waitingSecondClick` 在长按分支里没清，导致「先短按、再长按」之后的下一次普通短按被误判成双击 |
| 4 | **阈值 0 保护** | `setLongPressMs(0)` / `setDoubleClickMs(0)` 会被钳到 1，避免充能计算里除零 |

---

## 5. 编译期默认阈值宏（头文件顶部）

这些宏决定 `BUTTON_Config::getDefault()` 的默认值，可在包含本头文件前用 `#define` 覆盖：

```cpp
#ifndef BUTTON_LONG_PRESS_MS_DEFAULT
    #define BUTTON_LONG_PRESS_MS_DEFAULT    1000    // 长按判定阈值 (ms)
#endif
#ifndef BUTTON_DOUBLE_CLICK_MS_DEFAULT
    #define BUTTON_DOUBLE_CLICK_MS_DEFAULT  300     // 双击间隔阈值 (ms)
#endif
#ifndef BUTTON_DEBOUNCE_MS_DEFAULT
    #define BUTTON_DEBOUNCE_MS_DEFAULT      20      // 消抖时间 (ms)
#endif
#ifndef BUTTON_CHARGE_FULL_MS_DEFAULT
    #define BUTTON_CHARGE_FULL_MS_DEFAULT   2000    // 长按充能填满时间 (ms)
#endif
#ifndef BUTTON_DECAY_MS_DEFAULT
    #define BUTTON_DECAY_MS_DEFAULT         3000    // 未填满松开后消退时间 (ms)
#endif
#ifndef BUTTON_ACTIVE_LOW_DEFAULT
    #define BUTTON_ACTIVE_LOW_DEFAULT       1       // 1 = 低电平按下 (外部上拉)
#endif
```

其它可调项：

```cpp
#define BUTTON_MAX_INSTANCES   4    // 同时存在的按键实例上限（EXTI 分发用）
#define BUTTON_IRQ_PRIORITY    2    // EXTI 中断优先级 (Cortex-M0+ 只有 2 位)
```

示例（编译期改阈值）：

```cpp
#define BUTTON_LONG_PRESS_MS_DEFAULT   1500
#define BUTTON_DOUBLE_CLICK_MS_DEFAULT 250
#include "BUTTON.hpp"
```

---

## 6. 运行时阈值修改（MCU 运行期间可动态调整）

即使初始化后，也能随时修改阈值，无需重新编译：

```cpp
btn.setLongPressMs(2000);      // 运行期把长按阈值改为 2000ms
btn.setDoubleClickMs(400);     // 运行期把双击间隔改为 400ms

uint16_t lp = btn.getLongPressMs();     // 读回当前长按阈值
uint16_t dc = btn.getDoubleClickMs();   // 读回当前双击间隔
```

> 传 0 会被钳到 1（避免除零）。当前完整配置可用 `getConfig()` 读回。

---

## 7. API 说明

### 7.1 配置结构体 `BUTTON_Config`

```cpp
struct BUTTON_Config {
    uint16_t debounceMs;      // 消抖时间 (ms)
    uint16_t longPressMs;     // 长按判定阈值 (ms)
    uint16_t doubleClickMs;   // 双击间隔时间 (ms)
    uint16_t chargeFullMs;    // 长按充能填满所需时间 (ms)
    uint16_t decayMs;         // 未填满松开后消退所需时间 (ms)
    bool activeLow;           // true=低电平按下, false=高电平按下
    static BUTTON_Config getDefault();   // 使用头文件宏默认值
};
```

### 7.2 事件枚举 `e_BUTTON_Event`

```cpp
enum class e_BUTTON_Event : uint8_t {
    NONE, CLICK, DOUBLE_CLICK, LONG_PRESS, PRESS_DOWN, RELEASE
};
```

### 7.3 事件标志位 `BUTTON_Flag`

```cpp
enum class BUTTON_Flag : uint8_t {
    PRESS_DOWN = 0x01, CLICK = 0x02, DOUBLE_CLICK = 0x04,
    LONG_PRESS = 0x08, RELEASE = 0x10
};
```

标志位在事件产生时置位并保持，直到被读取/清除，适合 UI 轮询：

```cpp
uint8_t f  = btn.peekFlags();                        // 非破坏读取全部标志
bool    ok = btn.testFlag(BUTTON_Flag::DOUBLE_CLICK);// 测试某位
uint8_t f2 = btn.getAndClearFlags();                 // 读取并清除
btn.clearFlags();                                    // 直接清除
```

### 7.4 计数器访问器

```cpp
uint32_t c = btn.getClickCount();        // 单击次数
uint32_t d = btn.getDoubleClickCount();  // 双击次数
uint32_t l = btn.getLongPressCount();    // 长按次数（充能填满次数）
```

### 7.5 长按充能

```cpp
uint8_t pct = btn.getChargePercent();    // 0..100，UI 据此绘制进度条
```

### 7.6 状态 / 物理

```cpp
bool isPressed() const;              // 当前是否按下
e_BUTTON_State getState() const;     // 内部状态
uint32_t getExtiLine() const;        // 该按键占用的 EXTI 线 (LL_EXTI_LINE_x)
bool isPin(uint32_t pinMask) const;  // 引脚是否匹配
```

---

## 8. 接入示例（完整）

```cpp
#include "BUTTON.hpp"
using namespace LoveFinderLib;

Button btn;

void app_init(void)
{
    BUTTON_Config cfg = BUTTON_Config::getDefault();
    cfg.activeLow   = true;      // 外部上拉，按下为低
    cfg.longPressMs = 1000;      // 长按阈值 1000ms
    btn.init(cfg);               // 板级默认引脚 KEY_INT(PA1)，自动配 EXTI + NVIC
}

void app_loop(void)              // 主循环，建议 1-10ms 调用一次
{
    e_BUTTON_Event evt = btn.update();

    if (btn.testFlag(BUTTON_Flag::DOUBLE_CLICK))
        doDoubleClickAction();

    uint8_t pct = btn.getChargePercent();   // 驱动充能进度
    draw_charge_bar(pct);

    // 运行期调整阈值示例：
    if (need_slower_threshold)
        btn.setLongPressMs(2500);
}
```

工程的 EXTI 中断里分发（**必须**，否则按下的下降沿不会被记录）：

```cpp
extern "C" void EXTI0_1_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_0) != 0U)
    {
        LL_EXTI_ClearFlag(LL_EXTI_LINE_0);
        /* PA0 FUSB_INT */
    }
    if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_1) != 0U)
    {
        /* PA1 KEY_INT —— dispatchExti 内部会清挂起标志并分发给按键对象 */
        LoveFinderLib::Button::dispatchExti(LL_EXTI_LINE_1);
    }
}
```

---

## 9. 注意事项

- **必须**在工程的 EXTI 中断里调用 `Button::dispatchExti(LL_EXTI_LINE_x)`
  （或自行对匹配的实例调用 `btn.onExti()`），否则按下的下降沿无法被记录。
- `update()` 需在主循环中**定期、高频**调用（建议 1~10ms），双击窗口（默认 300ms）
  依赖它计时；调用间隔大于双击窗口会导致单击被误判。
- 依赖 `BSP_GetTick()`（`main.hpp` 声明的 1ms 计数），它由 `SysTick_Handler()`
  自增。若该计数器不跑（SysTick 中断未使能），按键会完全无响应。
  `tempLate_LL/README.md` 的「补丁 1」就是补这个的。
- `Button::dispatchExti()` 会**先清 EXTI 挂起标志**；若同一中断向量里还有别的
  EXTI 线要处理，请像上面的例子那样先判断各自的标志位再分别处理。
- 同时存在的按键实例不能超过 `BUTTON_MAX_INSTANCES`（默认 4），
  超出的实例收不到中断分发。
- 本库为 **C++17**，请确保 `BUTTON.cpp` 在 Keil 里被当作 C++ 编译
  （`<FileType>8</FileType>`）。

---

*LoveFinderLib :: BUTTON (PY32 LL) — 2026*
