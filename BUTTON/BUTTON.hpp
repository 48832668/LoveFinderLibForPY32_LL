/**
 * @file BUTTON.hpp
 * @brief EXTI 驱动的稳定按键库 (LoveFinderLibForPY32_LL) - C++17
 *
 * 移植自 LoveFinderLibForSTM32_HAL/BUTTON（API 与行为保持一致），
 * 底层从 STM32 HAL 换成 PY32 LL（无 HAL）。
 *
 * 设计要点:
 *  - KEY 引脚必须启用 EXTI 外部中断（硬性要求）。
 *  - 初始化时自动配置为【下降沿触发】（activeLow = true，按键接 3V3 上拉，
 *    按下为低电平），并自动使能对应 NVIC 分组中断（EXTI0_1 / EXTI2_3 / EXTI4_15）。
 *    - 若工程已在 Studio_GPIO_Init() 里配好 EXTI，本库 init() 仍会再显式配置一次，
 *      结果一致，因此"是否已用 PyStudio 配置"都不影响正确性。
 *  - 硬件依赖: 按键 IO 需有外部上拉（activeLow = true），按下为低电平。
 *    （库内再加内部上拉作为兜底，防止悬空误触发。）
 *  - 通过 EXTI 下降沿中断 + 主循环状态机，稳定区分: 单击 / 双击 / 长按。
 *  - 长按阈值、双击间隔阈值可通过宏在【编译期】设定默认值，也可通过
 *    setLongPressMs() / setDoubleClickMs() 在【MCU 运行期间】动态修改。
 *  - 附带"长按充能"矩形区域 (charge meter):
 *      长按(达到阈值后)持续充能，填满一次 → 长按次数 +1 并清零继续充能；
 *      未填满松开 → 内部填充慢慢消退 (decay)。
 *  - 提供事件标志位 (event flags) 与事件计数命名访问器，便于外部/UI 读取：
 *      单击/双击/长按/按下/释放 各占一个标志位 bit，ISR 可置位 (setFlag)，
 *      UI 可非破坏读取 (peekFlags/testFlag) 或读取并清除 (getAndClearFlags)；
 *      计数可用 getClickCount()/getDoubleClickCount()/getLongPressCount() 直接读取。
 *
 * @author LoveFinder
 * @date 2026
 */

#ifndef LOVE_FINDER_LIB_BUTTON_HPP
#define LOVE_FINDER_LIB_BUTTON_HPP

#include <cstdint>

/*============================================================================
 * 平台移植点 (PORT) —— 唯一需要修改的地方
 *
 * 本库基于 PY32F003 + LL 库（无 HAL），因此绑定的是【GPIO 外设实例 + 引脚掩码】，
 * 而不是 HAL 句柄。换板子只改这一段。
 *
 * 依赖工程提供:
 *   main.hpp  —— LL 驱动头（GPIO/EXTI/BUS 等）与 BSP_GetTick() 毫秒时基
 *   gpio.hpp  —— 板级引脚宏（可选；有 KEY_INT_Port/KEY_INT_Pin 即可用板级默认引脚）
 *============================================================================*/
#include "main.hpp"

#if defined(__has_include)
#  if __has_include("gpio.hpp")
#    include "gpio.hpp"
#  endif
#endif

/* 板级默认按键引脚：工程 gpio.hpp 里的 KEY_INT（本板 = PA1，外部 3V3 上拉）。
   若工程没有这两个宏，就只能用显式 init(port, pin, cfg) 重载。 */
#if defined(KEY_INT_Port) && defined(KEY_INT_Pin)
#  ifndef BUTTON_KEY_Port
#    define BUTTON_KEY_Port   KEY_INT_Port
#  endif
#  ifndef BUTTON_KEY_Pin
#    define BUTTON_KEY_Pin    KEY_INT_Pin
#  endif
#  define BUTTON_HAS_BOARD_PINS   1
#else
#  define BUTTON_HAS_BOARD_PINS   0
#endif

/*============================================================================
 * 默认阈值宏定义 (编译期可改)
 *
 * 这些宏决定 BUTTON_Config::getDefault() 的默认值。
 * 若需在编译期统一调整，可在包含本头文件之前重新 #define 覆盖；也可在
 * MCU 运行期间通过 LoveFinderLib::Button::setLongPressMs() /
 * setDoubleClickMs() 动态修改 (见类方法)。
 *============================================================================*/
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

/* 同时存在的按键实例上限（用于 EXTI 中断自动分发到各实例）。 */
#ifndef BUTTON_MAX_INSTANCES
    #define BUTTON_MAX_INSTANCES            4
#endif

/* EXTI 中断优先级 (0 = 最高, Cortex-M0+ 只有 2 位优先级) */
#ifndef BUTTON_IRQ_PRIORITY
    #define BUTTON_IRQ_PRIORITY             2
