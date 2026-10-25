/**
 * ============================================================================
 *  rn8306.h - RN8306(锐能微 RN830x/RN730x V3)三相计量芯片 平台无关驱动
 * ============================================================================
 *  设计要点:
 *    1. OOP 风格: rn8306_t 即对象, 所有 API 第一个参数传对象指针(self)。
 *    2. 依赖注入: 平台相关的 SPI 时序/延时/复位引脚, 全部通过一个注入回调
 *       (rn8306_io_fn_t)交给用户实现, 用户在该回调里 switch-case 分发操作。
 *       驱动内部不含任何平台头文件(仅 C 标准库 stdint/stddef/stdbool)。
 *    3. 无缓冲直通: HSDC 波形数据不经过环形缓冲区。用户把 SPI 从机收到的
 *       字节喂给 rn8306_hsdc_feed(), 驱动做帧定界与帧尾校验后, 立即调用用户
 *       注入的 on_wave 回调把原始数据流上抛 —— 用户收到的是连续数据流。
 *    4. 中断入口: 用户在 IO 中断服务程序里调用 rn8306_int_handler()。
 *
 *  硬件背景(依据《锐能微三相计量芯片用户手册》V1.2、《RN830x_RN730x V3 数据手册》Rev1.5):
 *    - SPI(芯片为从机): SCSN/SCLK/SDI/SDO, 最大 3.5Mbps, 用于寄存器读写。
 *      帧格式: [ADDR][CMD][DATA...][CS], CS = ~(ADDR+CMD+DATA) 单字节和取反。
 *      CMD = {R/W, Bank[2:0], BL[2:0], 0}, R/W: 0=读 1=写(注意与常见器件相反)。
 *      数据在 SCLK 上升沿输出、下降沿采样(MCU 侧 SPI 模式1: CPOL=0,CPHA=1)。
 *    - HSDC(芯片为主机): HSCSN/HSCLK/HSDAO, 波形缓存半满自动推送,
 *      MCU 把硬件 SPI 配成从机只收。每帧 = 半块波形 + 可选帧尾校验
 *      (SUM32: 8bit 累加 32bit 结果不取反; 或 CRC16 poly=0x1021 初值 0)。
 *    - 详细协议见 doc/RN8306_通信协议_SPI_HSDC.md。
 *
 *  注意: 驱动不做任何加锁。若在中断里调用 rn8306_hsdc_feed(), 请保证
 *        on_wave 回调执行足够快, 且不要与主循环同时操作同一对象。
 * ============================================================================
 */
#ifndef RN8306_H
#define RN8306_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 寄存器完整地址(11 位 = Bank<<8 | 偏移)。地址均依据 V3 用户手册核对。 */
/* ------------------------------------------------------------------ */
#define RN8306_REG_WREN             0x0180u  /* 写使能寄存器1, 1B, 命令值见 RN8306_WREN_CMD */
#define RN8306_REG_WREN2            0x017Fu  /* 写使能寄存器2, 4B; bit[15:8]=命令字(WREN2=0xE5/WRDIS2=0xDC),
                                             * bit[7:0]=扩位命令(0xAA/0x55), bit[31:16]保留(手册 3.4.35, p112) */
#define RN8306_REG_WMSW             0x0181u  /* 工作模式切换(命令型), 1B: 0xA2=GOEMM 切 EMM 模式;
                                             * 复位后默认测试模式, 上电/整片软复位后必须写(手册 3.4.36, p114)。
                                             * 注意: 写 0xA2 成功后该寄存器读出为模式标志 2'b01(SYSSR.WMS[1:0]
                                             * 同步反映), 不能按写入值 0xA2 回读校验 */
#define RN8306_WMSW_EMM             0xA2u    /* 切换到 EMM 计量模式 */
#define RN8306_REG_SYSCFG           0x0178u  /* 系统配置寄存器, 4B, 复位值 0x38A00;
                                             * bit[31:24]=SYS_PS 写密码位(0xEA), WO 只写不可回读,
                                             * 故本寄存器不能整体写后回读校验(手册 3.4.41) */
