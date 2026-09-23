/**
 * @file st7735.hpp
 * @brief ST7735 LCD Driver - C++17
 */
#ifndef ST7735_HPP
#define ST7735_HPP

#include "main.hpp"    /* PY32F003 LL 驱动头 */
#include "gpio.hpp"    /* 板级引脚: LCD_CS/DCD/RESET/EN */
#include "font.h"      /* 库的字体名册 + 查表 API (内部已 include 工程清单 font_config.hpp) */
#include "icons.h"
#include <cstdint>
#include <cstddef>

/*============================================================================
 * 库内默认字体
 *
 * 本库的字体像素数据全部在 LoveFinderLibForPY32_LL/FontLib/ (唯一副本)，
 * 每个工程用自己 LoveFinderLib/FontLib/font_config.hpp 里的
 *   USE_FONT_7X10      启用该字体
 *   FONT_7X10_CHARS(X) 声明「要编译哪些字」
 * 未声明的字符渲染时自动跳过 (不显示也不占位)。
 *
 * 换默认字体: 在工程清单里启用对应字体, 再用 -D 覆盖本宏, 或直接改这里。
 *============================================================================*/
#ifndef ST7735_DEFAULT_FONT
#define ST7735_DEFAULT_FONT   Font_7x10
#endif

/*============================================================================
 * C++17 常量定义 (constexpr 替代 #define)
 *============================================================================*/

namespace ST7735 {

// Memory Access Control
constexpr uint8_t MADCTL_MY  = 0x80;
constexpr uint8_t MADCTL_MX  = 0x40;
constexpr uint8_t MADCTL_MV  = 0x20;
constexpr uint8_t MADCTL_ML  = 0x10;
constexpr uint8_t MADCTL_RGB = 0x00;
constexpr uint8_t MADCTL_BGR = 0x08;
constexpr uint8_t MADCTL_MH  = 0x04;

/*----------------------------------------------------------------------------
 * 面板几何 / 方向 / 反色 —— 换屏时改这里
 *
 * 当前值来自本开发板（PY32F003F18U6-E + 0.96" 160x80 ST7735）实测可用配置，
 * 与 examples/HAL/LCD_Test 的 st7735_config.h 一致：
 *     偏移 (1, 26)，MADCTL = MX|MV|BGR = 0x68，需要反色 (INVON)
 *
 * 参照 LoveFinder830-PickSoul 的另一批面板参数是：
 *     XSTART=0, YSTART=24, ROTATION = MY|MV|BGR = 0xA8, 不需要反色
 * 换屏后若显示错位/颜色反了，先动这几个值。
 *----------------------------------------------------------------------------*/
constexpr uint8_t  XSTART  = 1;
constexpr uint8_t  YSTART  = 26;
constexpr uint16_t WIDTH   = 160;
constexpr uint16_t HEIGHT  = 80;
constexpr uint8_t  ROTATION = (MADCTL_MX | MADCTL_MV | MADCTL_BGR);   /* 0x68 */
constexpr bool     INVERT   = true;                                   /* 需要 INVON */

// Commands
constexpr uint8_t NOP     = 0x00;
constexpr uint8_t SWRESET = 0x01;
constexpr uint8_t RDDID   = 0x04;
constexpr uint8_t RDDST   = 0x09;
constexpr uint8_t SLPIN   = 0x10;
constexpr uint8_t SLPOUT  = 0x11;
constexpr uint8_t PTLON   = 0x12;
constexpr uint8_t NORON   = 0x13;
constexpr uint8_t INVOFF  = 0x20;
constexpr uint8_t INVON   = 0x21;
constexpr uint8_t GAMSET  = 0x26;
constexpr uint8_t DISPOFF = 0x28;
constexpr uint8_t DISPON  = 0x29;
constexpr uint8_t CASET   = 0x2A;
constexpr uint8_t RASET   = 0x2B;
constexpr uint8_t RAMWR   = 0x2C;
constexpr uint8_t RAMRD   = 0x2E;
constexpr uint8_t PTLAR   = 0x30;
constexpr uint8_t COLMOD  = 0x3A;
constexpr uint8_t MADCTL  = 0x36;
constexpr uint8_t FRMCTR1 = 0xB1;
constexpr uint8_t FRMCTR2 = 0xB2;
constexpr uint8_t FRMCTR3 = 0xB3;
constexpr uint8_t INVCTR  = 0xB4;
constexpr uint8_t DISSET5 = 0xB6;
constexpr uint8_t PWCTR1  = 0xC0;
constexpr uint8_t PWCTR2  = 0xC1;
constexpr uint8_t PWCTR3  = 0xC2;
constexpr uint8_t PWCTR4  = 0xC3;
constexpr uint8_t PWCTR5  = 0xC4;
constexpr uint8_t VMCTR1  = 0xC5;
constexpr uint8_t RDID1   = 0xDA;
constexpr uint8_t RDID2   = 0xDB;
constexpr uint8_t RDID3   = 0xDC;
constexpr uint8_t RDID4   = 0xDD;
constexpr uint8_t PWCTR6  = 0xFC;
constexpr uint8_t GMCTRP1 = 0xE0;
constexpr uint8_t GMCTRN1 = 0xE1;

// Colors (RGB565)
constexpr uint16_t BLACK   = 0x0000;
constexpr uint16_t BLUE    = 0x001F;
constexpr uint16_t RED     = 0xF800;
constexpr uint16_t GREEN   = 0x07E0;
constexpr uint16_t CYAN    = 0x07FF;
constexpr uint16_t MAGENTA = 0xF81F;
constexpr uint16_t YELLOW  = 0xFFE0;
constexpr uint16_t WHITE   = 0xFFFF;

// Color conversion macro
constexpr uint16_t Color565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

// Custom colors
constexpr uint16_t ORANGE  = Color565(255, 165, 0);

} // namespace ST7735

