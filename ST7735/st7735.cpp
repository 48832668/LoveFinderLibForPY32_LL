/**
 * @file st7735.cpp
 * @brief ST7735 LCD Driver Implementation - C++17
 */
#include "main.hpp"   /* PY32F003 LL 驱动头 + 板级引脚定义 */
#include "st7735.hpp"
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint8_t DELAY = 0x80;
}

// 前向声明
static void ST7735_SetAddressWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
static void ST7735_WriteData_DMA(uint8_t *buff, size_t buff_size);

/*============================================================================
 * SPI 阻塞写 —— HAL_SPI_Transmit(&hspi1, buf, len, HAL_MAX_DELAY) 的 LL 等价实现
 *
 * 三件事缺一不可（都是 HAL 内部做了、手写 LL 时容易漏的）：
 *   1) 传输前后关/开 SPE —— 复位 SPI 内部 TX/RX FIFO 与 OVR 状态
 *   2) 结束等 BSY 清零   —— 否则紧接着翻 CS 会截断最后一个字节
 *   3) 清 OVR            —— 全双工模式不读 DR，RX FIFO 会满并置 OVR
 *============================================================================*/
static void st7735_spi_write(const uint8_t* buf, size_t len)
{
  volatile uint32_t tmpreg;

  if ((buf == nullptr) || (len == 0U))
  {
    return;
  }

  LL_SPI_Disable(ST7735_SPI_INSTANCE);
  LL_SPI_Enable(ST7735_SPI_INSTANCE);

  while (len-- > 0U)
  {
    while (LL_SPI_IsActiveFlag_TXE(ST7735_SPI_INSTANCE) == 0U)
    {
    }
    LL_SPI_TransmitData8(ST7735_SPI_INSTANCE, *buf++);
  }

  while (LL_SPI_IsActiveFlag_TXE(ST7735_SPI_INSTANCE) == 0U)
  {
  }
  while (LL_SPI_IsActiveFlag_BSY(ST7735_SPI_INSTANCE) != 0U)
  {
  }

  tmpreg = LL_SPI_ReceiveData8(ST7735_SPI_INSTANCE);
  tmpreg = ST7735_SPI_INSTANCE->SR;
  (void)tmpreg;
}

// based on Adafruit ST7735 library for Arduino
static const uint8_t
    init_cmds1[] = {           // Init for 7735R, part 1 (red or green tab)
        15,                    // 15 commands in list:
        ST7735_SWRESET, DELAY, //  1: Software reset, 0 args, w/delay
        150,                   //     150 ms delay
        ST7735_SLPOUT, DELAY,  //  2: Out of sleep mode, 0 args, w/delay
        255,                   //     500 ms delay
        ST7735_FRMCTR1, 3,     //  3: Frame rate ctrl - normal mode, 3 args:
        0x01, 0x2C, 0x2D,      //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
        ST7735_FRMCTR2, 3,     //  4: Frame rate control - idle mode, 3 args:
        0x01, 0x2C, 0x2D,      //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
        ST7735_FRMCTR3, 6,     //  5: Frame rate ctrl - partial mode, 6 args:
        0x01, 0x2C, 0x2D,      //     Dot inversion mode
        0x01, 0x2C, 0x2D,      //     Line inversion mode
        ST7735_INVCTR, 1,      //  6: Display inversion ctrl, 1 arg, no delay:
        0x07,                  //     No inversion
        ST7735_PWCTR1, 3,      //  7: Power control, 3 args, no delay:
        0xA2,
        0x02,             //     -4.6V
        0x84,             //     AUTO mode
        ST7735_PWCTR2, 1, //  8: Power control, 1 arg, no delay:
        0xC5,             //     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD
        ST7735_PWCTR3, 2, //  9: Power control, 2 args, no delay:
        0x0A,             //     Opamp current small
        0x00,             //     Boost frequency
        ST7735_PWCTR4, 2, // 10: Power control, 2 args, no delay:
        0x8A,             //     BCLK/2, Opamp current small & Medium low
        0x2A,
        ST7735_PWCTR5, 2, // 11: Power control, 2 args, no delay:
        0x8A, 0xEE,
        ST7735_VMCTR1, 1, // 12: Power control, 1 arg, no delay:
        0x0E,
        ST7735_INVOFF, 0, // 13: Don't invert display, no args, no delay
        ST7735_MADCTL, 1, // 14: Memory access control (directions), 1 arg:
        ST7735_ROTATION,  //     row addr/col addr, bottom to top refresh
        ST7735_COLMOD, 1, // 15: set color mode, 1 arg, no delay:
        0x05},            //     16-bit color

    init_cmds2[] = {      // Init for 7735S, part 2 (160x80 display)
        3,                //  3 commands in list:
        ST7735_CASET, 4,  //  1: Column addr set, 4 args, no delay:
        0x00, 0x00,       //     XSTART = 0
        0x00, 0x4F,       //     XEND = 79
        ST7735_RASET, 4,  //  2: Row addr set, 4 args, no delay:
        0x00, 0x00,       //     XSTART = 0
        0x00, 0x9F,       //     XEND = 159
        ST7735_INVOFF, 1}, //  3: Invert colors 此处我修改为INVOFF

    init_cmds3[] = {                                                                                                         // Init for 7735R, part 3 (red or green tab)
        4,                                                                                                                   //  4 commands in list:
        ST7735_GMCTRP1, 16,                                                                                                  //  1: Gamma Adjustments (pos. polarity), 16 args, no delay:
        0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10, ST7735_GMCTRN1, 16,  //  2: Gamma Adjustments (neg. polarity), 16 args, no delay:
        0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10, ST7735_NORON, DELAY, //  3: Normal display on, no args, w/delay
        10,                                                                                                                  //     10 ms delay
        ST7735_DISPON, DELAY,                                                                                                //  4: Main screen turn on, no args w/delay
        100};                                                                                                                //     100 ms delay

static void ST7735_Select()
{
  LL_GPIO_ResetOutputPin(ST7735_CS_Port, ST7735_CS_Pin);
}

void ST7735_Unselect()
{
  LL_GPIO_SetOutputPin(ST7735_CS_Port, ST7735_CS_Pin);
}

static void ST7735_Reset()
{
  LL_GPIO_ResetOutputPin(ST7735_RES_Port, ST7735_RES_Pin);
  LL_mDelay(5);
  LL_GPIO_SetOutputPin(ST7735_RES_Port, ST7735_RES_Pin);
}

static void ST7735_WriteCommand(uint8_t cmd)
{
  LL_GPIO_ResetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);
  st7735_spi_write(&cmd, sizeof(cmd));
}

static void ST7735_WriteData(uint8_t *buff, size_t buff_size)
{
  LL_GPIO_SetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);
  st7735_spi_write(buff, buff_size);
}