#define RN8306_REG_SOFTRST          0x0182u  /* 软件复位寄存器, 1B(0xFA=整片软复位, 0xE1/0x4C=模块复位) */
#define RN8306_REG_SYSSR            0x018Au  /* 系统状态寄存器, 2B, 只读; REG_WE 位反映写保护态 */
#define RN8306_REG_CHECKSUM2        0x018Bu  /* NVM1/NVM2/系统配置校验和, 2B, 只读 */
#define RN8306_REG_WDATA            0x018Du  /* 最近一次 SPI 写入的数据, 3/4B, 只读(回读校验用) */
#define RN8306_REG_DEVICEID         0x018Fu  /* 器件 ID, 3B, 只读(0x830200/0x830201, 建议上电实测) */
#define RN8306_REG_WSAVECON         0x0163u  /* 采样数据写缓存控制, 1B; bit[3:0]=BUFCFG[3:0](缓存格式),
                                             * bit7=SWAVECFG 仅 DS_CFG=00(同步采样)时有效(手册 3.4.21.1, p89) */
#define RN8306_REG_EMMIE            0x0164u  /* EMM 中断允许, 2B, 写保护 */
#define RN8306_REG_EMMIE2           0x016Bu  /* EMM 中断允许 2, 4B; bit11=HSDC 半块输出中断, bit12=HSDC 错误中断 */
#define RN8306_REG_EMMIF2           0x016Cu  /* EMM 中断标志 2, 4B(含 HSDC_ERR_IF, W1C 写 1 清零) */
#define RN8306_REG_CHECKSUM1        0x016Au  /* EMM 校表/配置寄存器校验和, 3B, 只读 */
#define RN8306_REG_DMA_WAVECON      0x0193u  /* 同步采样通道配置, 4B; bit[1:0]=DS_CFG: 01=固定 8K 采样率(p91-92) */
#define RN8306_REG_IN_WAVEEN        0x0194u  /* IN 通道波形缓存使能, 2B, 复位 0x0000
                                             * (7 通道交叉模式 IN/IA 独立流转; 6 通道模式置 1 时 IN 顶替 IA, p125-126) */
#define RN8306_REG_DMA_WAVECON2     0x01D1u  /* 同步采样通道配置 2, 4B, 复位 0x3F800 */
#define RN8306_REG_DMA_WAVECON3     0x01E5u  /* 波形采样通道配置 3, 4B; bit10=交叉缓存, bit0=启动 */
#define RN8306_REG_HDSC_CTL         0x01C9u  /* HSDC 控制寄存器, 4B, 复位 0x04(见下方 HDSC 位定义) */
#define RN8306_REG_HSDC_CRC         0x01CAu  /* HSDC 最近半块输出校验结果, 4B, 只读 */

#define RN8306_WAVE_BUF_BASE        0x0200u  /* 波形缓存区起始(SPI Burst 读用) */
#define RN8306_WAVE_BUF_END         0x057Fu  /* 波形缓存区结束 */

/* WREN/WREN2 命令字(手册 3.4.35, p112) */
#define RN8306_WREN_CMD             0xE5u    /* WREN : 写使能(解除写保护) */
#define RN8306_WRDIS_CMD            0xDCu    /* WRDIS: 恢复写保护 */
#define RN8306_EXTEN_EN_CMD         0xAAu    /* WREN2 扩位命令: 寄存器扩位使能(0C~12H/37~39H/8DH) */
#define RN8306_EXTEN_DIS_CMD        0x55u    /* WREN2 扩位命令: 恢复默认字长 */

/* DeviceID 期望值(大端 3 字节; RN8302B=0x830200, 实物若返回 0x830201 亦接受) */
#define RN8306_DEVICEID_VAL         0x830200u
#define RN8306_DEVICEID_ALT_VAL     0x830201u

/* HDSC_CTL 位定义(手册 6.7.1) */
#define RN8306_HDSC_EN              (1u << 0)   /* HSDC 接口使能(出错时被芯片自动清零) */
#define RN8306_HDSC_CPOL            (1u << 1)   /* HSCLK 空闲电平: 0=低 1=高 */
#define RN8306_HDSC_CPHA            (1u << 2)   /* 时钟相位, 复位=1 */
#define RN8306_HDSC_CHK_EN          (1u << 3)   /* 帧尾校验使能(仅连续发送模式有效) */
#define RN8306_HDSC_CLKDIV_MSK      (7u << 4)   /* HSCLK 分频: 000=4.096M 001=2.048M 010=1.024M 011=512K */
#define RN8306_HDSC_FWIDTH          (1u << 7)   /* 有延时模式帧长: 0=8bit 1=24bit */
#define RN8306_HDSC_GAP_EN          (1u << 8)   /* 0=无延时连续发送 1=帧间延时 8*T_HSCLK */
#define RN8306_HDSC_CHK_SEL         (1u << 9)   /* 校验方式: 0=8bit累加和(32bit结果,不取反) 1=CRC16 */
#define RN8306_HDSC_BYTE_INV        (1u << 10)  /* 波形按字节反序发送(低字节在前) */
#define RN8306_HDSC_EXT_EN          (1u << 11)  /* 24bit 拓展为 32bit(高 8 位补 0) */
#define RN8306_HDSC_BIT_INV         (1u << 12)  /* 波形按位反序发送 */