// Backward compatible macros
#define ST7735_MADCTL_MY    ST7735::MADCTL_MY
#define ST7735_MADCTL_MX    ST7735::MADCTL_MX
#define ST7735_MADCTL_MV    ST7735::MADCTL_MV
#define ST7735_MADCTL_ML    ST7735::MADCTL_ML
#define ST7735_MADCTL_RGB   ST7735::MADCTL_RGB
#define ST7735_MADCTL_BGR   ST7735::MADCTL_BGR
#define ST7735_MADCTL_MH    ST7735::MADCTL_MH
#define ST7735_XSTART       ST7735::XSTART
#define ST7735_YSTART       ST7735::YSTART
#define ST7735_WIDTH        ST7735::WIDTH
#define ST7735_HEIGHT       ST7735::HEIGHT
#define ST7735_ROTATION     ST7735::ROTATION
#define ST7735_BLACK        ST7735::BLACK
#define ST7735_BLUE         ST7735::BLUE
#define ST7735_RED          ST7735::RED
#define ST7735_GREEN        ST7735::GREEN
#define ST7735_CYAN         ST7735::CYAN
#define ST7735_MAGENTA      ST7735::MAGENTA
#define ST7735_YELLOW       ST7735::YELLOW
#define ST7735_WHITE        ST7735::WHITE
#define ST7735_ORANGE       ST7735::ORANGE
#define ST7735_COLOR565(r,g,b) ST7735::Color565(r,g,b)

// Command aliases
#define ST7735_NOP          ST7735::NOP
#define ST7735_SWRESET      ST7735::SWRESET
#define ST7735_RDDID        ST7735::RDDID
#define ST7735_RDDST        ST7735::RDDST
#define ST7735_SLPIN        ST7735::SLPIN
#define ST7735_SLPOUT       ST7735::SLPOUT
#define ST7735_PTLON        ST7735::PTLON
#define ST7735_NORON        ST7735::NORON
#define ST7735_CASET        ST7735::CASET
#define ST7735_RASET        ST7735::RASET
#define ST7735_RAMWR        ST7735::RAMWR
#define ST7735_RAMRD        ST7735::RAMRD
#define ST7735_PTLAR        ST7735::PTLAR
#define ST7735_INVOFF       ST7735::INVOFF
#define ST7735_INVON        ST7735::INVON
#define ST7735_DISPON       ST7735::DISPON
#define ST7735_DISPOFF      ST7735::DISPOFF
#define ST7735_MADCTL       ST7735::MADCTL
#define ST7735_COLMOD       ST7735::COLMOD
#define ST7735_GMCTRP1      ST7735::GMCTRP1
#define ST7735_GMCTRN1      ST7735::GMCTRN1
#define ST7735_GAMSET       ST7735::GAMSET
#define ST7735_FRMCTR1      ST7735::FRMCTR1
#define ST7735_FRMCTR2      ST7735::FRMCTR2
#define ST7735_FRMCTR3      ST7735::FRMCTR3
#define ST7735_INVCTR       ST7735::INVCTR
#define ST7735_DISSET5      ST7735::DISSET5
#define ST7735_PWCTR1       ST7735::PWCTR1
#define ST7735_PWCTR2       ST7735::PWCTR2
#define ST7735_PWCTR3       ST7735::PWCTR3
#define ST7735_PWCTR4       ST7735::PWCTR4
#define ST7735_PWCTR5       ST7735::PWCTR5
#define ST7735_PWCTR6       ST7735::PWCTR6
#define ST7735_VMCTR1       ST7735::VMCTR1
#define ST7735_RDID1        ST7735::RDID1
#define ST7735_RDID2        ST7735::RDID2
#define ST7735_RDID3        ST7735::RDID3
#define ST7735_RDID4        ST7735::RDID4