/*============================================================================
 * 行缓冲 —— 一次 SPI 传输发一整行，而不是每像素发 2 字节
 *
 * 这是整个图形栈性能与体积的关键：
 *   - 逐像素写：每个像素都要进一次 st7735_spi_write()（含 SPE 关/开、
 *     TXE 轮询、BSY 等待），160 像素就是 160 次调用
 *   - 整行写：把一行在 RAM 里填好，一次发 320 字节 —— 调用次数 160:1
 *
 * 所有填充类图元（FillRectangle / FillCircle / FillRoundRect /
 * DrawHLine / DrawVLine / DrawRect）都汇聚到 ST7735_FillRectangle()，
 * 所以只要这一处批量，全栈受益。
 *
 * 代价：320 字节 BSS（160 像素 × 2 字节）。未使用填充类 API 时
 * 会被链接器 gc-sections 丢弃。
 *============================================================================*/
static uint8_t s_lineBuf[ST7735_WIDTH * 2];

/*============================================================================
 * DMA 传输 —— SPI1_TX → DMA1_Channel3
 *
 * PY32F003 的 DMA 请求映射硬件固定（无重映射），SPI1_TX 固定走 DMA1_Channel3。
 * 本实现沿用 HAL 版 LCD_DMA_Test 验证过的稳健设计：
 *   1) 轮询 TC 标志 + 超时，不用中断 —— 不依赖 SRAM 向量表，最稳
 *   2) 启动自检（CS 未选中时发哑字节）探测 DMA 可用性
 *   3) 任何超时都【永久退化】为阻塞 SPI，画面照常，只是慢一点
 *   4) 乒乓双缓冲让 CPU 渲染与 DMA 传输重叠（见 ST7735_DrawFrame）
 *
 * 注意：DMA 路径【不能】走 st7735_spi_write() —— 它每次传输都会关/开 SPE
 * 复位 FIFO，会把正在进行的 DMA 打断。所以这里单独实现。
 *============================================================================*/

/* DMA 可用性：启动自检后确定；任何一次超时即永久置 0 */
static volatile uint8_t s_useDma = 1U;

/* DMA 诊断计数器 —— 供演示 / 调试确认 DMA 是否真的在工作。
 * 如果 ST7735_GetDmaTxOk() 一直是 0，说明走的是阻塞退化路径。 */
static volatile uint32_t s_dmaTxOk   = 0U;   /* 成功完成的 DMA 传输次数 */
static volatile uint32_t s_dmaTxFail = 0U;   /* 超时失败次数 */

uint32_t ST7735_GetDmaTxOk(void)
{
  return s_dmaTxOk;
}

uint32_t ST7735_GetDmaTxFail(void)
{
  return s_dmaTxFail;
}

/*============================================================================
 * DMA 诊断：直接读回重映射寄存器
 *
 * PY32F003 的 DMA 请求靠 SYSCFG->CFGR3 路由，三个通道各占 5 位：
 *      bit 0..4   DMA1_MAP   （通道 1 的请求源）
 *      bit 8..12  DMA2_MAP   （通道 2 的请求源）
 *      bit 16..20 DMA3_MAP   （通道 3 的请求源）
 *
 * 通道 1 期望值 = LL_SYSCFG_DMA_MAP_SPI1_TX = 0x01。
 * 若读回来是 0，说明 LL_SYSCFG_SetDMARemap_CH1() 没生效
 * （最常见原因：SYSCFG 时钟没开，写入被丢弃）。
 *============================================================================*/
uint32_t ST7735_GetDmaRemapReg(void)
{
  return SYSCFG->CFGR3;
}

uint32_t ST7735_GetDmaChannelCcr(void)
{
  return ST7735_DMA_CHANNEL_INST->CCR;
}

uint32_t ST7735_GetSpiCr2(void)
{
  return ST7735_SPI_INSTANCE->CR2;
}

#if ST7735_USE_DMA

/* 启动一次 DMA 传输（不等待）。返回 1=已启动，0=参数无效 */
static uint8_t st7735_dma_start(const uint8_t* buf, uint16_t len)
{
  if ((buf == nullptr) || (len == 0U))
  {
    return 0U;
  }

  /* 清通道 3 全部标志，避免残留 TCIF 造成假完成 */
  ST7735_DMA_CLEAR_ALL();

  LL_DMA_DisableChannel(ST7735_DMA_INSTANCE, ST7735_DMA_CHANNEL);
  LL_DMA_SetMemoryAddress(ST7735_DMA_INSTANCE, ST7735_DMA_CHANNEL, (uint32_t)buf);
  LL_DMA_SetDataLength(ST7735_DMA_INSTANCE, ST7735_DMA_CHANNEL, (uint32_t)len);
  LL_DMA_EnableChannel(ST7735_DMA_INSTANCE, ST7735_DMA_CHANNEL);

  /* SPI 必须已使能，TX DMA 请求才有效 */
  if (LL_SPI_IsEnabled(ST7735_SPI_INSTANCE) == 0U)
  {
    LL_SPI_Enable(ST7735_SPI_INSTANCE);
  }
  LL_SPI_EnableDMAReq_TX(ST7735_SPI_INSTANCE);
  return 1U;
}

/* 等待一次 DMA 传输完成。返回 1=成功，0=超时（调用方须永久退化） */
static uint8_t st7735_dma_wait(void)
{
  uint32_t t0 = BSP_GetTick();

  while (ST7735_DMA_FLAG_TC() == 0U)
  {
    if ((BSP_GetTick() - t0) > ST7735_DMA_TIMEOUT_MS)
    {
      /* 超时：关通道与 TX 请求，返回失败 */
      LL_DMA_DisableChannel(ST7735_DMA_INSTANCE, ST7735_DMA_CHANNEL);
      LL_SPI_DisableDMAReq_TX(ST7735_SPI_INSTANCE);
      ST7735_DMA_CLEAR_ALL();
      s_dmaTxFail++;
      return 0U;
    }
  }

  /* 传输完成：清标志、关通道与请求 */
  ST7735_DMA_CLEAR_TC();
  LL_DMA_DisableChannel(ST7735_DMA_INSTANCE, ST7735_DMA_CHANNEL);
  LL_SPI_DisableDMAReq_TX(ST7735_SPI_INSTANCE);
  s_dmaTxOk++;

  /* TC 置位 != 最后一位已移出：必须等 BSY 清零，
     否则紧接着翻 CS 会把最后一个字节截断 */
  t0 = BSP_GetTick();
  while (LL_SPI_IsActiveFlag_BSY(ST7735_SPI_INSTANCE) != 0U)
  {
    if ((BSP_GetTick() - t0) > ST7735_DMA_TIMEOUT_MS)
    {
      break;
    }
  }
  return 1U;
}