#endif

namespace LoveFinderLib {

/*============================================================================
 * 按键事件枚举 (enum class)
 *============================================================================*/

enum class e_BUTTON_Event : uint8_t {
    NONE         = 0,   // 无事件
    CLICK        = 1,   // 单击
    DOUBLE_CLICK = 2,   // 双击
    LONG_PRESS   = 3,   // 长按 (达到阈值瞬间触发一次)
    PRESS_DOWN   = 4,   // 按下瞬间
    RELEASE      = 5    // 释放
};

/*============================================================================
 * 事件标志位 (event flags)
 *
 * 每个事件对应一个标志位。置位后一直保持，直到 UI 主动读取并清除
 * (peekFlags 非破坏读取 / getAndClearFlags 读取并清除 / clearFlags 直接清除)。
 * 标志位寄存器为 volatile，可在中断上下文 (onExti) 中置位；
 * 主循环 / UI 负责读取与清除。UI 可基于标志位做即时动作，不必依赖事件计数。
 *============================================================================*/

enum class BUTTON_Flag : uint8_t {
    PRESS_DOWN    = 0x01,   // 按下瞬间
    CLICK         = 0x02,   // 单击
    DOUBLE_CLICK  = 0x04,   // 双击
    LONG_PRESS    = 0x08,   // 长按
    RELEASE       = 0x10    // 释放
};

/**
 * @brief 将按键事件映射为对应的标志位
 * @param e 事件
 * @return 对应标志位掩码 (无事件返回 0)
 */
inline constexpr uint8_t flagOf(e_BUTTON_Event e) noexcept
{
    switch (e)
    {
        case e_BUTTON_Event::PRESS_DOWN:    return static_cast<uint8_t>(BUTTON_Flag::PRESS_DOWN);
        case e_BUTTON_Event::CLICK:         return static_cast<uint8_t>(BUTTON_Flag::CLICK);
        case e_BUTTON_Event::DOUBLE_CLICK:  return static_cast<uint8_t>(BUTTON_Flag::DOUBLE_CLICK);
        case e_BUTTON_Event::LONG_PRESS:    return static_cast<uint8_t>(BUTTON_Flag::LONG_PRESS);
        case e_BUTTON_Event::RELEASE:       return static_cast<uint8_t>(BUTTON_Flag::RELEASE);
        default:                            return 0;
    }
}

/*============================================================================
 * 按键状态枚举 (enum class)
 *============================================================================*/

enum class e_BUTTON_State : uint8_t {
    IDLE         = 0,   // 空闲状态
    DEBOUNCE     = 1,   // 消抖状态
    PRESSED      = 2,   // 已按下
    WAIT_RELEASE = 3,   // 等待释放 (保留，兼容)
    WAIT_CLICK   = 4    // 等待第二次点击
};

/*============================================================================
 * 按键配置结构体
 *============================================================================*/

struct BUTTON_Config {
    uint16_t debounceMs;      // 消抖时间 (ms)
    uint16_t longPressMs;     // 长按判定阈值 (ms)
    uint16_t doubleClickMs;   // 双击间隔时间 (ms)
    uint16_t chargeFullMs;    // 长按充能填满所需时间 (ms)
    uint16_t decayMs;         // 未填满松开后消退所需时间 (ms)
    bool activeLow;           // true: 低电平按下, false: 高电平按下

    // 默认配置工厂函数 (使用头文件顶部的宏默认值)
    static BUTTON_Config getDefault() {
        return {
            BUTTON_DEBOUNCE_MS_DEFAULT,
            BUTTON_LONG_PRESS_MS_DEFAULT,
            BUTTON_DOUBLE_CLICK_MS_DEFAULT,
            BUTTON_CHARGE_FULL_MS_DEFAULT,
            BUTTON_DECAY_MS_DEFAULT,
            (BUTTON_ACTIVE_LOW_DEFAULT != 0)
        };
    }
};

/*============================================================================
 * 按键计数器结构体
 *============================================================================*/

struct BUTTON_Counter {
    uint32_t clickCount;       // 单击次数
    uint32_t doubleClickCount; // 双击次数
    uint32_t longPressCount;   // 长按次数 (充能填满次数)
};

/*============================================================================
 * 按键类
 *============================================================================*/

class Button {
public:
    /**
     * @brief 默认构造函数
     */
    Button() = default;

    /**
     * @brief 构造并初始化
     * @param port GPIO端口
     * @param pin GPIO引脚掩码 (LL_GPIO_PIN_x)
     * @param config 配置参数 (可选，使用默认配置)
     */
    Button(GPIO_TypeDef* port, uint32_t pin, const BUTTON_Config& config = BUTTON_Config::getDefault());