/*============================================================================
 * 平台移植点 (PORT) —— 唯一需要修改的地方
 *
 * 本库基于 PY32F003 + LL 库（无 HAL），因此绑定的是【外设实例】而不是 HAL 句柄，
 * 引脚来自工程 gpio.hpp 的板级定义。换板子只改这一段。
 *============================================================================*/

/* SPI 外设实例（LL 直接用寄存器实例指针，不需要 HAL_HandleTypeDef） */
#define ST7735_SPI_INSTANCE   SPI1

/* 控制引脚：LCD_*_Port / LCD_*_Pin 由工程 Core/INC/gpio.hpp 提供 */
#define ST7735_CS_Port        LCD_CS_Port
#define ST7735_CS_Pin         LCD_CS_Pin

#define ST7735_DC_Port        LCD_DC_Port
#define ST7735_DC_Pin         LCD_DC_Pin

#define ST7735_RES_Port       LCD_RESET_Port
#define ST7735_RES_Pin        LCD_RESET_Pin

/* 背光/电源使能脚（可选）。若工程没有该引脚，把这两行注释掉，
   并在 ST7735_Init() 之前自行拉高背光。 */
#define ST7735_EN_Port        LCD_EN_Port
#define ST7735_EN_Pin         LCD_EN_Pin

/*============================================================================
 * DMA 加速配置 —— SPI1_TX → DMA1_Channel3
 *
 * PY32F003 的 DMA 请求映射【硬件固定、无重映射】（LL 里不存在
 * LL_SYSCFG_DMA_MAP_*，那是 PY32F403/E407 等带 DMA MUX 的型号），
 * SPI1_TX 固定走 DMA1_Channel3。PyStudio 的 DMA 外设里手选 Channel3 即可。
 *
 * ST7735_USE_DMA:
 *   1 = 启用 DMA 加速（工程必须调用 Studio_DMA_Init()，使能 DMA 时钟并配置通道）
 *   0 = 全部退化为阻塞 SPI（工程没有 DMA 时用，可省 Flash）
 *
 * 即使置 1，若启动自检失败（DMA 时钟没开 / 通道配错 / 芯片无此映射），
 * 库会【自动永久退化】为阻塞 SPI —— 功能完全不受影响，只是慢一点。
 *============================================================================*/
#ifndef ST7735_USE_DMA
#define ST7735_USE_DMA        1
#endif

/*----------------------------------------------------------------------------
 * DMA 通道 —— 必须与工程的 PyStudio 配置一致！
 *
 * PY32F003 的 DMA 请求【不是硬件固定的】，而是靠 SYSCFG->CFGR3 的三个
 * 5 位字段把"请求源"路由到通道 1/2/3：
 *
 *      DMA1_MAP (bit 0..4)   -> 通道 1 的请求源
 *      DMA2_MAP (bit 8..12)  -> 通道 2 的请求源
 *      DMA3_MAP (bit 16..20) -> 通道 3 的请求源
 *
 * 三个字段的复位值都是 0 = LL_SYSCFG_DMA_MAP_ADC。
 * 所以**光配置 DMA 通道是不够的**，必须再调用一次重映射：
 *
 *      LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);   // 先开 SYSCFG 时钟
 *      LL_SYSCFG_SetDMARemap_CH1(LL_SYSCFG_DMA_MAP_SPI1_TX);   // 把 SPI1_TX 接到通道 1
 *
 * 这一步由工程在 Studio_SPI1_Init() 里完成（见 Core/SRC/spi.cpp）。
 * 若漏掉，DMA 通道永远收不到请求 —— TC 标志不置位，库会超时并永久退化，
 * 现象就是"CPU 和 DMA 一样快"。
 *
 * ⚠️ 本宏必须与 .pysprj 的 SPI1.DMA.body.SPI1_TX.channel 保持一致，
 *    否则从 PyStudio 重新导出代码后通道会不匹配。
 *--------------------------------------------------------------------------*/