/* EMMIE2 位定义(手册 6.5) */
#define RN8306_EMMIE2_HSDC_TWAVE_HIE  (1u << 11) /* HSDC 输出半块缓存中断使能 */
#define RN8306_EMMIE2_HSDC_ERR_IE     (1u << 12) /* HSDC 错误中断使能 */

/* DMA_WAVECON3 位定义(厂商参考代码) */
#define RN8306_WAVECON3_CROSS       (1u << 10)  /* 1=通道交叉缓存模式 */
#define RN8306_WAVECON3_START       (1u << 0)   /* 启动波形缓存 DMA */

/* ------------------------------------------------------------------ */
/* 错误码                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    RN8306_OK           =  0,
    RN8306_ERR_PARAM    = -1,   /* 参数非法 */
    RN8306_ERR_IO       = -2,   /* 底层 IO 回调报错 */
    RN8306_ERR_CS       = -3,   /* SPI 读帧校验和不匹配 */
    RN8306_ERR_DEVICE   = -4,   /* DeviceID 不匹配 */
    RN8306_ERR_STATE    = -5,   /* 驱动状态错误(如 HSDC 未配置) */
    RN8306_ERR_VERIFY   = -6,   /* 关键寄存器写后回读不一致(verify_writes) */
} rn8306_err_t;

/* ------------------------------------------------------------------ */
/* 依赖注入: 低层 IO(唯一一个回调, 用户内部 switch-case 分发)           */
/* ------------------------------------------------------------------ */
typedef enum {
    RN8306_IO_SPI_XFER = 1,  /* 一次片选内的 SPI 传输: msg->tx/msg->rx/msg->len
                              *   - 全双工语义: 同时移出 tx、移入 rx(只写时 rx 可为 NULL,
                              *     只读时 tx 数据段已由驱动填成 0xFF);
                              *   - 拉低/拉高 SCSN(或 HSDC 对应片选)由用户在本操作内完成,
                              *     即一次调用 = 一个完整 CS 帧;
                              *   - 返回 0=成功, 非 0=失败(驱动统一映射为 RN8306_ERR_IO)。 */
    RN8306_IO_DELAY_US = 2,  /* 忙等/定时延时 msg->delay_us 微秒 */
    RN8306_IO_RESET    = 3,  /* 复位引脚 RSTN(低有效): msg->level=1 拉低复位, 0 释放 */
} rn8306_io_op_t;

typedef struct {
    uint8_t *tx;          /* 发送缓冲(可为 NULL, 视操作而定) */
    uint8_t *rx;          /* 接收缓冲(可为 NULL) */
    size_t   len;         /* 本次传输字节数 */
    uint32_t delay_us;    /* RN8306_IO_DELAY_US 用 */
    uint8_t  level;       /* RN8306_IO_RESET 用 */
} rn8306_io_msg_t;

typedef int (*rn8306_io_fn_t)(void *ctx, rn8306_io_op_t op, rn8306_io_msg_t *msg);

/* ------------------------------------------------------------------ */
/* 驱动 -> 用户回调                                                     */
/* ------------------------------------------------------------------ */
typedef struct rn8306 rn8306_t;

typedef enum {
    RN8306_EVT_WAVE_FRAME_OK   = 0,  /* 一帧(半块)波形接收完成且帧尾校验通过 */
    RN8306_EVT_WAVE_FRAME_ERR  = 1,  /* 帧尾校验失败(数据已通过 on_wave 上抛, 请丢弃本帧) */
    RN8306_EVT_WAVE_HALF_READY = 2,  /* INT1: 芯片半块缓存满, HSDC 即刻开始发送,
                                      * 用户此时应武装好 SPI 从机 DMA 准备接收 */
    RN8306_EVT_HSDC_ERROR      = 3,  /* HSDC 错误: 芯片已自动清除 HDSC_CTL.EN,
                                      * 需重新调用 rn8306_hsdc_setup() 恢复 */
} rn8306_evt_t;

