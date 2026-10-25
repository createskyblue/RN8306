/**
 * ============================================================================
 *  rn8306_example.c - RN8306 驱动使用示例(伪平台, 演示依赖注入与数据流)
 * ============================================================================
 *  本文件演示驱动要求的完整接法:
 *
 *    [MCU SPI 主机]──SCSN/SCLK/SDI/SDO──> RN8306(从机)   寄存器读写
 *    [MCU SPI 从机]<──HSCSN/HSCLK/HSDAO── RN8306(主机)   HSDC 波形流
 *    [MCU 外部中断]<────────INT1──────── RN8306          半块满/错误
 *
 *  数据流(目标是拿到连续波形流):
 *    1. 芯片波形缓存半满 -> INT1 产生下降沿;
 *    2. 用户在 INT1 中断里调用 rn8306_int_handler(&rn, RN8306_INT_WAVE_HALF),
 *       并武装好 SPI 从机 DMA(芯片随即自动开始推送半块数据);
 *    3. SPI 从机 DMA 收满一帧(帧长 = 驱动计算的 payload + 帧尾校验),
 *       用户把收到的字节调 rn8306_hsdc_feed() 喂回驱动;
 *    4. 驱动做帧定界 + 帧尾校验, 数据段立即通过 on_wave 回调上抛
 *       (无内部缓冲), 用户在回调里处理连续数据流。
 *
 *  编译: gcc -std=c99 -Wall -Wextra -I../src -o rn8306_example ../src/rn8306.c rn8306_example.c
 * ============================================================================
 */
#include <stdio.h>
#include <string.h>

#include "rn8306.h"

/* =====================================================================
 * 第 1 步: 实现注入的底层 IO 回调 —— 在这里 switch-case 分发硬件操作
 * ===================================================================== */

/* 以下三个是"你的平台"的硬件函数, 替换成你自己的实现 */
static void board_spi_transfer(uint8_t *tx, uint8_t *rx, size_t len)
{
    /* 示例桩: 真实平台在此调用硬件 SPI 全双工收发。
     * 注意: SCSN 的拉低/拉高必须包在一次传输的前后(一次调用 = 一个 CS 帧)。 */
    (void)tx; (void)rx; (void)len;
}
static void board_delay_us(uint32_t us)   { (void)us; }
static void board_rstn_write(int level)   { (void)level; }   /* 1=高(释放) 0=低(复位) */

/* 驱动注入的 IO 回调: 单回调 + switch-case(题目要求的形式) */
static int board_io(void *ctx, rn8306_io_op_t op, rn8306_io_msg_t *msg)
{
    (void)ctx;   /* 需要时可用 ctx 携带你的设备句柄 */

    switch (op) {
    case RN8306_IO_SPI_XFER:
        board_spi_transfer(msg->tx, msg->rx, msg->len);
        return 0;                       /* 0=成功, 非 0=失败 */

    case RN8306_IO_DELAY_US:
        board_delay_us(msg->delay_us);
        return 0;

    case RN8306_IO_RESET:
        board_rstn_write(msg->level ? 0 : 1);   /* RSTN 低有效, 电平取反 */
        return 0;

    default:
        return -1;
    }
}

/* =====================================================================
 * 第 2 步: 实现驱动回调 —— 波形数据流与事件
 * ===================================================================== */

static void my_wave_cb(void *ctx, rn8306_t *self, const uint8_t *data, uint32_t len)
{
    /* 收到即调: 这里拿到的是芯片推来的连续原始波形流(无内部缓冲)。
     * 默认配置(byte_inv=true, 24bit)下, 每 3 字节为一个采样点, 低字节在前:
     *   int32_t sample = rn8306_sample24(&data[i * 3], true);
     * 交叉缓存模式下每采样点的通道顺序由 BUFCFG 决定:
     *   6 通道(mode0):  UA, UB, UC, IA, IB, IC
     *   7 通道(mode14): UA, UB, UC, IA, IB, IC, IN(第 4 路电压) */
    (void)ctx; (void)self;
    (void)data;   /* 示例中仅打印长度; 实际按字节序处理:
                   *   for (i = 0; i + 3 <= len; i += 3)
                   *       sample[i/3] = rn8306_sample24(&data[i], true);
                   * 交叉缓存模式下采样点顺序: UA,UB,UC,IA,IB,IC,... */
    printf("[wave] %u bytes\n", (unsigned)len);
}

static volatile int g_hsdc_recover_req;  /* 中断置位, 主循环消费(ISR 里不做 SPI 长事务) */

static void my_evt_cb(void *ctx, rn8306_t *self, rn8306_evt_t evt)
{
    (void)ctx;
    switch (evt) {
    case RN8306_EVT_WAVE_HALF_READY:
        /* 半块缓存满: 芯片马上开始推送。真实平台在这里启动 SPI 从机 DMA 接收 */
        printf("[evt ] half-ready, start slave rx dma\n");
        break;
    case RN8306_EVT_WAVE_FRAME_OK:
        break;                          /* 需要整帧原子性的应用在此提交缓存的整帧 */
    case RN8306_EVT_WAVE_FRAME_ERR:
        printf("[evt ] frame cs error! drop this frame, consider rn8306_hsdc_resync()\n");
        break;
    case RN8306_EVT_HSDC_ERROR:
        /* 注意: 本回调可能运行在中断上下文, 只置标志; 恢复(setup+start 是一串
         * SPI 事务+延时)放到主循环做, 避免在 ISR 里阻塞 */
        g_hsdc_recover_req = 1;
        printf("[evt ] hsdc error, recovery queued\n");
        break;
    default:
        break;
    }
    (void)self;
}