#define ST7735_DMA_INSTANCE   DMA1
#define ST7735_DMA_CHANNEL    LL_DMA_CHANNEL_1
#define ST7735_DMA_CHANNEL_INST  DMA1_Channel1   /* 同一通道的寄存器指针（诊断用） */

/* 通道相关的标志访问（通道改了这里也要跟着改） */
#define ST7735_DMA_FLAG_TC()     LL_DMA_IsActiveFlag_TC1(ST7735_DMA_INSTANCE)
#define ST7735_DMA_CLEAR_TC()    LL_DMA_ClearFlag_TC1(ST7735_DMA_INSTANCE)
#define ST7735_DMA_CLEAR_ALL()   LL_DMA_ClearFlag_GI1(ST7735_DMA_INSTANCE)

/* DMA 单次传输超时 (ms)：超过即判定 DMA 不可用 */
#ifndef ST7735_DMA_TIMEOUT_MS
#define ST7735_DMA_TIMEOUT_MS 50U
#endif

/*============================================================================
 * Gamma enum class
 *============================================================================*/
enum class GammaDef : uint8_t {
    GAMMA_10 = 0x01,
    GAMMA_25 = 0x02,
    GAMMA_22 = 0x04,
    GAMMA_18 = 0x08
};

/*============================================================================
 * C++ API Functions
 *============================================================================*/
extern "C" {

void ST7735_Unselect();
void ST7735_Init();
void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7735_WriteString(uint16_t x, uint16_t y, const char* str, const FontDef& font, uint16_t color, uint16_t bgcolor);
void ST7735_WriteStringUTF8(uint16_t x, uint16_t y, const char* str, const FontDef& font, uint16_t color, uint16_t bgcolor);
void ST7735_FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_FillRectangleFast(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_FillRectangle_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_FillScreen(uint16_t color);
void ST7735_FillScreenFast(uint16_t color);
void ST7735_FillScreen_DMA(uint16_t color);
void ST7735_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data);
void ST7735_DrawImage_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data);
void ST7735_InvertColors(bool invert);
void ST7735_SetGamma(GammaDef gamma);
void ST7735_Print(uint16_t x, uint16_t y, const FontDef& font, uint16_t color, uint16_t bgcolor, const char* format, ...);

// Icon functions
void ST7735_DrawIcon(uint16_t x, uint16_t y, IconIndex icon);

// Graphics primitives
void ST7735_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void ST7735_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void ST7735_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint16_t color);
void ST7735_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t r, uint16_t color);

// Extended graphics primitives — 空心 (Hollow) + 实心 (Filled)
void ST7735_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void ST7735_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void ST7735_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void ST7735_DrawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);
void ST7735_FillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);
void ST7735_DrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void ST7735_FillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void ST7735_DrawPolygon(const int16_t* xs, const int16_t* ys, uint16_t n, uint16_t color);
void ST7735_FillPolygon(const int16_t* xs, const int16_t* ys, uint16_t n, uint16_t color);

// Utility functions
void ST7735_DrawValueWithIcon(uint16_t x, uint16_t y, IconIndex icon,
                              const char* value, const char* unit,
                              uint16_t valueColor, uint16_t unitColor);

/*============================================================================
 * DMA 加速渲染
 *============================================================================*/

/** 把 RGB565 写进 DMA 行缓冲（大端序 —— ST7735 要求高字节在前） */
#define ST7735_PUT565(dst, i, color)                          \
    do {                                                      \
        (dst)[(i) * 2]     = (uint8_t)((color) >> 8);         \
        (dst)[(i) * 2 + 1] = (uint8_t)((color) & 0xFFU);      \
    } while (0)

/**
 * @brief 行渲染回调：把第 y 行渲染进 dst
 * @param y   行号 (0..ST7735_HEIGHT-1)
 * @param dst 目标缓冲，需写入 w 个像素 = 2*w 字节（大端 RGB565）
 * @param w   行像素数（= ST7735_WIDTH）
 */
typedef void (*ST7735_RowRenderFn)(uint16_t y, uint8_t* dst, uint16_t w);

/**
 * @brief 全屏渲染 —— DMA 乒乓双缓冲
 *
 * CPU 渲染第 N+1 行时，DMA 正在把第 N 行推给屏幕，**计算与传输重叠**。
 * 需要 2 行静态缓冲（2 * ST7735_WIDTH * 2 = 640 字节）；
 * 未调用本函数时该缓冲会被链接器 gc-sections 丢弃，不占 RAM。
 *
 * DMA 不可用（或中途超时）时自动退化为阻塞逐行发送，画面照常。
 */