/* 启动自检：在 CS 未选中（面板会忽略数据）时试发 4 个哑字节。
 * TC 按预期置位 -> DMA 可用；超时 -> 永久退化为阻塞 SPI。 */
static void st7735_dma_selftest(void)
{
  static const uint8_t dummy[4] = {0x00U, 0x00U, 0x00U, 0x00U};

  s_useDma = 1U;
  if (st7735_dma_start(dummy, 4U) != 0U)
  {
    if (st7735_dma_wait() == 0U)
    {
      s_useDma = 0U;
    }
  }
  else
  {
    s_useDma = 0U;
  }
}

#else /* !ST7735_USE_DMA —— 工程没有 DMA，全部走阻塞 */

static uint8_t st7735_dma_start(const uint8_t* /*buf*/, uint16_t /*len*/) { return 0U; }
static uint8_t st7735_dma_wait(void) { return 0U; }
static void    st7735_dma_selftest(void) { s_useDma = 0U; }

#endif /* ST7735_USE_DMA */

/* 统一的像素数据出口：优先 DMA，失败则永久退化为阻塞 SPI。 */
static void st7735_send_data(const uint8_t* buf, size_t len)
{
  LL_GPIO_SetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);   /* data mode */

  if ((s_useDma != 0U) && (len > 0U) && (len <= 0xFFFFU))
  {
    if (st7735_dma_start(buf, (uint16_t)len) != 0U)
    {
      if (st7735_dma_wait() != 0U)
      {
        return;
      }
    }
    s_useDma = 0U;   /* 该芯片 DMA 不可用，以后全部走阻塞 */
  }
  st7735_spi_write(buf, len);
}

/* _DMA 后缀 API 的底层实现 */
static void ST7735_WriteData_DMA(uint8_t *buff, size_t buff_size)
{
  st7735_send_data(buff, buff_size);
}

bool ST7735_IsDmaActive()
{
  return (s_useDma != 0U);
}

void ST7735_DmaSelfTest()
{
  st7735_dma_selftest();
}

/*============================================================================
 * 全屏渲染 —— DMA 乒乓双缓冲
 *
 *   fn(0, cur); DMA_Start(cur);              // 发第 0 行
 *   for (y = 1..H-1) {
 *       fn(y, nxt);                          // CPU 渲染第 y 行
 *       DMA_Wait();                          // 同时 DMA 在发第 y-1 行  <- 重叠
 *       swap(cur, nxt); DMA_Start(cur);
 *   }
 *   DMA_Wait();
 *
 * 帧中途超时则永久切阻塞，重设地址窗口后整帧重绘，避免画面撕裂。
 *============================================================================*/
void ST7735_DrawFrameRectEx(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
                            ST7735_RowRenderFn fn, bool useDma)
{
  /* 乒乓行缓冲：2 行 * WIDTH * 2 字节。未调用本函数时被链接器丢弃 */
  static uint8_t s_fbuf[2][ST7735_WIDTH * 2];

  /* useDma 由调用方指定：
   *   false = 阻塞发送（每行「先算完再发」，计算与传输【串行】）
   *   true  = DMA 乒乓（DMA 发第 N 行时 CPU 算第 N+1 行，【重叠】）
   * 两者渲染回调完全相同，只有传输方式不同 —— 这样才是公平对比。 */
  const uint8_t useDmaNow = (useDma && (s_useDma != 0U)) ? 1U : 0U;

  if ((fn == nullptr) || (w == 0U) || (h == 0U))
  {
    return;
  }
  if ((x0 >= ST7735_WIDTH) || (y0 >= ST7735_HEIGHT))
  {
    return;
  }
  if ((static_cast<uint32_t>(x0) + w) > ST7735_WIDTH)
  {
    w = ST7735_WIDTH - x0;
  }
  if ((static_cast<uint32_t>(y0) + h) > ST7735_HEIGHT)
  {
    h = ST7735_HEIGHT - y0;
  }

  const uint16_t unit = (uint16_t)(w * 2U);
  const uint16_t yEnd = (uint16_t)(y0 + h - 1U);
  const uint16_t xEnd = (uint16_t)(x0 + w - 1U);

  ST7735_Select();
  ST7735_SetAddressWindow(static_cast<uint8_t>(x0), static_cast<uint8_t>(y0),
                          static_cast<uint8_t>(xEnd), static_cast<uint8_t>(yEnd));

  if (useDmaNow != 0U)
  {
    uint8_t* cur = s_fbuf[0];
    uint8_t* nxt = s_fbuf[1];
    bool ok = true;

    LL_GPIO_SetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);

    fn(y0, cur, w);
    (void)st7735_dma_start(cur, unit);

    for (uint16_t y = (uint16_t)(y0 + 1U); y <= yEnd; y++)
    {
      fn(y, nxt, w);                            /* CPU 渲染下一行 */
      if (st7735_dma_wait() == 0U)              /* 等上一行发完 */
      {
        ok = false;
        break;
      }
      uint8_t* tmp = cur; cur = nxt; nxt = tmp;
      (void)st7735_dma_start(cur, unit);
    }
    if (ok && (st7735_dma_wait() == 0U))
    {
      ok = false;
    }

    if (!ok)
    {
      /* DMA 中途超时：永久切阻塞，重设窗口后整块重绘 */
      s_useDma = 0U;
      ST7735_SetAddressWindow(static_cast<uint8_t>(x0), static_cast<uint8_t>(y0),
                              static_cast<uint8_t>(xEnd), static_cast<uint8_t>(yEnd));
      for (uint16_t y = y0; y <= yEnd; y++)
      {
        fn(y, s_fbuf[0], w);
        st7735_send_data(s_fbuf[0], unit);
      }
    }
  }
  else
  {
    /* 阻塞 SPI 逐行发送：每行「先渲染完，再整行发出」—— 计算与传输串行 */
    for (uint16_t y = y0; y <= yEnd; y++)
    {
      fn(y, s_fbuf[0], w);
      st7735_send_data(s_fbuf[0], unit);
    }
  }

  ST7735_Unselect();
}

/*============================================================================
 * 真正的【纯阻塞】逐行渲染 —— 全程不用 DMA
 *
 * 与 ST7735_DrawFrameRectEx(useDma=false) 的区别：后者内部走统一的
 * st7735_send_data()，只要 s_useDma==1 就【依然会用 DMA】（只是逐行串行、
 * 没有乒乓重叠）。本函数直接调 st7735_spi_write()，是货真价实的 CPU 轮询
 * 阻塞 SPI —— 做「DMA 关」对比时用它，才能和「DMA 开」形成干净对照。
 *============================================================================*/