/* 波形数据回调: 收到即调, data/len 为本帧内连续的原始字节(不含帧尾校验)。
 * 注意:
 *   1) 回调运行在 rn8306_hsdc_feed() 的调用上下文(可能是中断), 应尽快返回;
 *   2) 数据是流式分块上抛的: 若帧尾校验失败(随后收到 RN8306_EVT_WAVE_FRAME_ERR),
 *      本帧数据可能已经分块交付过 —— 需要整帧原子性的应用必须自行缓冲
 *      frame_payload 字节并在 FRAME_OK 事件后再提交。ctx 即 rn8306_if_t::wave_ctx。 */
typedef void (*rn8306_wave_fn_t)(void *ctx, rn8306_t *self, const uint8_t *data, uint32_t len);

/* 事件回调(可选), ctx 即 rn8306_if_t::evt_ctx。 */
typedef void (*rn8306_evt_fn_t)(void *ctx, rn8306_t *self, rn8306_evt_t evt);

/* ------------------------------------------------------------------ */
/* 配置                                                                */
/* ------------------------------------------------------------------ */
typedef enum {
    RN8306_HSDC_CLK_4M096 = 0,   /* HSCLK = 4.096MHz */
    RN8306_HSDC_CLK_2M048 = 1,
    RN8306_HSDC_CLK_1M024 = 2,
    RN8306_HSDC_CLK_512K  = 3,
} rn8306_hsdc_clk_t;

typedef enum {
    RN8306_CHK_SUM32 = 0,        /* 帧尾 4B: 8bit 累加和, 32bit 结果, 不取反(厂商默认, 已验证) */
    RN8306_CHK_CRC16 = 1,        /* 帧尾 2B: CRC16, 多项式 0x1021, 初值 0(移位方向手册未规定, 首板实测) */
    RN8306_CHK_NONE  = 2,        /* 不带帧尾校验 */
} rn8306_chk_t;

/* 帧尾校验字段的字节序: BYTE_INV=1 时手册未明确校验字段是高字节在前还是低字节在前。
 * AUTO=两种序任一匹配(首次上板用); 用 HSDC_CRC(0x1CA) 寄存器与本地计算结果比对
 * 确定实际字节序后, 建议锁定为 BE 或 LE, 恢复对字节序配置错误的检出能力。 */
typedef enum {
    RN8306_CS_AUTO = 0,          /* 大端/小端任一匹配即通过(默认) */
    RN8306_CS_BE   = 1,          /* 校验字段高字节在前 */
    RN8306_CS_LE   = 2,          /* 校验字段低字节在前 */
} rn8306_cs_order_t;