void ST7735_DrawFrame(ST7735_RowRenderFn fn);

/**
 * @brief 指定区域渲染 —— 同样走 DMA 乒乓双缓冲
 *
 * 与 ST7735_DrawFrame 的唯一区别是只更新 (x0,y0,w,h) 这块矩形，
 * 其它区域保持不动。典型用途：
 *   - 顶部留一条状态栏，只让内容区跑动画
 *   - 屏幕左右分区，左侧走 CPU、右侧走 DMA 做对比
 *
 * @param fn 行渲染回调；**y 是绝对行号**（不是相对 y0 的偏移），
 *           这样动画图案不会因为区域起点不同而跳变
 */
void ST7735_DrawFrameRect(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
                          ST7735_RowRenderFn fn);

/**
 * @brief 同上，但由调用方【显式指定】传输方式
 *
 * 这是做 CPU / DMA 公平对比的关键：
 * 两者用**完全相同的渲染回调**，只有传输方式不同。
 *
 * @param useDma false = 阻塞发送（每行「先算完再发」，计算与传输**串行**）
 *               true  = DMA 乒乓（DMA 发第 N 行时 CPU 算第 N+1 行，**重叠**）
 *
 * 注意：useDma=true 但库内 DMA 不可用（自检失败）时会自动退化为阻塞。
 */
void ST7735_DrawFrameRectEx(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
                            ST7735_RowRenderFn fn, bool useDma);

/**
 * @brief 纯阻塞逐行渲染（全程不用 DMA）—— 做「DMA 关」对比时用
 *
 * 与 DrawFrameRectEx(useDma=false) 的区别：后者内部走 st7735_send_data()，
 * 只要库内 DMA 可用（s_useDma==1）就【依然会用 DMA】（逐行串行，无乒乓）。
 * 本函数直接调 st7735_spi_write()，是真正的 CPU 轮询阻塞 SPI。
 */
void ST7735_DrawFrameRectBlocking(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
                                  ST7735_RowRenderFn fn);

/** @brief 只更新 y0..y0+h-1 这些行（全宽），其余同上 */
void ST7735_DrawFrameArea(uint16_t y0, uint16_t h, ST7735_RowRenderFn fn);

/** @return true = 当前使用 DMA 传输；false = DMA 不可用，已退化为阻塞 SPI */
bool ST7735_IsDmaActive();

/*============================================================================
 * DMA 诊断计数器
 *
 * 用途：确认测量期间 DMA 是否真的在工作。
 *   - ST7735_GetDmaTxOk()   一直为 0  -> 实际走的是阻塞退化路径
 *   - ST7735_GetDmaTxFail() 不为 0    -> 发生过超时，DMA 已被永久关闭
 *
 * 典型用法：在一段测量前后各读一次，差值就是这段期间完成的 DMA 传输次数。
 *============================================================================*/
uint32_t ST7735_GetDmaTxOk(void);    /* 成功完成的 DMA 传输次数 */
uint32_t ST7735_GetDmaTxFail(void);  /* 超时失败次数 */

/*----------------------------------------------------------------------------
 * DMA 诊断寄存器直读 —— 排查"DMA 不工作"时用
 *
 * ST7735_GetDmaRemapReg()  读 SYSCFG->CFGR3
 *      bit 0..4   = 通道 1 的请求源；期望值 1 = LL_SYSCFG_DMA_MAP_SPI1_TX
 *      bit 8..12  = 通道 2 的请求源
 *      bit 16..20 = 通道 3 的请求源
 *   读回 0 说明 LL_SYSCFG_SetDMARemap_CHx() 没生效（SYSCFG 时钟没开）。
 *
 * ST7735_GetDmaChannelCcr()  读 DMA 通道的 CCR
 *      bit0 = EN（传输中/已使能）；bit4 = DIR（1=内存->外设）
 *
 * ST7735_GetSpiCr2()  读 SPI->CR2
 *      bit1 = TXDMAEN（TX DMA 请求使能）
 *--------------------------------------------------------------------------*/
uint32_t ST7735_GetDmaRemapReg(void);
uint32_t ST7735_GetDmaChannelCcr(void);
uint32_t ST7735_GetSpiCr2(void);

/** 重新执行 DMA 启动自检（一般由 ST7735_Init() 自动调用，无需手动） */
void ST7735_DmaSelfTest();

} // extern "C"

#endif // ST7735_HPP