void ST7735_DrawFrameRectBlocking(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
                                  ST7735_RowRenderFn fn)
{
  static uint8_t s_fbuf[ST7735_WIDTH * 2];

  if ((fn == nullptr) || (w == 0U) || (h == 0U))
  {
    return;
  }
  if ((x0 >= ST7735_WIDTH) || (y0 >= ST7735_HEIGHT))
  {
    return;
  }
  if ((static_cast<uint32_t>(x0) + w) > ST7735_WIDTH)
  {
    w = ST7735_WIDTH - x0;
  }
  if ((static_cast<uint32_t>(y0) + h) > ST7735_HEIGHT)
  {
    h = ST7735_HEIGHT - y0;
  }

  const uint16_t unit = (uint16_t)(w * 2U);
  const uint16_t yEnd = (uint16_t)(y0 + h - 1U);
  const uint16_t xEnd = (uint16_t)(x0 + w - 1U);

  ST7735_Select();
  ST7735_SetAddressWindow(static_cast<uint8_t>(x0), static_cast<uint8_t>(y0),
                          static_cast<uint8_t>(xEnd), static_cast<uint8_t>(yEnd));

  LL_GPIO_SetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);
  for (uint16_t y = y0; y <= yEnd; y++)
  {
    fn(y, s_fbuf, w);
    st7735_spi_write(s_fbuf, unit);   /* 纯阻塞，不走 DMA */
  }

  ST7735_Unselect();
}

void ST7735_DrawFrameRect(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
                          ST7735_RowRenderFn fn)
{
  ST7735_DrawFrameRectEx(x0, y0, w, h, fn, true);
}

void ST7735_DrawFrameArea(uint16_t y0, uint16_t h, ST7735_RowRenderFn fn)
{
  ST7735_DrawFrameRect(0U, y0, ST7735_WIDTH, h, fn);
}

void ST7735_DrawFrame(ST7735_RowRenderFn fn)
{
  ST7735_DrawFrameRect(0U, 0U, ST7735_WIDTH, ST7735_HEIGHT, fn);
}

static void ST7735_ExecuteCommandList(const uint8_t *addr)
{
  uint8_t numCommands, numArgs;
  uint16_t ms;

  numCommands = *addr++;
  while (numCommands--)
  {
    uint8_t cmd = *addr++;
    ST7735_WriteCommand(cmd);

    numArgs = *addr++;
    // If high bit set, delay follows args
    ms = numArgs & DELAY;
    numArgs &= ~DELAY;
    if (numArgs)
    {
      ST7735_WriteData((uint8_t *)addr, numArgs);
      addr += numArgs;
    }

    if (ms)
    {
      ms = *addr++;
      if (ms == 255)
        ms = 500;
      LL_mDelay(ms);
    }
  }
}

static void ST7735_SetAddressWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
  // column address set
  ST7735_WriteCommand(ST7735_CASET);
  uint8_t data[] = {0x00, static_cast<uint8_t>(x0 + ST7735_XSTART), 0x00, static_cast<uint8_t>(x1 + ST7735_XSTART)};
  ST7735_WriteData(data, sizeof(data));

  // row address set
  ST7735_WriteCommand(ST7735_RASET);
  data[1] = static_cast<uint8_t>(y0 + ST7735_YSTART);
  data[3] = static_cast<uint8_t>(y1 + ST7735_YSTART);
  ST7735_WriteData(data, sizeof(data));

  // write to RAM
  ST7735_WriteCommand(ST7735_RAMWR);
}

void ST7735_Init()
{
  /* 背光/电源使能：本板 LCD_EN 拉高才点亮屏幕 */
  LL_GPIO_SetOutputPin(ST7735_EN_Port, ST7735_EN_Pin);

  ST7735_Select();
  ST7735_Reset();
  ST7735_ExecuteCommandList(init_cmds1);
  ST7735_ExecuteCommandList(init_cmds2);
  ST7735_ExecuteCommandList(init_cmds3);
  ST7735_Unselect();

  /* DMA 启动自检：此时 CS 未选中，面板会忽略这 4 个哑字节。
     TC 按预期置位 -> DMA 可用；超时 -> 库内永久退化为阻塞 SPI。 */
  st7735_dma_selftest();

  /* 面板反色：本板面板需要 INVON（见 st7735.hpp 的 INVERT 常量） */
  ST7735_InvertColors(ST7735::INVERT);
}

/*============================================================================
 * ST7735_DrawPixel —— 单像素
 *
 * ⚠️ 必须 noinline！
 *
 * 函数体只有 128 字节，但图形原语会调用它很多次：
 *   DrawCircle 每轮 8 次、DrawRoundRect 每轮 8 次、DrawLine 每步 1 次……
 * 编译器（尤其开 LTO 时）会把这份 128 字节**内联到每个调用点**，
 * 于是 DrawCircle 变成 1028 字节、DrawRoundRect 变成 1324 字节。
 * 链接器日志里能直接看到：
 *     Removing st7735.o(.text.ST7735_DrawPixel), (128 bytes).
 * —— 本体被删掉，只剩一堆重复副本。
 *
 * 加 noinline 后只保留一份本体（128 字节），所有调用点走 BL，
 * DrawCircle / DrawRoundRect 各能瘦约 900 字节。
 *============================================================================*/
#if defined(__GNUC__) || defined(__clang__)
#define ST7735_NOINLINE  __attribute__((noinline))
#else
#define ST7735_NOINLINE
#endif

ST7735_NOINLINE
void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT))
    return;

  ST7735_Select();

  ST7735_SetAddressWindow(x, y, x, y);
  uint8_t data[] = {static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color & 0xFF)};
  ST7735_WriteData(data, sizeof(data));

  ST7735_Unselect();
}

/*============================================================================
 * 字符渲染 — 双模式字形解析
 *
 * 模式 A (传统): font.data != nullptr -> 整套字库, 固定索引 (ch-32)*height
 * 模式 B (字符级编译): font.data == nullptr -> font_get_glyph 查表,
 *                      未编译的字符返回 nullptr, 自动跳过(保持其他字符不受影响)
 *==========================================================================*/

static const uint16_t* ST7735_ResolveGlyph(const FontDef& font, uint32_t ch, bool* isWide)
{
  // 中文子集字体: ch 是 Unicode 码点(>=128), 走宽字符通道
  if (ch > 127) {
    *isWide = true;
    return font_get_glyph_unicode(font, static_cast<uint16_t>(ch));
  }

  // ASCII 通道
  *isWide = false;
  if (ch < 32 || ch > 126) return nullptr;

  if (font.data != nullptr) {
    // 模式 A: 整套字库
    return &font.data[(ch - 32) * font.height];
  }
  // 模式 B: 字符级编译
  return font_get_glyph(font, static_cast<uint8_t>(ch));
}