typedef struct {
    /* ---- HSDC ---- */
    rn8306_hsdc_clk_t hsdc_clk;      /* HSCLK 时钟, 默认 4.096MHz(7 通道 8K 推荐; 2.048MHz 亦满足) */
    rn8306_chk_t      chk;           /* 帧尾校验, 默认 SUM32 */
    uint8_t           wave_channels; /* 波形通道数(3/4/6/7), 默认 6: UA UB UC IA IB IC;
                                     * 7 通道(4V+3I): UA UB UC IA IB IC IN(第 4 路电压接 IN 输入) */
    uint16_t          points_per_half; /* 半块采样点数, 默认 64(整块 128 点),
                                       * 帧长 = points_per_half * wave_channels * (ext32?4:3) */
    uint8_t           bufcfg;        /* BUFCFG[4:0] 波形缓存格式, 0xFF=按通道数自动(推荐):
                                      *   6 通道 -> BUFCFG=00000: 交叉缓存, 序 UA,UB,UC,IA,IB,IC
                                      *   7 通道 -> BUFCFG=10010: 交叉缓存, 序 UA,UB,UC,IA,IB,IC,IN
                                      *             (备选 01011 亦为 7 通道同顺序, 手册 p147)
                                      * 陷阱: DMA_WAVECON3[1]=1 而 WSAVECON[3:0]=0000 -> BUFCFG=10000,
                                      *       不是 7 通道(手册 p147 该行读作 6 通道; 该表存在排版笔误,
                                      *       另一次审读为纯 IN —— 无论哪种都不是 7 通道, 帧定界都会错位) */
    bool              in_wave_en;    /* 写 IN_WaveEN=0x0001。7 通道交叉模式保持 false(复位默认,
                                      * IN/IA 独立流转); 若实测 IN 无数据再置 true 验证(p125-126) */
    rn8306_cs_order_t cs_order;      /* 帧尾校验字段字节序, 默认 AUTO(见上方说明) */
    bool              verify_writes; /* 关键寄存器写后回读比对(官方程序实践), 默认 true */
    bool              ext32;         /* 每采样点 24bit 拓展为 32bit, 默认 false */
    bool              byte_inv;      /* 波形按字节反序(低字节在前), 默认 true(厂商做法) */
    bool              bit_inv;       /* 波形按位反序, 默认 false */
    bool              cpha;          /* HSDC CPHA, 默认 true(与手册时序图 CPOL=0/CPHA=1 一致) */
    bool              cpol;          /* HSDC CPOL, 默认 false */
    /* ---- 采样率 ---- */
    uint32_t          dma_wavecon;   /* DMA_WAVECON(0x193) 原始值, 默认 0x01 = 8K 采样率(厂商注释);
                                      * 其他速率见手册 3.4 节该寄存器说明。
                                      * 注: WAVECNT=0 时该寄存器在缓存过程中会自变(反映实际分频数),
                                      * 驱动对其不做写后回读校验 */
    uint32_t          dma_wavecon2;  /* DMA_WAVECON2(0x1D1) 原始值; 0=不写该寄存器(保持复位 0x3F800,
                                      * 原始波形直通, 默认)。厂商 HSDC 流参考配置为 0x00000700
                                      * (CIC 直流校正+增益自动调整+谐波衰减补偿, 使波形幅值与计量
                                      * 通道对齐) —— 若上板发现波形幅值与计量瞬时值对不上, 改配此值 */
    /* ---- SPI ---- */
    uint8_t           spi_retry;     /* SPI 读帧校验失败重试次数, 默认 2 */
    /* ---- 复位 ---- */
    uint32_t          reset_delay_us;/* RSTN 释放后到访问芯片的延时, 默认 50ms */
} rn8306_config_t;

/* 默认配置: HSCLK 4.096MHz / 8K 采样率 / 6 通道 / 24bit / SUM32 校验 / 字节反序 */
#define RN8306_CONFIG_DEFAULT {                                \
        .hsdc_clk         = RN8306_HSDC_CLK_4M096,             \
        .chk              = RN8306_CHK_SUM32,                  \
        .wave_channels    = 6,                                 \
        .points_per_half  = 64,                                \
        .bufcfg           = 0xFFu,  /* 按通道数自动 */         \
        .in_wave_en       = false,                             \
        .cs_order         = RN8306_CS_AUTO,                    \
        .verify_writes    = true,                              \
        .ext32            = false,                             \
        .byte_inv         = true,                              \
        .bit_inv          = false,                             \
        .cpha             = true,                              \
        .cpol             = false,                             \
        .dma_wavecon      = 0x01u,  /* DS_CFG=01: 固定 8K 采样率 */ \
        .dma_wavecon2     = 0u,     /* 0=不写, 保持复位(原始波形); 0x0700=厂商流配置 */ \
        .spi_retry        = 2,                                 \
        .reset_delay_us   = 50000u,                            \
    }

/* ------------------------------------------------------------------ */
/* 对象定义(用户静态分配, 驱动不做动态内存)                             */
/* ------------------------------------------------------------------ */
struct rn8306 {
    /* --- 注入的依赖(勿直接修改, init 后只读) --- */
    rn8306_io_fn_t   io;
    void            *io_ctx;
    rn8306_wave_fn_t on_wave;     /* 可为 NULL(只做校验不上抛) */
    void            *wave_ctx;
    rn8306_evt_fn_t  on_evt;      /* 可为 NULL */
    void            *evt_ctx;

    /* --- 配置快照 --- */
    rn8306_config_t  cfg;
    uint32_t         frame_payload;  /* HSDC 每帧 payload 字节数(不含帧尾校验) */