    /**
     * @brief 初始化
     * @param port GPIO端口
     * @param pin GPIO引脚掩码 (LL_GPIO_PIN_x)
     * @param config 配置参数
     */
    void init(GPIO_TypeDef* port, uint32_t pin, const BUTTON_Config& config);

    /**
     * @brief 初始化 (使用默认配置)
     * @param port GPIO端口
     * @param pin GPIO引脚掩码 (LL_GPIO_PIN_x)
     */
    void init(GPIO_TypeDef* port, uint32_t pin);

    /**
     * @brief 初始化板级默认按键 (gpio.hpp 的 KEY_INT_Port / KEY_INT_Pin)
     * @param config 配置参数
     * @note 仅当工程 gpio.hpp 定义了 KEY_INT_Port/KEY_INT_Pin 时可用。
     */
    void init(const BUTTON_Config& config);

    /**
     * @brief 初始化板级默认按键并使用默认配置
     */
    void init();

    /**
     * @brief 更新按键状态与充能 (需在主循环中定期调用，建议 1-10ms)
     * @return 当前按键事件
     */
    e_BUTTON_Event update();

    /**
     * @brief 检查是否有待处理事件
     * @return true=有事件, false=无事件
     */
    bool hasEvent() const { return m_hasEvent; }

    /**
     * @brief 获取并清除事件
     * @return 按键事件
     */
    e_BUTTON_Event getEvent();

    /**
     * @brief 获取按键计数器
     * @return 计数器结构体引用
     */
    const BUTTON_Counter& getCounter() const { return m_counter; }

    /**
     * @brief 重置按键计数器
     */
    void resetCounter();

    /*================ 事件计数命名访问器 (便于外部/UI 读取) ================*/

    /**
     * @brief 获取单击次数
     * @return 单击次数
     */
    uint32_t getClickCount() const { return m_counter.clickCount; }

    /**
     * @brief 获取双击次数
     * @return 双击次数
     */
    uint32_t getDoubleClickCount() const { return m_counter.doubleClickCount; }

    /**
     * @brief 获取长按次数 (长按充能填满次数)
     * @return 长按次数
     */
    uint32_t getLongPressCount() const { return m_counter.longPressCount; }

    /*================ 事件标志位 (便于外部/UI 读取) ================*/

    /**
     * @brief 置位一个事件标志位
     * @note 可在中断上下文 (onExti) 中调用；置位后保持，直到被读取/清除。
     * @param e 事件
     */
    void setFlag(e_BUTTON_Event e) { m_flags |= flagOf(e); }

    /**
     * @brief 非破坏性读取全部标志位 (UI 可反复轮询，不会清除)
     * @return 标志位掩码
     */
    uint8_t peekFlags() const { return m_flags; }

    /**
     * @brief 测试某个标志位是否置位 (非破坏性)
     * @param f 标志位
     * @return 是否置位
     */
    bool testFlag(BUTTON_Flag f) const
    {
        return (m_flags & static_cast<uint8_t>(f)) != 0u;
    }

    /**
     * @brief 读取并清除全部标志位 (UI 读取后主动清除)
     * @return 读取到的标志位掩码
     */
    uint8_t getAndClearFlags()
    {
        uint8_t f = m_flags;
        m_flags = 0;
        return f;
    }

    /**
     * @brief 直接清除全部标志位
     */
    void clearFlags() { m_flags = 0; }

    /**
     * @brief 检查当前引脚是否属于此按键
     * @param pin 引脚掩码
     * @return 是否匹配
     */
    bool isPin(uint32_t pin) const { return m_pin == pin; }

    /**
     * @brief 检查某个 EXTI 线是否属于此按键
     * @param extiLine EXTI 线掩码 (LL_EXTI_LINE_x)
     * @return 是否匹配
     */
    bool isExtiLine(uint32_t extiLine) const { return m_extiLine == extiLine; }

    /**
     * @brief 获取当前按键物理状态
     * @return true=按下, false=释放
     */
    bool isPressed() const;

    /**
     * @brief 获取当前状态
     * @return 状态枚举
     */
    e_BUTTON_State getState() const { return m_state; }

    /*================ 运行时阈值修改 (MCU 运行期间可动态调整) ================*/

    /**
     * @brief 设置长按判定阈值 (运行期动态修改)
     * @param ms 长按判定阈值 (毫秒)，0 会被钳到 1
     */
    void setLongPressMs(uint16_t ms) { m_config.longPressMs = (ms == 0u) ? 1u : ms; }

    /**
     * @brief 获取当前长按判定阈值
     * @return 毫秒
     */
    uint16_t getLongPressMs() const { return m_config.longPressMs; }

    /**
     * @brief 设置双击间隔阈值 (运行期动态修改)
     * @param ms 双击间隔时间 (毫秒)，0 会被钳到 1
     */
    void setDoubleClickMs(uint16_t ms) { m_config.doubleClickMs = (ms == 0u) ? 1u : ms; }