static void ST7735_WriteChar(uint16_t x, uint16_t y, uint32_t ch, const FontDef& font, uint16_t color, uint16_t bgcolor)
{
  bool isWide = false;
  const uint16_t* glyph = ST7735_ResolveGlyph(font, ch, &isWide);
  if (!glyph) return;   // 未编译字符 -> 跳过 (模式 B 的核心行为)

  uint8_t charW = (isWide && font.width == 16) ? 16 : font.width;
  uint32_t i, j;

  ST7735_SetAddressWindow(x, y, x + charW - 1, y + font.height - 1);

  for (i = 0; i < font.height; i++)
  {
    uint16_t b = glyph[i];
    for (j = 0; j < charW; j++)
    {
      uint8_t data[2];
      if ((b << j) & 0x8000)
      {
        data[0] = static_cast<uint8_t>(color >> 8);
        data[1] = static_cast<uint8_t>(color & 0xFF);
      }
      else
      {
        data[0] = static_cast<uint8_t>(bgcolor >> 8);
        data[1] = static_cast<uint8_t>(bgcolor & 0xFF);
      }
      ST7735_WriteData(data, sizeof(data));
    }
  }
}

/*
Simpler (and probably slower) implementation:

static void ST7735_WriteChar(uint16_t x, uint16_t y, char ch, FontDef font, uint16_t color) {
    uint32_t i, b, j;

    for(i = 0; i < font.height; i++) {
        b = font.data[(ch - 32) * font.height + i];
        for(j = 0; j < font.width; j++) {
            if((b << j) & 0x8000)  {
                ST7735_DrawPixel(x + j, y + i, color);
            }
        }
    }
}
*/

/*============================================================================
 * UTF-8 解码 — 支持中文字符串
 *==========================================================================*/
static uint32_t ST7735_UTF8_Decode(const char* str, uint32_t* consumed)
{
  uint8_t c0 = static_cast<uint8_t>(str[0]);
  if (c0 < 0x80) { *consumed = 1; return c0; }                 // 1字节: ASCII
  if ((c0 & 0xE0) == 0xC0) {                                   // 2字节
    *consumed = 2;
    return static_cast<uint32_t>((c0 & 0x1F) << 6) |
           (static_cast<uint8_t>(str[1]) & 0x3F);
  }
  if ((c0 & 0xF0) == 0xE0) {                                   // 3字节: 汉字
    *consumed = 3;
    return static_cast<uint32_t>((c0 & 0x0F) << 12) |
           ((static_cast<uint8_t>(str[1]) & 0x3F) << 6) |
           (static_cast<uint8_t>(str[2]) & 0x3F);
  }
  *consumed = 1;                                               // 无效序列
  return c0;
}

void ST7735_WriteString(uint16_t x, uint16_t y, const char *str, const FontDef& font, uint16_t color, uint16_t bgcolor)
{
  ST7735_Select();

  while (*str)
  {
    uint32_t consumed = 0;
    uint32_t ch = ST7735_UTF8_Decode(str, &consumed);
    bool isWide = (ch > 127);

    uint8_t charW = (isWide && font.width == 16) ? 16 : font.width;

    if (x + charW >= ST7735_WIDTH)
    {
      x = 0;
      y += font.height;
      if (y + font.height >= ST7735_HEIGHT)
      {
        break;
      }

      if (*str == ' ')
      {
        // skip spaces in the beginning of the new line
        str += consumed;
        continue;
      }
    }

    ST7735_WriteChar(x, y, ch, font, color, bgcolor);
    x += charW;
    str += consumed;
  }

  ST7735_Unselect();
}

/*============================================================================
 * UTF-8 显式入口 — 与 ST7735_WriteString 行为一致, 语义更清晰
 *==========================================================================*/
void ST7735_WriteStringUTF8(uint16_t x, uint16_t y, const char *str, const FontDef& font, uint16_t color, uint16_t bgcolor)
{
  ST7735_WriteString(x, y, str, font, color, bgcolor);
}

void ST7735_FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  // clipping
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT) || (w == 0U) || (h == 0U))
    return;
  if ((static_cast<uint32_t>(x) + w) > ST7735_WIDTH)
    w = ST7735_WIDTH - x;
  if ((static_cast<uint32_t>(y) + h) > ST7735_HEIGHT)
    h = ST7735_HEIGHT - y;

  /* 在行缓冲里铺好一整行（大端 RGB565），然后整行一次发出。
     这是全栈的性能关键 —— 见 s_lineBuf 的说明。 */
  const uint8_t hi = static_cast<uint8_t>(color >> 8);
  const uint8_t lo = static_cast<uint8_t>(color & 0xFFU);
  for (uint16_t i = 0U; i < w; i++)
  {
    s_lineBuf[i * 2U]      = hi;
    s_lineBuf[i * 2U + 1U] = lo;
  }

  ST7735_Select();
  ST7735_SetAddressWindow(static_cast<uint8_t>(x), static_cast<uint8_t>(y),
                          static_cast<uint8_t>(x + w - 1U),
                          static_cast<uint8_t>(y + h - 1U));

  LL_GPIO_SetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);
  for (uint16_t r = 0U; r < h; r++)
  {
    st7735_spi_write(s_lineBuf, static_cast<size_t>(w) * 2U);
  }

  ST7735_Unselect();
}

void ST7735_FillRectangleFast(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  // clipping
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT))
    return;
  if ((x + w - 1) >= ST7735_WIDTH)
    w = ST7735_WIDTH - x;
  if ((y + h - 1) >= ST7735_HEIGHT)
    h = ST7735_HEIGHT - y;

  ST7735_Select();
  ST7735_SetAddressWindow(x, y, x + w - 1, y + h - 1);

  // Prepare whole line in a single buffer
  uint8_t pixel[] = {static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color & 0xFF)};
  uint8_t *line = static_cast<uint8_t*>(malloc(w * sizeof(pixel)));
  for (x = 0; x < w; ++x)
    memcpy(line + x * sizeof(pixel), pixel, sizeof(pixel));

  LL_GPIO_SetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);
  for (y = h; y > 0; y--)
    st7735_spi_write(line, w * sizeof(pixel));

  free(line);
  ST7735_Unselect();
}