    /* --- 内部状态(私有, 勿直接访问) --- */
    struct {
        bool     in_frame;        /* 正在接收一帧 */
        uint8_t  phase;           /* 0=数据段 1=帧尾校验段 */
        uint32_t payload_left;    /* 本帧数据段剩余字节数 */
        uint32_t sum32;           /* 累加和 */
        uint16_t crc;             /* CRC16 */
        uint8_t  cs[4];           /* 帧尾校验字节缓存 */
        uint8_t  cs_got;
        uint32_t frames_ok;
        uint32_t frames_err;
        bool     hsdc_up;         /* HSDC 已配置并启动 */
    } st;
};

/* 注入接口集合(依赖注入的载体) */
typedef struct {
    rn8306_io_fn_t   io;        /* [必填] 低层 IO 回调 */
    void            *io_ctx;    /*        传回给 io 回调的用户指针 */
    rn8306_wave_fn_t on_wave;   /* [建议] 波形数据流回调(可为 NULL) */
    void            *wave_ctx;
    rn8306_evt_fn_t  on_evt;    /* [可选] 事件回调 */
    void            *evt_ctx;
} rn8306_if_t;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* 对象构造: 绑定依赖与配置(此函数不访问芯片)。
 * cfg 传 NULL 等价于 &((rn8306_config_t)RN8306_CONFIG_DEFAULT)。
 * 返回 RN8306_OK 或 RN8306_ERR_PARAM。 */
rn8306_err_t rn8306_init(rn8306_t *self, const rn8306_if_t *itf, const rn8306_config_t *cfg);

/* ---- 寄存器层(芯片为 SPI 从机) ---- */

/* 读寄存器。len 必须等于该寄存器字长(1~4 字节, 见手册寄存器表)。
 * 内部做帧校验, 失败按 cfg->spi_retry 重试。 */
rn8306_err_t rn8306_read_reg(rn8306_t *self, uint16_t addr, uint8_t *out, uint8_t len);

/* 读 4 字节寄存器并按寄存器字节序(默认大端)拼成 32 位值。 */
rn8306_err_t rn8306_read_reg32(rn8306_t *self, uint16_t addr, uint32_t *out);

/* 写寄存器。注意: 可写寄存器默认带写保护, 需先 rn8306_write_protect(self, true)。 */
rn8306_err_t rn8306_write_reg(rn8306_t *self, uint16_t addr, const uint8_t *in, uint8_t len);

/* 读改写 32 位寄存器(自动两级写使能/恢复)。clr/set 为按位掩码。 */
rn8306_err_t rn8306_update_reg32(rn8306_t *self, uint16_t addr,
                                 uint32_t clr_mask, uint32_t set_mask);

/* 读-修改-写其它字长寄存器(自动两级写使能/恢复)。
 * len: 字长 1~4; clr/set: 与读写字节序一致(高字节在前), 为 NULL 表示该方向不变。 */
rn8306_err_t rn8306_update_reg(rn8306_t *self, uint16_t addr, uint8_t len,
                               const uint8_t *clr, const uint8_t *set);

/* SPI Burst 读波形缓存: count 仅允许 1/4/8/16/32/64(BL 编码决定),
 * out 需 count*3 字节, 每个采样点 24bit。addr 应在 RN8306_WAVE_BUF_BASE~END 内。
 * 注: 驱动不置 DMA_WAVECON3.SPI_INV(bit11), SPI 读出的采样点高字节在前,
 *     解析请用 rn8306_sample24(..., byte_inv=false), 与 HSDC 流(byte_inv=true)不同。
 *     另: 手册 5.3 正文与帧格式表对 CMD 中 BL 位序描述不一致(见 doc/OPEN_QUESTIONS.md),
 *     本函数按帧格式表实现, 上板建议先 burst 4 读 0x200 验证一次。 */
rn8306_err_t rn8306_burst_read_wave(rn8306_t *self, uint16_t addr,
                                    uint8_t count, uint8_t *out);

/* 写保护控制: unlock=true 解除(0xE5+WREN2), false 恢复(见 RN8306_WRDIS_CMD 说明)。 */
rn8306_err_t rn8306_write_protect(rn8306_t *self, bool unlock);

/* 整片软复位(SOFTRST=0xFA, 官方程序用法)。
 * 注意: 复位后芯片回到默认测试模式且写保护恢复, 必须重新执行 rn8306_hsdc_setup()
 * (内部会写 WMSW=0xA2 切回 EMM 模式)。 */
