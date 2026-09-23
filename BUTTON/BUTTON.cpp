/**
 * @file BUTTON.cpp
 * @brief EXTI 驱动的稳定按键库实现 (LoveFinderLibForPY32_LL) - C++17
 *
 * 移植自 LoveFinderLibForSTM32_HAL/BUTTON/BUTTON.cpp：
 * 状态机与充能逻辑逐行保持一致，只把平台调用换成 PY32 LL：
 *
 *   HAL_GPIO_Init(port,&gpio) + GPIO_MODE_IT_FALLING  ->  LL_GPIO_Init() + LL_EXTI_Init()
 *   HAL_GPIO_ReadPin(port,pin)                        ->  LL_GPIO_IsInputPinSet(port,pin)
 *   HAL_GetTick()                                     ->  BSP_GetTick()
 *   HAL_NVIC_SetPriority/EnableIRQ                    ->  NVIC_SetPriority/EnableIRQ
 *   HAL_GPIO_EXTI_Callback(pin)                       ->  Button::dispatchExti(extiLine)
 */

#include "BUTTON.hpp"

namespace LoveFinderLib {

/*============================================================================
 * 实例注册表 —— 让工程的 EXTI 中断只需一行 dispatchExti() 就能找到按键对象
 *
 * PY32 的 LL 库没有 HAL 那样的 HAL_GPIO_EXTI_Callback() 统一回调，中断服务函数
 * 必须由工程自己写。为了不让每个工程都去维护"引脚 → 对象"的分发表，本库在
 * init() 时把实例登记到这张表，dispatchExti() 按 EXTI 线查找并分发。
 *============================================================================*/
namespace {

Button* s_instances[BUTTON_MAX_INSTANCES] = { nullptr };

/*--------------------------------------------------------------------------
 * 从 GPIO 引脚掩码计算引脚编号 (0..15)
 *
 * PY32 的 LL_GPIO_PIN_x 与 LL_EXTI_LINE_x 都是 (1 << n) 形式的位掩码，
 * 所以引脚编号既决定 EXTI 线号，也决定 NVIC 分组中断号。
 *------------------------------------------------------------------------*/
inline uint8_t pinNumber(uint32_t pinMask)
{
    uint8_t n = 0;
    uint32_t m = pinMask;
    while (((m & 0x0001UL) == 0UL) && (n < 31u))
    {
        m >>= 1u;
        n++;
    }
    return n;
}

/*--------------------------------------------------------------------------
 * EXTI 源选择参数的编码 (喂给 LL_EXTI_SetEXTISource)
 *
 * LL_EXTI_SetEXTISource() 内部这样解码:
 *      mask = (Line >> 16) & 0xFF;      // 要改的位宽
 *      pos  = (Line >>  8) & 0xFF;      // 在 EXTICR[n] 里的位偏移
 *      n    =  Line        & 0x03;      // 用哪个 EXTICR 寄存器
 * 每个 EXTI 线占 EXTICR 里一个 8 位字段，四条线一个寄存器。
 *
 * 厂商 py32f0xx_ll_exti.h 里的 LL_EXTI_CONFIG_LINEn 宏对 LINE5 之后只给了
 * 1 位掩码（选不了 PORTF），这里统一用 8 位整字段，避免那个不一致。
 *------------------------------------------------------------------------*/
constexpr uint32_t extiConfigLine(uint8_t n) noexcept
{
    return (0xFFUL << 16)
         | (static_cast<uint32_t>((n & 0x03u) * 8u) << 8)
         |  static_cast<uint32_t>(n >> 2);
}

/*--------------------------------------------------------------------------
 * GPIO 端口 -> EXTI 端口选择值 / IOP 时钟使能位
 *------------------------------------------------------------------------*/
uint32_t extiPortConfig(GPIO_TypeDef* port) noexcept
{
    if (port == GPIOA) { return LL_EXTI_CONFIG_PORTA; }
    if (port == GPIOB) { return LL_EXTI_CONFIG_PORTB; }
    if (port == GPIOF) { return LL_EXTI_CONFIG_PORTF; }
    return LL_EXTI_CONFIG_PORTA;
}

uint32_t gpioClockOf(GPIO_TypeDef* port) noexcept
{
    if (port == GPIOA) { return LL_IOP_GRP1_PERIPH_GPIOA; }
    if (port == GPIOB) { return LL_IOP_GRP1_PERIPH_GPIOB; }
    if (port == GPIOF) { return LL_IOP_GRP1_PERIPH_GPIOF; }
    return LL_IOP_GRP1_PERIPH_GPIOA;
}

/*--------------------------------------------------------------------------
 * 引脚编号 -> NVIC 分组中断号
 *
 * PY32F003 只有三个 EXTI 中断向量:
 *      EXTI0_1_IRQn   (线 0-1)
 *      EXTI2_3_IRQn   (线 2-3)
 *      EXTI4_15_IRQn  (线 4-15)
 *------------------------------------------------------------------------*/
IRQn_Type irqOf(uint8_t n) noexcept
{
    if (n <= 1u) { return EXTI0_1_IRQn; }
    if (n <= 3u) { return EXTI2_3_IRQn; }
    return EXTI4_15_IRQn;
}

} // namespace

/*============================================================================
 * Button 类实现
 *============================================================================*/

Button::Button(GPIO_TypeDef* port, uint32_t pin, const BUTTON_Config& config)
{
    init(port, pin, config);
}

void Button::init(GPIO_TypeDef* port, uint32_t pin, const BUTTON_Config& config)
{
    m_port = port;
    m_pin  = pin;
    m_config = config;

    m_state = e_BUTTON_State::IDLE;
    m_pressPending = false;
    m_pressTick = 0;
    m_debounceTick = 0;
    m_pressTime = 0;
    m_waitingSecondClick = false;
    m_longPressFired = false;
    m_longPressActive = false;

    m_chargePercent = 0;
    m_chargeTick = 0;
    m_decayTick = 0;

    m_counter = {0, 0, 0};
    m_lastEvent = e_BUTTON_Event::NONE;
    m_hasEvent = false;
    m_flags = 0;

    /* 硬性要求: KEY 必须启用 EXTI，并配置为按下沿触发 + 启用 NVIC。 */
    configureExti();

    /* 登记到实例表，供 dispatchExti() 在中断里查找 */
    registerInstance();
}

void Button::init(GPIO_TypeDef* port, uint32_t pin)
{
    init(port, pin, BUTTON_Config::getDefault());
}

#if BUTTON_HAS_BOARD_PINS
void Button::init(const BUTTON_Config& config)
{
    init(BUTTON_KEY_Port, BUTTON_KEY_Pin, config);
}

void Button::init()
{
    init(BUTTON_KEY_Port, BUTTON_KEY_Pin, BUTTON_Config::getDefault());
}
#endif /* BUTTON_HAS_BOARD_PINS */

void Button::configureExti()
{
    const uint8_t num = pinNumber(m_pin);

    /* GPIO 时钟兜底：工程 Studio_GPIO_Init() 一般已开，这里重复开是幂等的 */
    LL_IOP_GRP1_EnableClock(gpioClockOf(m_port));

    /* ---- GPIO: 输入 + 内部上/下拉兜底 (外部已有上拉，这里防悬空误触发) ---- */
    LL_GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = m_pin;
    gpio.Mode  = LL_GPIO_MODE_INPUT;
    gpio.Pull  = m_config.activeLow ? LL_GPIO_PULL_UP : LL_GPIO_PULL_DOWN;
    gpio.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    LL_GPIO_Init(m_port, &gpio);

    /* ---- EXTI 线掩码: LL_EXTI_LINE_n == (1 << n) == LL_GPIO_PIN_n ---- */
    m_extiLine = (1UL << num);

    /* ---- EXTI 源: 把该 EXTI 线接到按键所在的 GPIO 端口 ---- */
    LL_EXTI_SetEXTISource(extiPortConfig(m_port), extiConfigLine(num));

    /* ---- EXTI: 中断模式 + activeLow 时下降沿 (按键接 3V3 上拉，按下为低) ---- */
    LL_EXTI_InitTypeDef exti = {0};
    exti.Line        = m_extiLine;
    exti.Mode        = LL_EXTI_MODE_IT;
    exti.Trigger     = m_config.activeLow ? LL_EXTI_TRIGGER_FALLING : LL_EXTI_TRIGGER_RISING;
    exti.LineCommand = ENABLE;
    LL_EXTI_Init(&exti);

    /* 清掉配置过程中可能残留的挂起标志，避免 init 完立刻误进一次中断 */
    LL_EXTI_ClearFlag(m_extiLine);

    /* ---- NVIC: 映射 EXTI 线 -> 分组中断号并启用 ---- */
    const IRQn_Type irq = irqOf(num);
    NVIC_SetPriority(irq, BUTTON_IRQ_PRIORITY);
    NVIC_EnableIRQ(irq);
}

void Button::onExti()
{
    /* 仅在按下沿记录按下。重复/抖动边缘在此被过滤。 */
    if (m_pressPending)
    {
        return;
    }
    m_pressPending = true;
    m_pressTick = BSP_GetTick();
}

uint32_t Button::dispatchExti(uint32_t extiLine)
{
    /* 先清挂起标志：PY32 的 EXTI 挂起位必须手动清，否则中断会反复进入 */
    LL_EXTI_ClearFlag(extiLine);

    uint32_t handled = 0;
    for (uint32_t i = 0; i < BUTTON_MAX_INSTANCES; i++)
    {
        Button* b = s_instances[i];
        if ((b != nullptr) && b->isExtiLine(extiLine))
        {
            b->onExti();
            handled++;
        }
    }
    return handled;
}

void Button::registerInstance()
{
    /* 已在表里 (重复 init) -> 不重复登记 */
    for (uint32_t i = 0; i < BUTTON_MAX_INSTANCES; i++)
    {
        if (s_instances[i] == this)
        {
            return;
        }
    }
    /* 找第一个空位 */
    for (uint32_t i = 0; i < BUTTON_MAX_INSTANCES; i++)
    {
        if (s_instances[i] == nullptr)
        {
            s_instances[i] = this;
            return;
        }
    }
    /* 表满: 该实例收不到 dispatchExti() 分发 —— 需要调大 BUTTON_MAX_INSTANCES */
}

bool Button::isPressed() const
{
    const uint32_t level = LL_GPIO_IsInputPinSet(m_port, m_pin);
    return m_config.activeLow ? (level == 0UL) : (level != 0UL);
}

void Button::pushEvent(e_BUTTON_Event e)
{
    m_lastEvent = e;
    m_hasEvent = true;
    m_flags |= flagOf(e);   // 累积事件标志位 (供 UI 非破坏读取/读取清除)
}

e_BUTTON_Event Button::update()
{
    const uint32_t now = BSP_GetTick();
    e_BUTTON_Event event = e_BUTTON_Event::NONE;

    switch (m_state)
    {
        case e_BUTTON_State::IDLE:
            /* EXTI 按下沿中断已置位按下 */
            if (m_pressPending)
            {
                m_pressPending = false;
                m_state = e_BUTTON_State::DEBOUNCE;
                m_debounceTick = now;
            }
            break;

        case e_BUTTON_State::DEBOUNCE:
            if ((now - m_debounceTick) >= m_config.debounceMs)
            {
                m_pressPending = false;   // 丢弃消抖期间的多余边缘
                if (isPressed())
                {
                    m_state = e_BUTTON_State::PRESSED;
                    m_pressTime = now;
                    m_longPressFired = false;
                    m_longPressActive = false;
                    event = e_BUTTON_Event::PRESS_DOWN;
                    pushEvent(event);
                }
                else
                {
                    // 抖动，未真正按下
                    m_state = e_BUTTON_State::IDLE;
                }
            }
            break;

        case e_BUTTON_State::PRESSED:
            if (!isPressed())
            {
                /* ===== 释放 ===== */
                m_pressPending = false;      // 丢弃释放瞬间可能残留的边缘，避免误判双击
                m_longPressActive = false;   // 停止充能
                m_decayTick = now;           // 未填满则开始消退
                setFlag(e_BUTTON_Event::RELEASE);   // 释放标志位 (不覆盖本次主事件)

                if ((now - m_pressTime) >= m_config.longPressMs)
                {
                    /* 长按释放 (事件已在阈值处触发) */
                    if (!m_longPressFired)
                    {
                        m_longPressFired = true;
                        event = e_BUTTON_Event::LONG_PRESS;
                        pushEvent(event);
                    }
                    /* 长按不是「第一次点击」，必须清掉双击等待标志。
                       否则「先短按一下、再长按一次」之后，下一次普通短按会被
                       误判成双击（STM32 原版遗留的 bug，这里修掉）。 */
                    m_waitingSecondClick = false;
                    m_state = e_BUTTON_State::IDLE;
                }
                else
                {
                    /* 短按释放 */
                    if (m_waitingSecondClick)
                    {
                        /* 第二次点击 → 双击 */
                        m_waitingSecondClick = false;
                        m_counter.doubleClickCount++;
                        event = e_BUTTON_Event::DOUBLE_CLICK;
                        pushEvent(event);
                        m_state = e_BUTTON_State::IDLE;
                    }
                    else
                    {
                        /* 第一次点击，进入等待第二次点击窗口 */
                        m_state = e_BUTTON_State::WAIT_CLICK;
                        m_debounceTick = now;
                    }
                }
            }
            else
            {
                /* ===== 仍按住 ===== */
                if ((now - m_pressTime) >= m_config.longPressMs)
                {
                    if (!m_longPressFired)
                    {
                        m_longPressFired = true;
                        m_longPressActive = true;   // 进入长按充能
                        m_chargeTick = now;         // 从阈值处开始充能
                        event = e_BUTTON_Event::LONG_PRESS;
                        pushEvent(event);
                    }
                }
            }
            break;

        case e_BUTTON_State::WAIT_CLICK:
            if (m_pressPending)
            {
                /* 第二次按下 */
                m_pressPending = false;
                m_waitingSecondClick = true;
                m_state = e_BUTTON_State::DEBOUNCE;
                m_debounceTick = now;
            }
            else if ((now - m_debounceTick) >= m_config.doubleClickMs)
            {
                /* 超时 → 确认单击 */
                m_counter.clickCount++;
                event = e_BUTTON_Event::CLICK;
                pushEvent(event);
                m_state = e_BUTTON_State::IDLE;
            }
            else if (isPressed())
            {
                /* 兜底: 未触发 EXTI 也检测到按下 */
                m_waitingSecondClick = true;
                m_state = e_BUTTON_State::DEBOUNCE;
                m_debounceTick = now;
            }
            break;

        default:
            m_state = e_BUTTON_State::IDLE;
            break;
    }

    /* 充能 / 消退更新 (始终调用) */
    updateCharge(now);

    return event;
}

void Button::updateCharge(uint32_t now)
{
    if (m_longPressActive)
    {
        /* ===== 长按充能 ===== */
        const uint32_t dt = now - m_chargeTick;
        if (dt >= 1u)
        {
            const uint32_t full = (m_config.chargeFullMs == 0u) ? 1u : m_config.chargeFullMs;
            const uint32_t add = static_cast<uint32_t>(static_cast<uint64_t>(dt) * 100u / full);
            if (add >= 1u)
            {
                m_chargeTick = now;
                const uint32_t level = static_cast<uint32_t>(m_chargePercent) + add;
                if (level >= 100u)
                {
                    /* 填满一次 → 长按次数 +1，剩余部分继续充能 (持续充能) */
                    const uint32_t fills = level / 100u;
                    m_counter.longPressCount += fills;
                    m_chargePercent = static_cast<uint16_t>(level % 100u);
                }
                else
                {
                    m_chargePercent = static_cast<uint16_t>(level);
                }
            }
            /* add==0 时不推进 m_chargeTick，下次调用继续累积，保证不丢充能 */
        }
    }
    else if (!isPressed() && (m_chargePercent > 0u))
    {
        /* ===== 未填满松开 → 慢慢消退 ===== */
        const uint32_t dt = now - m_decayTick;
        if (dt >= 1u)
        {
            const uint32_t decay = (m_config.decayMs == 0u) ? 1u : m_config.decayMs;
            const uint32_t sub = static_cast<uint32_t>(static_cast<uint64_t>(dt) * 100u / decay);
            if (sub >= 1u)
            {
                m_decayTick = now;
                if (sub >= m_chargePercent)
                {
                    m_chargePercent = 0;
                }
                else
                {
                    m_chargePercent -= static_cast<uint16_t>(sub);
                }
            }
        }
    }
}

e_BUTTON_Event Button::getEvent()
{
    const e_BUTTON_Event e = m_lastEvent;
    m_lastEvent = e_BUTTON_Event::NONE;
    m_hasEvent = false;
    return e;
}

void Button::resetCounter()
{
    m_counter = {0, 0, 0};
}

} // namespace LoveFinderLib