// DMA版本 - 最高性能
void ST7735_FillRectangle_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  // clipping
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT) || (w == 0U) || (h == 0U))
    return;
  if ((static_cast<uint32_t>(x) + w) > ST7735_WIDTH)
    w = ST7735_WIDTH - x;
  if ((static_cast<uint32_t>(y) + h) > ST7735_HEIGHT)
    h = ST7735_HEIGHT - y;

  /* 在静态行缓冲里铺好一整行 —— 与阻塞版 FillRectangle 完全一致的准备方式。
   *
   * 原实现是每行 malloc(w*2) + 逐像素 memcpy + free()，
   * 堆分配/释放的开销在 80x50 这种小块上非常可观（实测约 20%），
   * 而且反复 malloc/free 会切碎 MicroLIB 的小堆。改用共享静态缓冲后
   * 既没有堆操作，也省掉 memcpy。 */
  const uint8_t hi = static_cast<uint8_t>(color >> 8);
  const uint8_t lo = static_cast<uint8_t>(color & 0xFFU);
  for (uint16_t i = 0U; i < w; i++)
  {
    s_lineBuf[i * 2U]      = hi;
    s_lineBuf[i * 2U + 1U] = lo;
  }

  ST7735_Select();
  ST7735_SetAddressWindow(static_cast<uint8_t>(x), static_cast<uint8_t>(y),
                          static_cast<uint8_t>(x + w - 1U),
                          static_cast<uint8_t>(y + h - 1U));

  LL_GPIO_SetOutputPin(ST7735_DC_Port, ST7735_DC_Pin);

  /* 逐行走 DMA（每行一次传输） */
  const size_t unit = static_cast<size_t>(w) * 2U;
  for (uint16_t r = 0U; r < h; r++)
  {
    st7735_send_data(s_lineBuf, unit);
  }

  ST7735_Unselect();
}

// DMA整屏填充 - 最快速度
void ST7735_FillScreen_DMA(uint16_t color)
{
  ST7735_FillRectangle_DMA(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

// DMA图像绘制 - 高速图像传输
void ST7735_DrawImage_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT))
    return;
  if ((x + w - 1) >= ST7735_WIDTH)
    return;
  if ((y + h - 1) >= ST7735_HEIGHT)
    return;

  ST7735_Select();
  ST7735_SetAddressWindow(x, y, x + w - 1, y + h - 1);
  
  // 使用DMA传输整个图像
  ST7735_WriteData_DMA((uint8_t *)data, sizeof(uint16_t) * w * h);
  
  ST7735_Unselect();
}

void ST7735_FillScreen(uint16_t color)
{
  ST7735_FillRectangle(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void ST7735_FillScreenFast(uint16_t color)
{
  ST7735_FillRectangleFast(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void ST7735_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT))
    return;
  if ((x + w - 1) >= ST7735_WIDTH)
    return;
  if ((y + h - 1) >= ST7735_HEIGHT)
    return;

  ST7735_Select();
  ST7735_SetAddressWindow(x, y, x + w - 1, y + h - 1);
  ST7735_WriteData((uint8_t *)data, sizeof(uint16_t) * w * h);
  ST7735_Unselect();
}

void ST7735_InvertColors(bool invert)
{
  ST7735_Select();
  ST7735_WriteCommand(invert ? ST7735_INVON : ST7735_INVOFF);
  ST7735_Unselect();
}

void ST7735_SetGamma(uint8_t gamma)
{
  ST7735_Select();
  ST7735_WriteCommand(ST7735_GAMSET);
  ST7735_WriteData(&gamma, sizeof(gamma));
  ST7735_Unselect();
}

void ST7735_Print(uint16_t x, uint16_t y, const FontDef& font, uint16_t color, uint16_t bgcolor, const char *format, ...)
{
  char temp[256];
  va_list ap;
  va_start(ap, format);
  vsprintf(temp, format, ap);
  va_end(ap);
  ST7735_WriteString(x, y, temp, font, color, bgcolor);
}

/*============================================================================
 * 图标绘制函数
 *============================================================================*/

void ST7735_DrawIcon(uint16_t x, uint16_t y, IconIndex icon)
{
  if (icon >= ICON_COUNT) return;
  if ((x + ICON_WIDTH > ST7735_WIDTH) || (y + ICON_HEIGHT > ST7735_HEIGHT)) return;
  
  ST7735_DrawImage(x, y, ICON_WIDTH, ICON_HEIGHT, Icons[icon].data);
}

/*============================================================================
 * 辅助图形绘制函数
 *============================================================================*/

void ST7735_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT)) return;
  if (x + w > ST7735_WIDTH) w = ST7735_WIDTH - x;
  
  ST7735_FillRectangle(x, y, w, 1, color);
}

void ST7735_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT)) return;
  if (y + h > ST7735_HEIGHT) h = ST7735_HEIGHT - y;
  
  ST7735_FillRectangle(x, y, 1, h, color);
}

void ST7735_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  ST7735_DrawHLine(x, y, w, color);
  ST7735_DrawHLine(x, y + h - 1, w, color);
  ST7735_DrawVLine(x, y, h, color);
  ST7735_DrawVLine(x + w - 1, y, h, color);
}

void ST7735_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint16_t color)
{
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  
  // Draw straight lines
  ST7735_DrawHLine(x + r, y, w - 2 * r, color);
  ST7735_DrawHLine(x + r, y + h - 1, w - 2 * r, color);
  ST7735_DrawVLine(x, y + r, h - 2 * r, color);
  ST7735_DrawVLine(x + w - 1, y + r, h - 2 * r, color);
  
  // Draw corners — 中点圆算法（整数，无开方、无浮点）
  //
  // 圆心在圆角方块的内角 (x+r, y+r)，半径 r；每轮产生 8 个对称点。
  //
  // 原实现用 `r * 0.7f` 近似，有两个问题：
  //   1) Cortex-M0+ 无 FPU —— 一次 float 乘法会把软浮点运行库拉进固件
  //   2) 画出来是两段直角折线，不是圆弧
  if (r != 0U)
  {
    const int16_t rr = static_cast<int16_t>(r);
    int16_t px = 0;
    int16_t py = rr;
    int16_t d  = 3 - 2 * rr;

    const uint16_t xL = static_cast<uint16_t>(x + r);            /* 左侧圆心 */
    const uint16_t yT = static_cast<uint16_t>(y + r);            /* 上侧圆心 */
    const uint16_t xR = static_cast<uint16_t>(x + w - 1 - r);    /* 右侧圆心 */
    const uint16_t yB = static_cast<uint16_t>(y + h - 1 - r);    /* 下侧圆心 */

    while (px <= py)
    {
      const uint16_t ax = static_cast<uint16_t>(xL - px);
      const uint16_t ay = static_cast<uint16_t>(yT - py);
      const uint16_t bx = static_cast<uint16_t>(xL - py);
      const uint16_t by = static_cast<uint16_t>(yT - px);
      const uint16_t cx = static_cast<uint16_t>(xR + px);
      const uint16_t cy = static_cast<uint16_t>(xR + py);
      const uint16_t ex = static_cast<uint16_t>(yB + py);
      const uint16_t ey = static_cast<uint16_t>(yB + px);

      ST7735_DrawPixel(ax, ay, color);   /* 左上 */
      ST7735_DrawPixel(bx, by, color);
      ST7735_DrawPixel(cx, ay, color);   /* 右上 */
      ST7735_DrawPixel(cy, by, color);
      ST7735_DrawPixel(ax, ey, color);   /* 左下 */
      ST7735_DrawPixel(bx, ex, color);
      ST7735_DrawPixel(cx, ey, color);   /* 右下 */
      ST7735_DrawPixel(cy, ex, color);

      if (d < 0)
      {
        d += 4 * px + 6;
      }
      else
      {
        d += 4 * (px - py) + 10;
        py--;
      }
      px++;
    }
  }
}