rn8306_err_t rn8306_soft_reset(rn8306_t *self);

/* 读取并核对 DeviceID(期望 0x830200), 用于上电自检。 */
rn8306_err_t rn8306_check_device(rn8306_t *self);

/* ---- HSDC(芯片为 SPI 主机, MCU 从机接收) ---- */

/* 按 cfg 配置芯片并准备 HSDC 波形流(默认 8K 采样率):
 * 两级写使能 -> WMSW=0xA2 切 EMM 模式 -> SYSCFG 打开 HSDC -> 模块软复位 ->
 * 波形格式/采样率 -> 清 EMMIF2 残留标志 -> HDSC_CTL 参数 -> EMMIE2 中断 ->
 * HDSC_CTL.EN=1。关键寄存器默认写后回读校验(cfg->verify_writes)。
 * 完成后用 rn8306_hsdc_start() 启动。 */
rn8306_err_t rn8306_hsdc_setup(rn8306_t *self);

/* 启动/停止 HSDC 波形发送(写 DMA_WAVECON3.START 与 HDSC_CTL.EN)。
 * 注: 手册建议 HSDC_BUSY=1 时不要清除 EN(见 doc/OPEN_QUESTIONS.md); 本函数直接清,
 *     若上板发现停止后芯片异常, 先等半块发送完(BUSY=0)再调 stop。 */
rn8306_err_t rn8306_hsdc_start(rn8306_t *self, bool en);

/* 使能/关闭 HSDC 相关中断(EMMIE2[12:11])。 */
rn8306_err_t rn8306_hsdc_irq_enable(rn8306_t *self, bool half_ready, bool error);

/* 运行中修改帧布局(换通道数/点数/位宽后调用), 重置帧定界状态。 */
void rn8306_hsdc_set_layout(rn8306_t *self, uint8_t channels,
                            uint16_t points_per_half, bool ext32);

/* ---- 用户侧入口(中断/DMA 上下文调用) ---- */

/* 用户 IO 中断服务程序中调用(根据实际接线选择中断源):
 *   RN8306_INT_WAVE_HALF: 波形半块中断 -> 上抛 EVT_WAVE_HALF_READY
 *   RN8306_INT_HSDC_ERR : HSDC 错误中断 -> 上抛 EVT_HSDC_ERROR
 *   RN8306_INT_OTHER    : 其他          -> 不做处理(仅回调, 便于扩展) */
typedef enum {
    RN8306_INT_WAVE_HALF = 0,
    RN8306_INT_HSDC_ERR  = 1,
    RN8306_INT_OTHER     = 2,
} rn8306_int_src_t;

void rn8306_int_handler(rn8306_t *self, rn8306_int_src_t src);

/* 连续数据流入口: 用户 SPI 从机(DMA)每收到一批 HSDA0 上的字节就调一次,
 * 驱动在内部做帧定界与帧尾校验, 数据段即刻通过 on_wave 上抛(无内部缓冲)。
 * 约定: 每帧 = frame_payload 字节数据 + 4B(SUM32)/2B(CRC16) 校验;
 *       若芯片帧间有空隙, 请保证只喂入有效帧字节(从机按 CS 帧截取即可)。
 * 返回: RN8306_OK / RN8306_ERR_PARAM / RN8306_ERR_STATE(未 setup) */
rn8306_err_t rn8306_hsdc_feed(rn8306_t *self, const uint8_t *data, uint32_t len);

/* 丢弃半帧状态, 从下一帧重新对齐(校验失败或丢数据后调用)。 */
void rn8306_hsdc_resync(rn8306_t *self);

/* 统计信息 */
typedef struct {
    uint32_t frames_ok;
    uint32_t frames_err;
} rn8306_stats_t;

void rn8306_get_stats(const rn8306_t *self, rn8306_stats_t *out);

/* ---- 工具: 把 24bit 采样(按 cfg->byte_inv 之后的原始 3 字节)转为有符号值 ---- */
/* data[0..2] 为芯片输出顺序( byte_inv=true 时 = {data[7:0],data[15:8],data[23:16]} ),
 * 本函数按字节反序规则自动还原 24bit 有符号数。 */
int32_t rn8306_sample24(const uint8_t *data, bool byte_inv);

#ifdef __cplusplus
}
#endif

#endif /* RN8306_H */