    /**
     * @brief 获取当前双击间隔阈值
     * @return 毫秒
     */
    uint16_t getDoubleClickMs() const { return m_config.doubleClickMs; }

    /**
     * @brief 获取当前配置
     * @return 配置结构体引用
     */
    const BUTTON_Config& getConfig() const { return m_config; }

    /*================ EXTI 相关 ================*/

    /**
     * @brief 配置 EXTI (activeLow 时下降沿) 并启用对应 NVIC 分组中断
     * @note 在 init() 中自动调用。若工程已用 PyStudio 配置过 EXTI，仍会重复配置
     *       为同一边沿，因此结果一致。
     */
    void configureExti();

    /**
     * @brief EXTI 中断回调入口
     * @note 必须在中断上下文调用 (尽量保持短小: 仅记录标志与时间戳)。
     */
    void onExti();

    /**
     * @brief EXTI 中断统一分发入口 —— 在工程的 EXTIx_y_IRQHandler 里调用
     *
     * 会先清掉该 EXTI 线的挂起标志，再把中断分发给所有已注册 (init 过) 且
     * EXTI 线匹配的按键实例。因此工程侧只需一行:
 *
     *     void EXTI0_1_IRQHandler(void)
     *     {
     *       LoveFinderLib::Button::dispatchExti(LL_EXTI_LINE_1);   // PA1 KEY_INT
     *     }
     *
     * @param extiLine EXTI 线掩码 (LL_EXTI_LINE_x)
     * @return 实际被分发的按键实例个数
     */
    static uint32_t dispatchExti(uint32_t extiLine);

    /*================ 长按充能 (charge meter) ================*/

    /**
     * @brief 获取当前充能百分比
     * @return 0..100
     */
    uint8_t getChargePercent() const { return m_chargePercent; }

    /**
     * @brief 该按键对应的 EXTI 线掩码 (init 后有效)
     * @return LL_EXTI_LINE_x
     */
    uint32_t getExtiLine() const { return m_extiLine; }

private:
    GPIO_TypeDef* m_port = nullptr;
    uint32_t m_pin = 0;
    uint32_t m_extiLine = 0;   // 由 m_pin 推导: LL_EXTI_LINE_n == (1UL << n) == LL_GPIO_PIN_n

    BUTTON_Config m_config;
    e_BUTTON_State m_state = e_BUTTON_State::IDLE;

    /* --- 中断侧 (volatile) --- */
    volatile bool     m_pressPending = false;  // ISR 置位，主循环消费
    volatile uint32_t m_pressTick    = 0;      // ISR 记录按下时间戳

    /* --- 主循环侧 --- */
    uint32_t m_debounceTick = 0;
    uint32_t m_pressTime    = 0;               // 确认按下时间
    bool m_waitingSecondClick = false;         // 是否等待第二次点击
    bool m_longPressFired   = false;           // 本次按下是否已触发长按事件
    bool m_longPressActive  = false;           // 是否处于长按充能中

    /* --- 充能 (charge meter) --- */
    uint16_t m_chargePercent = 0;              // 0..100
    uint32_t m_chargeTick    = 0;
    uint32_t m_decayTick     = 0;

    BUTTON_Counter m_counter = {0, 0, 0};

    volatile e_BUTTON_Event m_lastEvent = e_BUTTON_Event::NONE;
    volatile bool m_hasEvent = false;
    volatile uint8_t m_flags = 0;   // 事件标志位 (ISR 可置位，主循环/UI 读取清除)

    void pushEvent(e_BUTTON_Event e);
    void updateCharge(uint32_t now);
    void registerInstance();
};

/*============================================================================
 * 使用说明 (C++17 纯 C++ 接口)
 *
 *   LoveFinderLib::Button btn;
 *   btn.init();                              // 板级默认引脚 KEY_INT(PA1) + 默认配置
 *
 *   // 主循环定期调用 update()，返回当前事件并累积标志位
 *   e_BUTTON_Event evt = btn.update();
 *
 *   // UI 侧: 非破坏读取标志位 / 读取并清除标志位
 *   if (btn.testFlag(BUTTON_Flag::DOUBLE_CLICK)) { ... }
 *   uint8_t flags = btn.getAndClearFlags();
 *
 *   // 计数访问器
 *   uint32_t clicks = btn.getClickCount();
 *
 *   // 运行期改长按阈值
 *   btn.setLongPressMs(1500);
 *
 *   // 工程的 EXTI 中断里分发 (清标志 + 通知按键)
 *   void EXTI0_1_IRQHandler(void)
 *   {
 *     LoveFinderLib::Button::dispatchExti(LL_EXTI_LINE_1);
 *   }
 *============================================================================*/

} // namespace LoveFinderLib

#endif // LOVE_FINDER_LIB_BUTTON_HPP