void ST7735_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint16_t color)
{
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;

  /* 中间大块 + 左右两条直边 */
  ST7735_FillRectangle(x + r, y, w - 2 * r, h, color);
  if (h > 2U * r)
  {
    ST7735_FillRectangle(x, y + r, r, h - 2 * r, color);
    ST7735_FillRectangle(x + w - r, y + r, r, h - 2 * r, color);
  }

  if (r == 0U)
  {
    return;
  }

  /* 四个圆角 —— 逐行水平填充，而不是逐像素。
   *
   * 圆角区是 r×r 的方块，圆弧圆心在方块的内角 (x+r, y+r)，半径 r。
   * 第 j 行的圆内最左列 i 满足 (i-r)^2 + (j-r)^2 <= r^2；
   * j 递增时 i 单调不增，所以用一个游标往左推即可，**不需要开方**。
   *
   * 原实现是 r×r 嵌套循环 + 每格 4 次 ST7735_DrawPixel()，
   * 每次 DrawPixel 都要重设一次地址窗口 —— 现在是每行 1 次水平填充。 */
  const int16_t rr = static_cast<int16_t>(r);
  int16_t iStart = rr;                       /* 当前行圆内最左列（局部坐标） */
  const int32_t r2 = static_cast<int32_t>(rr) * rr;

  for (int16_t j = 0; j <= rr; j++)
  {
    const int32_t dj = rr - j;
    while (iStart > 0)
    {
      const int32_t di = rr - (iStart - 1);  /* = rr - iStart + 1，恒正 */
      if ((di * di + dj * dj) > r2) break;
      iStart--;
    }

    const uint16_t runLen = static_cast<uint16_t>(rr - iStart + 1);
    const uint16_t rowTop = static_cast<uint16_t>(y + j);
    const uint16_t rowBot = static_cast<uint16_t>(y + h - 1 - j);

    /* 左上 / 右上 */
    ST7735_FillRectangle(static_cast<uint16_t>(x + iStart), rowTop, runLen, 1U, color);
    ST7735_FillRectangle(static_cast<uint16_t>(x + w - 1 - rr + iStart), rowTop, runLen, 1U, color);
    /* 左下 / 右下（j == 0 时与上边重合，跳过省一次传输） */
    if (j != 0)
    {
      ST7735_FillRectangle(static_cast<uint16_t>(x + iStart), rowBot, runLen, 1U, color);
      ST7735_FillRectangle(static_cast<uint16_t>(x + w - 1 - rr + iStart), rowBot, runLen, 1U, color);
    }
  }
}

/*============================================================================
 * 美化显示函数 - 带图标的数值显示
 *============================================================================*/

void ST7735_DrawValueWithIcon(uint16_t x, uint16_t y, IconIndex icon, 
                               const char *value, const char *unit,
                               uint16_t valueColor, uint16_t unitColor)
{
  // Draw icon
  ST7735_DrawIcon(x, y, icon);
  
  // Draw value
  ST7735_WriteString(x + ICON_WIDTH + 2, y + 1, value, ST7735_DEFAULT_FONT, valueColor, ST7735_BLACK);
  
  // Draw unit (smaller, after value)
  uint8_t valueLen = 0;
  while (value[valueLen]) valueLen++;
  ST7735_WriteString(x + ICON_WIDTH + 2 + valueLen * 7, y + 1, unit, ST7735_DEFAULT_FONT, unitColor, ST7735_BLACK);
}

/*============================================================================
 * 扩展图形原语 — 空心 (Hollow) + 实心 (Filled)
 *==========================================================================*/

// 内部: 带裁剪的实心水平线 (坐标可为负, 自动裁剪到屏幕内)
static void ST7735_FillHLineClipped(int16_t x0, int16_t x1, int16_t y, uint16_t color)
{
  if (y < 0 || y >= (int16_t)ST7735_HEIGHT) return;
  if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
  if (x1 < 0 || x0 >= (int16_t)ST7735_WIDTH) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= (int16_t)ST7735_WIDTH) x1 = ST7735_WIDTH - 1;
  ST7735_FillRectangle((uint16_t)x0, (uint16_t)y, (uint16_t)(x1 - x0 + 1), 1, color);
}

// Bresenham 直线 (空心)
void ST7735_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
  int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  int16_t sx = (x0 < x1) ? 1 : -1;
  int16_t sy = (y0 < y1) ? 1 : -1;
  int16_t err = dx - dy;

  for (;;)
  {
    ST7735_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx)  { err += dx; y0 += sy; }
  }
}

// 中点圆算法 (空心)
void ST7735_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
  if (r <= 0) return;
  int16_t x = 0;
  int16_t y = r;
  int16_t d = 3 - 2 * r;

  while (x <= y)
  {
    ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
    ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 + y), color);
    ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 - y), color);
    ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 - y), color);
    ST7735_DrawPixel((uint16_t)(x0 + y), (uint16_t)(y0 + x), color);
    ST7735_DrawPixel((uint16_t)(x0 - y), (uint16_t)(y0 + x), color);
    ST7735_DrawPixel((uint16_t)(x0 + y), (uint16_t)(y0 - x), color);
    ST7735_DrawPixel((uint16_t)(x0 - y), (uint16_t)(y0 - x), color);

    if (d < 0)
      d += 4 * x + 6;
    else
    {
      d += 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

// 实心圆 (水平线填充)
void ST7735_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
  if (r <= 0) return;
  int16_t x = 0;
  int16_t y = r;
  int16_t d = 3 - 2 * r;

  while (x <= y)
  {
    ST7735_FillHLineClipped(x0 - x, x0 + x, y0 + y, color);
    ST7735_FillHLineClipped(x0 - x, x0 + x, y0 - y, color);
    ST7735_FillHLineClipped(x0 - y, x0 + y, y0 + x, color);
    ST7735_FillHLineClipped(x0 - y, x0 + y, y0 - x, color);

    if (d < 0)
      d += 4 * x + 6;
    else
    {
      d += 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

// 中点椭圆算法 (空心)
void ST7735_DrawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color)
{
  if (rx <= 0 || ry <= 0) return;
  int16_t x = 0;
  int16_t y = ry;
  int32_t rx2 = (int32_t)rx * rx;
  int32_t ry2 = (int32_t)ry * ry;
  int32_t twoRx2 = 2 * rx2;
  int32_t twoRy2 = 2 * ry2;
  int32_t px = 0;
  int32_t py = twoRx2 * y;
  int32_t d = ry2 - rx2 * y + (rx2 >> 2);

  // Region 1
  while (px < py)
  {
    ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
    ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 + y), color);
    ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 - y), color);
    ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 - y), color);
    x++;
    px += twoRy2;
    if (d < 0)
      d += ry2 + px;
    else
    {
      y--;
      py -= twoRx2;
      d += ry2 + px - py;
    }
  }

  // Region 2
  d = ry2 * (x + 1) * (x + 1) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
  while (y >= 0)
  {
    ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
    ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 + y), color);
    ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 - y), color);
    ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 - y), color);
    y--;
    py -= twoRx2;
    if (d > 0)
      d += rx2 - py;
    else
    {
      x++;
      px += twoRy2;
      d += rx2 - py + px;
    }
  }
}