/* =====================================================================
 * 第 3 步: 中断与 DMA 完成入口(用户调用驱动的回调)
 * ===================================================================== */

static rn8306_t g_rn8306;                 /* 驱动对象: 用户静态分配 */

/* 平台中断入口(示意), 提前声明以满足 -Wmissing-prototypes */
void board_exti_irq_handler(void);
void board_spi_slave_dma_done(const uint8_t *buf, uint32_t len);
int  rn8306_demo(void);

/* 你平台的 INT1 外部中断服务程序(示意) */
void board_exti_irq_handler(void)
{
    /* 1) 通知驱动(内部会上抛 RN8306_EVT_WAVE_HALF_READY) */
    rn8306_int_handler(&g_rn8306, RN8306_INT_WAVE_HALF);

    /* 2) 启动 SPI 从机 DMA 接收一帧:
     *    帧长 = frame_payload + 帧尾校验(4B)。
     *    frame_payload = 点数/半块 x 通道数 x 3(或 4), 见 rn8306_t::frame_payload */
    /* start_spi_slave_dma(dma_buf, g_rn8306.frame_payload + 4); */

    /* 3) 清中断标志(用户平台代码) */
}

/* 你平台的 SPI 从机 DMA 接收完成回调(示意): 收到即喂驱动 */
void board_spi_slave_dma_done(const uint8_t *buf, uint32_t len)
{
    /* 驱动做帧定界/校验, 数据段立刻经 my_wave_cb 上抛 */
    (void)rn8306_hsdc_feed(&g_rn8306, buf, len);
}

/* =====================================================================
 * 第 4 步: 初始化与启动
 * ===================================================================== */

int rn8306_demo(void)
{
    rn8306_if_t    itf;
    rn8306_stats_t st;

    /* 依赖注入: IO 回调 + 数据/事件回调 + 各自用户指针 */
    itf.io       = board_io;
    itf.io_ctx   = NULL;             /* 可携带你的 SPI 设备句柄 */
    itf.on_wave  = my_wave_cb;
    itf.wave_ctx = NULL;
    itf.on_evt   = my_evt_cb;
    itf.evt_ctx  = NULL;

    /* cfg 传 NULL = 默认配置(6 通道): 8K 采样 / HSCLK 4.096MHz / 24bit / SUM32 校验 / 字节反序
     * 4V+3I 硬件(7 通道, 第 4 路电压接 IN 输入)请改为:
     *   rn8306_config_t cfg = RN8306_CONFIG_DEFAULT;
     *   cfg.wave_channels = 7;    // 驱动自动选 mode14: 每采样点 UA,UB,UC,IA,IB,IC,IN
     *   rn8306_init(&g_rn8306, &itf, &cfg); */
    if (rn8306_init(&g_rn8306, &itf, NULL) != RN8306_OK) {
        return -1;
    }

    /* 上电时序: 释放复位 -> 等 50ms -> 读 DeviceID 自检(0x830200) */
    if (rn8306_check_device(&g_rn8306) != RN8306_OK) {
        printf("rn8306 not found (check wiring/spi/cs)\n");
        return -2;
    }

    /* 配置芯片 HSDC(两级写使能/软复位/波形格式/HDSC_CTL/中断), 默认 8K 速率 */
    if (rn8306_hsdc_setup(&g_rn8306) != RN8306_OK) {
        return -3;
    }

    /* 启动波形缓存 DMA, 缓存半满后 HSDC 自动推送, 之后就是连续数据流 */
    if (rn8306_hsdc_start(&g_rn8306, true) != RN8306_OK) {
        return -4;
    }

    /* --- 主循环里随时可以做寄存器访问(芯片 SPI 从机接口) --- */
    {
        uint8_t syssr[2];
        if (rn8306_read_reg(&g_rn8306, RN8306_REG_SYSSR, syssr, 2u) == RN8306_OK) {
            printf("SYSSR = %02X %02X\n", syssr[0], syssr[1]);
        }
        /* 例: 改 HSDC 时钟为 2.048MHz(7 通道 8K 场景不要降速) */
        /* rn8306_update_reg32(&g_rn8306, RN8306_REG_HDSC_CTL,
         *                    RN8306_HDSC_CLKDIV_MSK,
         *                    ((uint32_t)RN8306_HSDC_CLK_2M048 << 4)); */
    }

    rn8306_get_stats(&g_rn8306, &st);
    printf("frames ok=%u err=%u\n",
           (unsigned)st.frames_ok, (unsigned)st.frames_err);

    /* --- 主循环: 处理 ISR 置起的恢复请求(不在中断里做 SPI 长事务) --- */
    /* while (1) {
     *     if (g_hsdc_recover_req) {
     *         g_hsdc_recover_req = 0;
     *         rn8306_hsdc_setup(&g_rn8306);       // 含 WMSW=0xA2 切回 EMM 模式
     *         rn8306_hsdc_start(&g_rn8306, true);
     *     }
     *     ... 常规业务 ...
     * } */
    return 0;
}

/* 伪平台入口: 仅为了能独立编译运行 */
int main(void)
{
    return (rn8306_demo() == RN8306_OK) ? 0 : 1;
}