// 实心椭圆 (水平线填充)
void ST7735_FillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color)
{
  if (rx <= 0 || ry <= 0) return;
  int16_t x = 0;
  int16_t y = ry;
  int32_t rx2 = (int32_t)rx * rx;
  int32_t ry2 = (int32_t)ry * ry;
  int32_t twoRx2 = 2 * rx2;
  int32_t twoRy2 = 2 * ry2;
  int32_t px = 0;
  int32_t py = twoRx2 * y;
  int32_t d = ry2 - rx2 * y + (rx2 >> 2);

  while (px < py)
  {
    ST7735_FillHLineClipped(x0 - x, x0 + x, y0 + y, color);
    ST7735_FillHLineClipped(x0 - x, x0 + x, y0 - y, color);
    x++;
    px += twoRy2;
    if (d < 0)
      d += ry2 + px;
    else
    {
      y--;
      py -= twoRx2;
      d += ry2 + px - py;
    }
  }

  d = ry2 * (x + 1) * (x + 1) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
  while (y >= 0)
  {
    ST7735_FillHLineClipped(x0 - x, x0 + x, y0 + y, color);
    ST7735_FillHLineClipped(x0 - x, x0 + x, y0 - y, color);
    y--;
    py -= twoRx2;
    if (d > 0)
      d += rx2 - py;
    else
    {
      x++;
      px += twoRy2;
      d += rx2 - py + px;
    }
  }
}

// 三角形 (空心) — 三条边
void ST7735_DrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
{
  ST7735_DrawLine(x0, y0, x1, y1, color);
  ST7735_DrawLine(x1, y1, x2, y2, color);
  ST7735_DrawLine(x2, y2, x0, y0, color);
}

// 三角形 (实心) — 扫描线 (Adafruit GFX 验证算法), 支持任意方向 (含平底/平顶/全水平)
void ST7735_FillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
{
  int16_t a, b, y, last;

  // 按 y 排序顶点: 保证 y0 <= y1 <= y2
  if (y0 > y1) { int16_t t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
  if (y1 > y2) { int16_t t; t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; }
  if (y0 > y1) { int16_t t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }

  // 三个顶点在同一水平线上: 直接画一条水平线
  if (y0 == y2)
  {
    a = b = x0;
    if (x1 < a) a = x1; else if (x1 > b) b = x1;
    if (x2 < a) a = x2; else if (x2 > b) b = x2;
    ST7735_FillHLineClipped(a, b, y0, color);
    return;
  }

  int32_t dx02 = (int32_t)x2 - x0, dy02 = (int32_t)y2 - y0;
  int32_t dx01 = (int32_t)x1 - x0, dy01 = (int32_t)y1 - y0;
  int32_t dx12 = (int32_t)x2 - x1, dy12 = (int32_t)y2 - y1;
  int32_t sa = 0, sb = 0;

  // 上半部分: 扫描线交点位于边 0-1 和 0-2 上
  // 若 y1==y2 (平底), last=y1 包含该行; 否则 last=y1-1 跳过 (避免下部分 /0)
  last = (y1 == y2) ? y1 : (y1 - 1);
  for (y = y0; y <= last; y++)
  {
    a = (int16_t)(x0 + sa / dy01);
    b = (int16_t)(x0 + sb / dy02);
    ST7735_FillHLineClipped(a, b, y, color);
    sa += dx01;
    sb += dx02;
  }

  // 下半部分: 扫描线交点位于边 0-2 和 1-2 上
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for (; y <= y2; y++)
  {
    a = (int16_t)(x1 + sa / dy12);
    b = (int16_t)(x0 + sb / dy02);
    ST7735_FillHLineClipped(a, b, y, color);
    sa += dx12;
    sb += dx02;
  }
}

// 多边形 (空心)
void ST7735_DrawPolygon(const int16_t* xs, const int16_t* ys, uint16_t n, uint16_t color)
{
  if (n < 3) return;
  for (uint16_t i = 0; i < n; i++)
  {
    uint16_t j = (i + 1) % n;
    ST7735_DrawLine(xs[i], ys[i], xs[j], ys[j], color);
  }
}

// 多边形 (实心) — 通用扫描线填充 (奇偶规则)
void ST7735_FillPolygon(const int16_t* xs, const int16_t* ys, uint16_t n, uint16_t color)
{
  if (n < 3) return;

  // 求包围盒
  int16_t minY = ys[0], maxY = ys[0];
  for (uint16_t i = 1; i < n; i++)
  {
    if (ys[i] < minY) minY = ys[i];
    if (ys[i] > maxY) maxY = ys[i];
  }
  if (maxY < 0 || minY >= (int16_t)ST7735_HEIGHT) return;

  // 每行扫描: 计算所有交点, 两两配对填充
  for (int16_t y = minY; y <= maxY; y++)
  {
    int16_t xsect[16];   // 最多 8 个顶点的多边形, 每行最多 8 个交点
    uint16_t cnt = 0;

    for (uint16_t i = 0; i < n && cnt < 16; i++)
    {
      uint16_t j = (i + 1) % n;
      int16_t yi = ys[i], yj = ys[j];
      int16_t xi = xs[i], xj = xs[j];

      // 边与扫描线的交点 (半开区间避免顶点重复计数)
      bool cond1 = (yi <= y) && (yj > y);
      bool cond2 = (yj <= y) && (yi > y);
      if (cond1 || cond2)
      {
        int32_t x = (int32_t)xi + ((int32_t)(y - yi) * (xj - xi)) / (yj - yi);
        xsect[cnt++] = (int16_t)x;
      }
    }

    // 排序交点
    for (uint16_t a = 0; a < cnt; a++)
      for (uint16_t b = a + 1; b < cnt; b++)
        if (xsect[b] < xsect[a])
        {
          int16_t t = xsect[a]; xsect[a] = xsect[b]; xsect[b] = t;
        }

    // 成对填充
    for (uint16_t k = 0; k + 1 < cnt; k += 2)
      ST7735_FillHLineClipped(xsect[k], xsect[k + 1], y, color);
  }
}
