/**
 * ============================================================================
 *  rn8306.c - RN8306 平台无关驱动实现
 * ============================================================================
 *  实现说明:
 *    - 所有芯片访问都经过 rn8306_init() 注入的 io 回调(单回调 switch-case)。
 *    - 寄存器数据在 SPI 上按"先发高字节"(大端)传输(用户手册 5.3: MSB/LSB 字节,
 *      先发高字节, 后发低字节)。若个别平台/芯片批次表现相反,
 *      可在工程里定义 RN8306_REG_LSB_FIRST 切换为低字节在前。
 *    - HSDC 数据流不做内部缓冲: rn8306_hsdc_feed() 逐段转发给 on_wave,
 *      帧尾校验字节由驱动截取并比对, 不上抛。
 * ============================================================================
 */
#include "rn8306.h"

#include <string.h>

/* 寄存器字节序开关: 默认按手册"先发高字节"(大端)。 */
#ifdef RN8306_REG_LSB_FIRST
static void reg_be_pack(uint32_t v, uint8_t *p) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t reg_be_unpack(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
#else
static void reg_be_pack(uint32_t v, uint8_t *p) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static uint32_t reg_be_unpack(const uint8_t *p) { return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | (uint32_t)p[3]; }
#endif

/* ------------------------------------------------------------------ */
/* 内部小工具                                                          */
/* ------------------------------------------------------------------ */

/* CMD 字节 = {R/W, Bank[2:0], BL[1:0], BL[2], 1'b0}, R/W: 0=读 1=写
 * BL 位序依据手册 5.3 正文(p140): BL[1:0] 在 bit[3:2], BL[2] 在 bit[1];
 * 已由厂商官方 fnRN8302_ReadBuf()(Dl645_Fft.c, burst 读 16 地址写 CMD|=0x0C)
 * 证实 —— 手册同节帧格式表(p141)写作 {BANK, BL[2:0], 1'b0} 属排版笔误, 不采用。 */
static uint8_t build_cmd(uint16_t addr, bool wr, uint8_t bl_code)
{
    uint8_t bank = (uint8_t)((addr >> 8) & 0x07u);
    return (uint8_t)((wr ? 0x80u : 0x00u)
                   | (uint8_t)(bank << 4)
                   | (uint8_t)((bl_code & 0x03u) << 2)           /* BL[1:0] -> bit[3:2] */
                   | (uint8_t)(((bl_code >> 2) & 0x01u) << 1));  /* BL[2]   -> bit[1]   */
}

/* SPI 帧校验和: ~(ADDR + CMD + DATA) 单字节和取反 */
static uint8_t frame_cs(uint8_t addr_lo, uint8_t cmd, const uint8_t *data, size_t len)
{
    uint16_t s = (uint16_t)addr_lo + (uint16_t)cmd;
    while (len--) { s += *data++; }
    return (uint8_t)(~(uint8_t)s);
}

static uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t *d, size_t n)
{
    /* CRC16: 多项式 x^16+x^12+x^5+1 (0x1021), 初值 0, 手册 6.1/6.7.2 */
    while (n--) {
        uint8_t b;
        crc ^= (uint16_t)((uint16_t)(*d++) << 8);
        for (b = 0; b < 8u; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* 底层 IO 分发: 透传给用户注入的回调 */
static int io_call(rn8306_t *self, rn8306_io_op_t op, rn8306_io_msg_t *m)
{
    if (self->io == NULL) { return -1; }
    return self->io(self->io_ctx, op, m);   /* 0=成功, 非 0=用户侧错误 */
}

static rn8306_err_t io_spi(rn8306_t *self, uint8_t *tx, uint8_t *rx, size_t len)
{
    rn8306_io_msg_t m;
    m.tx = tx; m.rx = rx; m.len = len;
    m.delay_us = 0; m.level = 0;
    return (io_call(self, RN8306_IO_SPI_XFER, &m) == 0) ? RN8306_OK : RN8306_ERR_IO;
}

static rn8306_err_t io_delay_us(rn8306_t *self, uint32_t us)
{
    rn8306_io_msg_t m;
    memset(&m, 0, sizeof(m));
    m.delay_us = us;
    return (io_call(self, RN8306_IO_DELAY_US, &m) == 0) ? RN8306_OK : RN8306_ERR_IO;
}

static void emit_evt(rn8306_t *self, rn8306_evt_t evt)
{
    if (self->on_evt != NULL) { self->on_evt(self->evt_ctx, self, evt); }
}

/* ------------------------------------------------------------------ */
/* 寄存器层                                                            */
/* ------------------------------------------------------------------ */

rn8306_err_t rn8306_read_reg(rn8306_t *self, uint16_t addr, uint8_t *out, uint8_t len)
{
    uint8_t tx[2 + 4 + 1];       /* ADDR + CMD + len + CS(读时 TX 数据段填 0xFF) */
    uint8_t rx[2 + 4 + 1];
    uint8_t cmd;
    uint8_t attempt, retry;
    size_t  total;

    if (self == NULL || self->io == NULL || out == NULL || len == 0u || len > 4u) {
        return RN8306_ERR_PARAM;
    }
    /* 注: 波形缓存(Bank2~5)请用 rn8306_burst_read_wave(); 寄存器字长以手册为准。 */

    cmd    = build_cmd(addr, false, 0u);   /* BL=000: 单地址读 */
    total  = (size_t)2u + (size_t)len + 1u;
    retry  = self->cfg.spi_retry;

    for (attempt = 0; attempt <= retry; attempt++) {
        size_t i;
        uint16_t sum;

        tx[0] = (uint8_t)(addr & 0xFFu);
        tx[1] = cmd;
        for (i = 2u; i < total; i++) { tx[i] = 0xFFu; }   /* 读阶段给时钟 */

        if (io_spi(self, tx, rx, total) != RN8306_OK) { return RN8306_ERR_IO; }

        /* 校验: ADDR + CMD + DATA 求和取反, 与帧尾字节比较 */
        sum  = (uint16_t)tx[0] + (uint16_t)cmd;
        for (i = 0u; i < (size_t)len; i++) { sum += rx[2u + i]; }

        if ((uint8_t)(~(uint8_t)sum) == rx[2u + (size_t)len]) {
            memcpy(out, &rx[2], (size_t)len);
            return RN8306_OK;
        }
    }
    return RN8306_ERR_CS;
}

rn8306_err_t rn8306_read_reg32(rn8306_t *self, uint16_t addr, uint32_t *out)
{
    uint8_t b[4];
    rn8306_err_t err;

    if (self == NULL || out == NULL) { return RN8306_ERR_PARAM; }
    err = rn8306_read_reg(self, addr, b, 4u);
    if (err != RN8306_OK) { return err; }
    *out = reg_be_unpack(b);
    return RN8306_OK;
}

rn8306_err_t rn8306_write_reg(rn8306_t *self, uint16_t addr, const uint8_t *in, uint8_t len)
{
    uint8_t tx[2 + 4 + 1];
    uint8_t cmd;

    if (self == NULL || self->io == NULL || in == NULL || len == 0u || len > 4u) {
        return RN8306_ERR_PARAM;
    }

    cmd    = build_cmd(addr, true, 0u);    /* BL 无效但必须带上, 值参与校验和 */
    tx[0]  = (uint8_t)(addr & 0xFFu);
    tx[1]  = cmd;
    memcpy(&tx[2], in, (size_t)len);
    tx[2u + (size_t)len] = frame_cs(tx[0], cmd, in, (size_t)len);

    return io_spi(self, tx, NULL, (size_t)2u + (size_t)len + 1u);
}

rn8306_err_t rn8306_update_reg(rn8306_t *self, uint16_t addr, uint8_t len,
                               const uint8_t *clr, const uint8_t *set)
{
    uint8_t cur[4];
    uint8_t i;
    rn8306_err_t err;

    if (self == NULL || len == 0u || len > 4u ||
        ((clr == NULL) && (set == NULL))) {
        return RN8306_ERR_PARAM;
    }

    err = rn8306_read_reg(self, addr, cur, len);
    if (err != RN8306_OK) { return err; }

    for (i = 0; i < len; i++) {
        uint8_t c = (clr != NULL) ? clr[i] : 0u;
        uint8_t s = (set != NULL) ? set[i] : 0u;
        cur[i] = (uint8_t)((cur[i] & (uint8_t)(~c)) | s);
    }

    (void)rn8306_write_protect(self, true);
    err = rn8306_write_reg(self, addr, cur, len);
    {
        rn8306_err_t lk = rn8306_write_protect(self, false);
        if (err == RN8306_OK && lk != RN8306_OK) { err = lk; }
    }
    return err;
}

rn8306_err_t rn8306_update_reg32(rn8306_t *self, uint16_t addr, uint32_t clr_mask, uint32_t set_mask)
{
    uint8_t clr[4], set[4];
    reg_be_pack(clr_mask, clr);
    reg_be_pack(set_mask, set);
    return rn8306_update_reg(self, addr, 4u, clr, set);
}

rn8306_err_t rn8306_burst_read_wave(rn8306_t *self, uint16_t addr, uint8_t count, uint8_t *out)
{
    static const uint8_t bl_of_len[9] = {
        0xFFu,            /* 0  非法 */
        0u,               /* 1  -> BL=000 */
        0xFFu, 0xFFu,     /* 2,3 非法 */
        1u,               /* 4  -> BL=001 */
        0xFFu, 0xFFu, 0xFFu,  /* 5..7 非法 */
        2u,               /* 8  -> BL=010 */
    };
    uint8_t bl;
    uint8_t tx[2 + 64u * 3u + 1u];
    uint8_t rx[2 + 64u * 3u + 1u];
    size_t  data_len, total;
    uint16_t sum;
    size_t  i;
    uint8_t attempt, retry;

    if (self == NULL || self->io == NULL || out == NULL) { return RN8306_ERR_PARAM; }
    if (addr < RN8306_WAVE_BUF_BASE || addr > RN8306_WAVE_BUF_END) { return RN8306_ERR_PARAM; }

    if (count <= 8u) {
        bl = bl_of_len[count];
    } else if (count == 16u) {
        bl = 3u;
    } else if (count == 32u) {
        bl = 4u;      /* 仅波形数据可用 */
    } else if (count == 64u) {
        bl = 5u;      /* 仅波形数据可用 */
    } else {
        bl = 0xFFu;
    }
    if (bl == 0xFFu) { return RN8306_ERR_PARAM; }

    data_len = (size_t)count * 3u;           /* 波形 Burst 每个数据固定 24bit */
    total    = 2u + data_len + 1u;

    tx[0] = (uint8_t)(addr & 0xFFu);
    tx[1] = build_cmd(addr, false, bl);
    for (i = 2u; i < total; i++) { tx[i] = 0xFFu; }

    retry = self->cfg.spi_retry;
    for (attempt = 0; attempt <= retry; attempt++) {
        if (io_spi(self, tx, rx, total) != RN8306_OK) { continue; }

        sum = (uint16_t)tx[0] + (uint16_t)tx[1];
        for (i = 0u; i < data_len; i++) { sum += rx[2u + i]; }

        if ((uint8_t)(~(uint8_t)sum) == rx[2u + data_len]) {
            memcpy(out, &rx[2], data_len);
            return RN8306_OK;
        }
    }
    return RN8306_ERR_CS;
}

rn8306_err_t rn8306_write_protect(rn8306_t *self, bool unlock)
{
    uint8_t v1[1];
    uint8_t v2[4];
    uint8_t cmd = unlock ? (uint8_t)RN8306_WREN_CMD : (uint8_t)RN8306_WRDIS_CMD;
    rn8306_err_t err;

    if (self == NULL) { return RN8306_ERR_PARAM; }

    v1[0] = cmd;
    err = rn8306_write_reg(self, RN8306_REG_WREN, v1, 1u);
    if (err != RN8306_OK) { return err; }

    /* WREN2(0x17F, 4B) 寄存器值: bit[15:8]=命令字(WREN2=0xE5/WRDIS2=0xDC),
     * bit[7:0]=扩位命令(0xAA/0x55), 大端上总线:
     *   解锁 0x0000E5AA -> 线上 00 00 E5 AA
     *   上锁 0x0000DC55 -> 线上 00 00 DC 55
     * 注意: 厂商 HSDC.c 的 {0xAA,0xE5,0,0} 是 CPU 小端内存序, 不能照抄到总线序 */
    v2[0] = 0u;
    v2[1] = 0u;
    v2[2] = cmd;
    v2[3] = unlock ? (uint8_t)RN8306_EXTEN_EN_CMD : (uint8_t)RN8306_EXTEN_DIS_CMD;
    return rn8306_write_reg(self, RN8306_REG_WREN2, v2, 4u);
}

rn8306_err_t rn8306_soft_reset(rn8306_t *self)
{
    uint8_t v = 0xFAu;   /* 官方《RN8302应用程序V1.4.c》整片软复位命令值 */
    rn8306_err_t err;

    if (self == NULL) { return RN8306_ERR_PARAM; }

    /* SOFTRST 命令要求在写使能状态下写入(手册 3.4.37), 否则芯片不动作 */
    err = rn8306_write_protect(self, true);
    if (err != RN8306_OK) { return err; }

    err = rn8306_write_reg(self, RN8306_REG_SOFTRST, &v, 1u);
    if (err != RN8306_OK) { return err; }

    (void)io_delay_us(self, 10000u);   /* 复位后等待 10ms(官方程序 SystemDelay(10)) */
    /* 注意: 整片复位后芯片回到默认测试模式且写保护自动恢复, 故此处不再补上锁;
     * 复位后必须重新 hsdc_setup()(内部会写 WMSW=0xA2 切回 EMM 模式)才能工作 */
    self->st.hsdc_up = false;
    rn8306_hsdc_resync(self);
    return RN8306_OK;
}

rn8306_err_t rn8306_check_device(rn8306_t *self)
{
    uint8_t id[3];
    uint32_t v;
    rn8306_err_t err;

    err = rn8306_read_reg(self, RN8306_REG_DEVICEID, id, 3u);
    if (err != RN8306_OK) { return err; }

    v = (((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2]);
    if ((v != RN8306_DEVICEID_VAL) && (v != RN8306_DEVICEID_ALT_VAL)) {
        return RN8306_ERR_DEVICE;
    }
    return RN8306_OK;
}

/* ------------------------------------------------------------------ */
/* HSDC 配置与启停                                                     */
/* ------------------------------------------------------------------ */

static uint32_t hdsc_ctl_compose(const rn8306_config_t *c, bool with_en)
{
    static const uint8_t clkdiv[4] = { 0u, 1u, 2u, 3u };
    uint32_t v = (uint32_t)(clkdiv[(uint8_t)c->hsdc_clk & 3u]) << 4;

    if (with_en)         { v |= RN8306_HDSC_EN; }
    if (c->cpol)         { v |= RN8306_HDSC_CPOL; }
    if (c->cpha)         { v |= RN8306_HDSC_CPHA; }
    if (c->chk != RN8306_CHK_NONE)  { v |= RN8306_HDSC_CHK_EN; }
    if (c->chk == RN8306_CHK_CRC16) { v |= RN8306_HDSC_CHK_SEL; }
    if (c->byte_inv)     { v |= RN8306_HDSC_BYTE_INV; }
    if (c->ext32)        { v |= RN8306_HDSC_EXT_EN; }
    if (c->bit_inv)      { v |= RN8306_HDSC_BIT_INV; }
    return v;
}

static uint32_t frame_payload_of(const rn8306_config_t *c)
{
    uint32_t bytes_per_sample = c->ext32 ? 4u : 3u;
    return (uint32_t)c->points_per_half * (uint32_t)c->wave_channels * bytes_per_sample;
}

/* HSCLK 各档频率(HDSC_CTL.CLKDIV 分频源 8.192MHz) */
static const uint32_t hsdc_clk_hz[4] = { 4096000u, 2048000u, 1024000u, 512000u };

static uint32_t cs_len_of(const rn8306_config_t *c)
{
    return (c->chk == RN8306_CHK_CRC16) ? 2u :
           (c->chk == RN8306_CHK_SUM32) ? 4u : 0u;
}

/* HSCLK 下限守卫(手册 3.4.27.4): 半块没发完又收到半满标志 -> HSDC_ERR_IF=1
 * 且 EN 自动清零。判据对齐手册 6.6 官方口径(数据量 = 采样率 x 通道数 x 24,
 * "HSDC 速率配置大于等于表中速率即可"): 帧发送时间(含帧尾校验) <= 半块填充时间,
 * 即占用率 <=100% —— 手册表自身含 90% 占用的行(如 128 点@50Hz x6ch -> 1.024M),
 * 按 100% 判据恰好放行, 与官方一致; 官方判"不支持"的行(512 点 x7ch = 4.3Mbps
 * > 4.096M)占用 >100% 被拒。此前的 80% 余量为自创判据, 会拒掉 5 行官方支持
 * 配置(含 512 点 x6ch@4.096M 这一无档可升的行), 已去除。
 * 采样率来源: DS_CFG=01/10/11 三档均为固定 8K(3.4.21.2); DS_CFG=00 同步采样
 * 跟踪电网频率(40~70Hz, 点/周波 x 基频, 手册 2.4.2), 要求用户经 cfg->
 * sync_sample_hz 显式给出(合法范围 2560~35840Hz = 64 点@40Hz ~ 512 点@70Hz),
 * 未给或越界均拒绝(防止高点数配置静默放行 / 误填极小值使守卫失效)。 */
static rn8306_err_t check_hsdc_rate(const rn8306_config_t *c, uint32_t frame_payload)
{
    uint64_t sample_hz;

    /* 芯片半块结构硬约束(容量+定点数): 缓存共 896 地址单元(200H~57FH,
     * 手册 3.4.20); mode14/17 半块 448 地址 / mode0~12/15 半块 384 地址
     * (6.3.2) —— 6/7 通道档每通道固定 64 点/半块, 由 BUFCFG 档位决定、
     * 不可配置。pp>64 超过半块容量; pp<64 芯片仍按 64 点/半块发送,
     * 两者都导致驱动期望帧长与芯片实际错位, 故锁定 pp == 64。 */
    if (c->points_per_half != 64u) { return RN8306_ERR_PARAM; }

    if ((c->dma_wavecon & 0x03u) != 0x00u) {
        sample_hz = 8000u;                       /* DS_CFG=01/10/11: 固定 8K */
    } else if ((c->sync_sample_hz >= 2560u) && (c->sync_sample_hz <= 35840u)) {
        sample_hz = c->sync_sample_hz;           /* DS_CFG=00: 用户显式给出 */
    } else {
        return RN8306_ERR_PARAM;                 /* 同步采样率未给或越界 */
    }
    {
        uint64_t frame_bits = (uint64_t)(frame_payload + cs_len_of(c)) * 8u;
        uint64_t tx_us   = frame_bits * 1000000u / hsdc_clk_hz[(uint8_t)c->hsdc_clk & 3u];
        uint64_t fill_us = (uint64_t)c->points_per_half * 1000000u / sample_hz;
        if (tx_us > fill_us) { return RN8306_ERR_PARAM; }
    }
    return RN8306_OK;
}

/* 读改写 32 位寄存器(调用方需已解除写保护) */
static rn8306_err_t rmw_reg32(rn8306_t *self, uint16_t addr, uint32_t clr, uint32_t set)
{
    uint8_t  b[4];
    uint32_t v;
    rn8306_err_t err = rn8306_read_reg32(self, addr, &v);

    if (err != RN8306_OK) { return err; }
    v = (v & ~clr) | set;
    reg_be_pack(v, b);
    return rn8306_write_reg(self, addr, b, 4u);
}

/* 带回读校验的寄存器写(官方程序实践: 每笔关键写后回读比对)。
 * 失败重写一次, 仍不一致返回 RN8306_ERR_VERIFY。 */
static rn8306_err_t write_reg_v(rn8306_t *self, uint16_t addr, const uint8_t *in, uint8_t len)
{
    uint8_t rb[4];
    uint8_t attempt;
    rn8306_err_t err = RN8306_OK;

    for (attempt = 0; attempt < 2u; attempt++) {
        err = rn8306_write_reg(self, addr, in, len);
        if (err != RN8306_OK) { return err; }
        err = rn8306_read_reg(self, addr, rb, len);
        if (err != RN8306_OK) { return err; }
        /* WSAVECON[5:4] is a command on write and live cache status on read.
         * Compare only its stable configuration bits; all other registers
         * retain the full read-back check. */
        if (addr == RN8306_REG_WSAVECON && len == 1u) {
            if (((rb[0] ^ in[0]) & 0xCFu) == 0u) { return RN8306_OK; }
        } else if (memcmp(rb, in, (size_t)len) == 0) {
            return RN8306_OK;
        }
    }
    return RN8306_ERR_VERIFY;
}

/* rmw + 回读校验(用于关键寄存器) */
static rn8306_err_t rmw_reg32_v(rn8306_t *self, uint16_t addr, uint32_t clr, uint32_t set)
{
    uint8_t  b[4];
    uint32_t v;
    rn8306_err_t err = rn8306_read_reg32(self, addr, &v);

    if (err != RN8306_OK) { return err; }
    v = (v & ~clr) | set;
    reg_be_pack(v, b);
    return write_reg_v(self, addr, b, 4u);
}

rn8306_err_t rn8306_hsdc_setup(rn8306_t *self)
{
    rn8306_err_t err;
    uint8_t  b1;
    uint8_t  bufcfg;      /* BUFCFG[4:0] */
    uint32_t wavecon3;

    if (self == NULL || self->io == NULL) { return RN8306_ERR_PARAM; }
    /* 通道数仅支持 6/7: 3/4 通道档为芯片固定通道集合(mode1/2/13/16, 手册
     * 6.2.2/6.3.2)且 3ch 半块为 128 点, wave_channels 抽象无法表达, 不支持 */
    if ((self->cfg.wave_channels != 6u) && (self->cfg.wave_channels != 7u)) {
        return RN8306_ERR_PARAM;
    }
    if (self->cfg.points_per_half == 0u) {
        return RN8306_ERR_PARAM;
    }
    /* 先验证再提交: 守卫失败时直接返回, 不触碰任何状态 —— 正在运行的旧布局
     * 波形流(含 frame_payload/hsdc_up/帧定界)不受本次失败影响 */
    {
        uint32_t payload = frame_payload_of(&self->cfg);
        err = check_hsdc_rate(&self->cfg, payload);
        if (err != RN8306_OK) { return err; }
        self->frame_payload = payload;
    }

    rn8306_hsdc_resync(self);
    self->st.hsdc_up = false;

    /* 波形缓存格式 BUFCFG[4:0] = {DMA_WAVECON3[1], WSAVECON[3:0]}(手册 6.2, p146-147):
     *   7 通道(4V+3I) -> BUFCFG=10010(mode14): 序 UA,UB,UC,IA,IB,IC,IN, 半块 448 地址
     *                   (备选 10101/mode17 亦为 7 通道, 但顺序为 UA,IA,UB,IB,UC,IC,IN)
     *   6 通道        -> BUFCFG=00000(mode0):  序 UA,UB,UC,IA,IB,IC, 半块 384 地址
     *                   (备选 10011/mode15, 顺序 UA,IA,UB,IB,UC,IC)
     * 表低段(单通道/纯 IN 档)存在排版笔误、含义不确定, 不允许显式使用 */
    if (self->cfg.bufcfg != 0xFFu) {
        bufcfg = (uint8_t)(self->cfg.bufcfg & 0x1Fu);
        /* 显式 bufcfg 白名单 + 与通道数一致性校验(防配置不匹配导致静默帧错位) */
        switch (bufcfg) {
        case 0x00u: case 0x13u:                    /* 6 通道 */
            if (self->cfg.wave_channels != 6u) { return RN8306_ERR_PARAM; }
            break;
        case 0x12u: case 0x15u:                    /* 7 通道 */
            if (self->cfg.wave_channels != 7u) { return RN8306_ERR_PARAM; }
            break;
        default:
            return RN8306_ERR_PARAM;               /* 未验证档位(含手册排版错乱区) */
        }
    } else {
        bufcfg = (self->cfg.wave_channels >= 7u) ? 0x12u : 0x00u;
    }

    /* 写入方式: verify_writes=true 时关键寄存器走回读校验路径 */
    #define WREG(addr, data, len) \
        (self->cfg.verify_writes ? write_reg_v(self, (addr), (data), (len)) \
                                 : rn8306_write_reg(self, (addr), (data), (len)))
    #define RMW32V(addr, clr, set) \
        (self->cfg.verify_writes ? rmw_reg32_v(self, (addr), (clr), (set)) \
                                 : rmw_reg32(self, (addr), (clr), (set)))

    /* 1) 两级写使能(setup 期间所有受保护寄存器的写入都依赖它, 结束时统一恢复) */
    err = rn8306_write_protect(self, true);
    if (err != RN8306_OK) { return err; }

    /* 2) 切换到 EMM 计量模式: RN8306 上电/复位后的缺省工作模式手册未明示,
     *    写 GOEMM(0xA2) 兜底确保进入 EMM(若已在 EMM, 芯片不动作, 手册 3.4.36)。
     *    WMSW 为命令型寄存器: 写 0xA2 成功后读出为模式标志 2'b01(非 0xA2),
     *    不能按写入值回读校验, 改用 SYSSR.WMS[1:0] 确认(手册 3.4.42, bits[15:14]) */
    b1 = RN8306_WMSW_EMM;
    err = rn8306_write_reg(self, RN8306_REG_WMSW, &b1, 1u);
    if (err != RN8306_OK) { return err; }
    {
        uint8_t st[2];
        uint8_t attempt;
        rn8306_err_t rd = RN8306_ERR_IO;
        /* 若从 SLM/NVM 切入 EMM 可能发生唤醒复位(手册 3.5.4), 首读或为旧值:
         * 校验不通过时隔 1ms 重读一次。错误分类: 读失败(IO/CS)原样透传,
         * 便于上板区分"SPI 不通"与"模式未切成功"; 读成功但 WMS 不对才报 VERIFY */
        for (attempt = 0; attempt < 2u; attempt++) {
            rd = rn8306_read_reg(self, RN8306_REG_SYSSR, st, 2u);
            if (rd == RN8306_OK) {
                if ((st[0] & (uint8_t)(RN8306_SYSSR_WMS_MSK >> 8)) ==
                    (uint8_t)(RN8306_SYSSR_WMS_EMM >> 8)) {
                    break;
                }
                rd = RN8306_ERR_VERIFY;
            }
            if (attempt == 0u) { (void)io_delay_us(self, 1000u); }
        }
        if (rd != RN8306_OK) { return rd; }
    }

    /* 3) SYSCFG 打开 HSDC 功能: 0xEA000000|0x1000 沿用厂商参考代码写法
     *    (高 8 位 0xEA 是 SYS_PS 写密码, WO 只写, 回读恒不含; bit12 手册标
     *    Reserved, 置 1 为厂商 HSDC 用法) —— 回读不可能等于写入值, 不做校验 */
    err = rmw_reg32(self, RN8306_REG_SYSCFG, 0u, 0xEA000000u | 0x1000u);
    if (err != RN8306_OK) { return err; }

    /* 4) 模块软复位: 0xE1=MMU 波形缓存 DMA, 0x4C=HSDC(厂商参考代码);
     *    复位后 HDSC_CTL 恢复默认(EN=0), 满足"参数仅 EN=0 时可配"(p157) */
    b1 = 0xE1u;
    err = rn8306_write_reg(self, RN8306_REG_SOFTRST, &b1, 1u);
    if (err != RN8306_OK) { return err; }
    b1 = 0x4Cu;
    err = rn8306_write_reg(self, RN8306_REG_SOFTRST, &b1, 1u);
    if (err != RN8306_OK) { return err; }
    (void)io_delay_us(self, 100u);

    /* 5) 波形格式与采样率
     *    WSAVECON    : bit[3:0]=BUFCFG[3:0]; bit7 仅 DS_CFG=00 时有效, 固定 8K 下无关
     *    DMA_WAVECON : 采样率, 默认 0x01(DS_CFG=01=固定 8K, 手册 p91-92)
     *    DMA_WAVECON3: bit10=交叉缓存, bit1=BUFCFG[4](不启动, START 由 hsdc_start 置位) */
    b1 = (uint8_t)(0x80u | (bufcfg & 0x0Fu));
    err = WREG(RN8306_REG_WSAVECON, &b1, 1u);
    if (err != RN8306_OK) { return err; }

    {
        uint8_t wc[4];
        reg_be_pack(self->cfg.dma_wavecon, wc);
        /* DMA_WAVECON 不走回读校验: WAVECNT=0 时该寄存器在缓存过程中会自变
         * (反映实际采样分频数, 手册 3.4.21.2), 回读值不保证等于写入值 */
        err = rn8306_write_reg(self, RN8306_REG_DMA_WAVECON, wc, 4u);
        if (err != RN8306_OK) { return err; }
    }

    /* 5a-1) 可选: DMA_WAVECON2(CIC 直流校正/增益自动调整/谐波衰减补偿)。
     *       默认 0=不写(保持复位 0x3F800, 原始波形直通); 0x0700=厂商 HSDC
     *       流参考配置(波形幅值与计量通道对齐), 幅值对不上时启用 */
    if (self->cfg.dma_wavecon2 != 0u) {
        uint8_t w2[4];
        reg_be_pack(self->cfg.dma_wavecon2, w2);
        err = WREG(RN8306_REG_DMA_WAVECON2, w2, 4u);
        if (err != RN8306_OK) { return err; }
    }

    wavecon3 = RN8306_WAVECON3_CROSS | ((((uint32_t)bufcfg >> 4) & 1u) << 1);
    {
        uint8_t w3[4];
        reg_be_pack(wavecon3, w3);
        err = WREG(RN8306_REG_DMA_WAVECON3, w3, 4u);
        if (err != RN8306_OK) { return err; }
    }

    /* 5a) IN 通道(第 4 路电压)缓存使能: 7 通道交叉模式保持复位 0(IN/IA 独立流转, p125-126);
     *     若上板实测 IN 无数据, 置 cfg->in_wave_en=true 复测(6 通道模式置 1 时 IN 顶替 IA) */
    if (self->cfg.in_wave_en) {
        uint8_t inen[2] = { 0x00u, 0x01u };
        err = rn8306_write_reg(self, RN8306_REG_IN_WAVEEN, inen, 2u);
        if (err != RN8306_OK) { return err; }
    }

    /* 5b) 清 HSDC 残留中断标志(EMMIF2 bit11/12, 写 1 清零, p101) */
    {
        uint8_t f[4];
        reg_be_pack(RN8306_EMMIE2_HSDC_TWAVE_HIE | RN8306_EMMIE2_HSDC_ERR_IE, f);
        err = rn8306_write_reg(self, RN8306_REG_EMMIF2, f, 4u);
        if (err != RN8306_OK) { return err; }
    }

    /* 6) HDSC_CTL 参数(EN=0 时写入, 手册 6.5 流程 p152-153) */
    {
        uint8_t hc[4];
        reg_be_pack(hdsc_ctl_compose(&self->cfg, false), hc);
        err = WREG(RN8306_REG_HDSC_CTL, hc, 4u);
        if (err != RN8306_OK) { return err; }
    }

    /* 7) 使能 HSDC 半块输出中断与错误中断(EMMIE2[12:11]), 再置 HDSC_CTL.EN=1
     *    (顺序按手册 6.5: 先 EMMIE2 后 EN) */
    err = RMW32V(RN8306_REG_EMMIE2, 0u,
                 RN8306_EMMIE2_HSDC_TWAVE_HIE | RN8306_EMMIE2_HSDC_ERR_IE);
    if (err != RN8306_OK) { return err; }

    err = RMW32V(RN8306_REG_HDSC_CTL, 0u, RN8306_HDSC_EN);
    if (err != RN8306_OK) { return err; }

    /* 8) 恢复写保护(避免芯片长期处于写使能态) */
    err = rn8306_write_protect(self, false);
    if (err != RN8306_OK) { return err; }

    #undef WREG
    #undef RMW32V

    self->st.hsdc_up = true;
    return RN8306_OK;
}

/* 等 HSDC 发送空闲(EMMIF2 bit20 HSDC_BUSY, 手册 3.4.27.4)。
 * 超时仍返回错误, 由调用方决定是否继续(避免卡死)。 */
static rn8306_err_t wait_hsdc_idle(rn8306_t *self, uint32_t tries)
{
    uint32_t v;

    while (tries-- > 0u) {
        if (rn8306_read_reg32(self, RN8306_REG_EMMIF2, &v) != RN8306_OK) {
            return RN8306_ERR_IO;
        }
        if ((v & RN8306_EMMIF2_HSDC_BUSY) == 0u) { return RN8306_OK; }
        (void)io_delay_us(self, 200u);
    }
    return RN8306_ERR_STATE;
}

rn8306_err_t rn8306_hsdc_start(rn8306_t *self, bool en)
{
    rn8306_err_t err;

    if (self == NULL) { return RN8306_ERR_PARAM; }
    if (!self->st.hsdc_up && en) { return RN8306_ERR_STATE; }

    err = rn8306_write_protect(self, true);
    if (err != RN8306_OK) { return err; }

    if (en) {
        /* 启动: HDSC_CTL.EN=1 + DMA_WAVECON3.START=1, 半满后 HSDC 自动推送 */
        err = rmw_reg32(self, RN8306_REG_HDSC_CTL, 0u, RN8306_HDSC_EN);
        if (err == RN8306_OK) {
            err = rmw_reg32(self, RN8306_REG_DMA_WAVECON3, 0u, RN8306_WAVECON3_START);
        }
    } else {
        /* 停止: 手册 3.4.27.4 建议 HSDC_BUSY=1 时不要清 EN —— 先等发送空闲
         * (超时则继续清, 避免卡死); 再停波形缓存 + 关 HSDC 接口 + 关 HSDC 中断
         * (避免停止后 INT1 持续触发), 并复位驱动状态(重新启动需重走 setup)。
         * 等待预算按帧发送时间动态计算(200us/次, 多留 1.6ms 余量):
         * 慢档位如 7ch@2.048M 一帧 5.3ms, 固定次数会等不到 */
        {
            uint64_t tx_us = ((uint64_t)(self->frame_payload + cs_len_of(&self->cfg)) * 8u * 1000000u)
                             / hsdc_clk_hz[(uint8_t)self->cfg.hsdc_clk & 3u];
            uint32_t tries = (uint32_t)(tx_us / 200u) + 8u;
            (void)wait_hsdc_idle(self, tries);
        }
        err = rmw_reg32(self, RN8306_REG_DMA_WAVECON3, RN8306_WAVECON3_START, 0u);
        if (err == RN8306_OK) {
            err = rmw_reg32(self, RN8306_REG_HDSC_CTL, RN8306_HDSC_EN, 0u);
        }
        if (err == RN8306_OK) {
            err = rmw_reg32(self, RN8306_REG_EMMIE2,
                            RN8306_EMMIE2_HSDC_TWAVE_HIE | RN8306_EMMIE2_HSDC_ERR_IE, 0u);
        }
        self->st.hsdc_up = false;
        rn8306_hsdc_resync(self);
    }
    if (err == RN8306_OK) {
        err = rn8306_write_protect(self, false);
    }
    return err;
}

rn8306_err_t rn8306_hsdc_irq_enable(rn8306_t *self, bool half_ready, bool error)
{
    uint32_t clr = 0u, set = 0u;
    rn8306_err_t err;

    if (self == NULL) { return RN8306_ERR_PARAM; }

    if (half_ready) { set |= RN8306_EMMIE2_HSDC_TWAVE_HIE; } else { clr |= RN8306_EMMIE2_HSDC_TWAVE_HIE; }
    if (error)      { set |= RN8306_EMMIE2_HSDC_ERR_IE;  } else { clr |= RN8306_EMMIE2_HSDC_ERR_IE;  }

    err = rn8306_write_protect(self, true);
    if (err != RN8306_OK) { return err; }
    err = rmw_reg32(self, RN8306_REG_EMMIE2, clr, set);
    if (err == RN8306_OK) {
        err = rn8306_write_protect(self, false);
    }
    return err;
}

rn8306_err_t rn8306_hsdc_set_layout(rn8306_t *self, uint8_t channels,
                                    uint16_t points_per_half, bool ext32)
{
    rn8306_config_t probe;
    uint32_t payload;

    if (self == NULL) { return RN8306_ERR_PARAM; }
    if (points_per_half == 0u) { return RN8306_ERR_PARAM; }
    /* 通道数只在 setup 时写入芯片(BUFCFG), 本函数不重配芯片 —— 改通道数
     * 会导致芯片侧帧长与驱动期望错位, 必须重新 hsdc_setup() */
    if (channels != self->cfg.wave_channels) { return RN8306_ERR_PARAM; }

    /* 先在副本上验证新布局(含 HSCLK 速率守卫), 通过后才应用 —— 防止绕过守卫 */
    probe = self->cfg;
    probe.wave_channels   = channels;
    probe.points_per_half = points_per_half;
    probe.ext32           = ext32;
    payload = frame_payload_of(&probe);
    {
        rn8306_err_t err = check_hsdc_rate(&probe, payload);
        if (err != RN8306_OK) { return err; }
    }

    self->cfg = probe;
    self->frame_payload = payload;
    rn8306_hsdc_resync(self);
    return RN8306_OK;
}

/* ------------------------------------------------------------------ */
/* 用户侧入口: 中断与连续数据流                                        */
/* ------------------------------------------------------------------ */

void rn8306_int_handler(rn8306_t *self, rn8306_int_src_t src)
{
    if (self == NULL) { return; }

    switch (src) {
    case RN8306_INT_WAVE_HALF:
        emit_evt(self, RN8306_EVT_WAVE_HALF_READY);
        break;
    case RN8306_INT_HSDC_ERR:
        self->st.hsdc_up = false;       /* 芯片已自动清除 HDSC_CTL.EN */
        emit_evt(self, RN8306_EVT_HSDC_ERROR);
        break;
    case RN8306_INT_OTHER:
    default:
        break;                           /* 预留: 用户可自行扩展处理 */
    }
}

static void hsdc_frame_done(rn8306_t *self, bool ok)
{
    if (ok) { self->st.frames_ok++;  emit_evt(self, RN8306_EVT_WAVE_FRAME_OK); }
    else    { self->st.frames_err++; emit_evt(self, RN8306_EVT_WAVE_FRAME_ERR); }
    self->st.in_frame = false;
}

static bool hsdc_verify_cs(const rn8306_t *self)
{
    uint8_t expect_be[4];
    uint8_t n, i;
    bool match_be = true, match_le = true;

    if (self->cfg.chk == RN8306_CHK_CRC16) {
        uint16_t c = self->st.crc;
        expect_be[0] = (uint8_t)(c >> 8);
        expect_be[1] = (uint8_t)(c);
        n = 2u;
    } else {
        uint32_t s = self->st.sum32;
        expect_be[0] = (uint8_t)(s >> 24);
        expect_be[1] = (uint8_t)(s >> 16);
        expect_be[2] = (uint8_t)(s >> 8);
        expect_be[3] = (uint8_t)(s);
        n = 4u;
    }

    for (i = 0u; i < n; i++) {
        if (self->st.cs[i] != expect_be[i]) { match_be = false; }
        if (self->st.cs[i] != expect_be[n - 1u - i]) { match_le = false; }
    }

    /* 手册时序图(p150-151)仅展示 BYTE_INV=0 时校验字段高字节在前, BYTE_INV=1 时
     * 校验字段的字节序手册未明确。AUTO=两种序任一匹配; 现场读 HSDC_CRC(0x1CA)
     * 与本地计算结果比对定序后, 应将 cfg.cs_order 锁定为 BE/LE, 恢复对字节序
     * 配置错误的检出能力。 */
    switch (self->cfg.cs_order) {
    case RN8306_CS_BE: return match_be;
    case RN8306_CS_LE: return match_le;
    case RN8306_CS_AUTO:
    default:           return match_be || match_le;
    }
}

rn8306_err_t rn8306_hsdc_feed(rn8306_t *self, const uint8_t *data, uint32_t len)
{
    uint8_t cs_len;

    if (self == NULL || (data == NULL && len != 0u)) { return RN8306_ERR_PARAM; }
    if (!self->st.hsdc_up) { return RN8306_ERR_STATE; }

    cs_len = (self->cfg.chk == RN8306_CHK_CRC16) ? 2u :
             (self->cfg.chk == RN8306_CHK_SUM32) ? 4u : 0u;

    while (len > 0u) {
        if (!self->st.in_frame) {
            self->st.in_frame     = true;
            self->st.phase        = 0u;
            self->st.payload_left = self->frame_payload;
            self->st.sum32        = 0u;
            self->st.crc          = 0u;
            self->st.cs_got       = 0u;
        }

        if (self->st.phase == 0u) {
            /* 数据段: 收到即整段转发给用户(无内部缓冲), 同时累加校验 */
            uint32_t n = (len < self->st.payload_left) ? len : self->st.payload_left;

            if (self->on_wave != NULL) { self->on_wave(self->wave_ctx, self, data, n); }

            if (self->cfg.chk == RN8306_CHK_CRC16) {
                self->st.crc = crc16_ccitt_update(self->st.crc, data, (size_t)n);
            } else {
                uint32_t i;
                for (i = 0u; i < n; i++) { self->st.sum32 += data[i]; }
            }

            data += n;
            len  -= n;
            self->st.payload_left -= n;

            if (self->st.payload_left == 0u) {
                if (cs_len == 0u) { hsdc_frame_done(self, true); }
                else              { self->st.phase = 1u; }
            }
        } else {
            /* 帧尾校验段: 截取不下发(校验字节不是波形数据) */
            uint32_t remain = (uint32_t)cs_len - self->st.cs_got;
            uint32_t n = (len < remain) ? len : remain;

            memcpy(&self->st.cs[self->st.cs_got], data, (size_t)n);
            self->st.cs_got = (uint8_t)(self->st.cs_got + n);
            data += n;
            len  -= n;

            if (self->st.cs_got == cs_len) {
                hsdc_frame_done(self, hsdc_verify_cs(self));
            }
        }
    }
    return RN8306_OK;
}

void rn8306_hsdc_resync(rn8306_t *self)
{
    if (self == NULL) { return; }
    self->st.in_frame = false;
    self->st.phase    = 0u;
}

void rn8306_get_stats(const rn8306_t *self, rn8306_stats_t *out)
{
    if (self == NULL || out == NULL) { return; }
    out->frames_ok  = self->st.frames_ok;
    out->frames_err = self->st.frames_err;
}

/* ------------------------------------------------------------------ */
/* 工具函数                                                            */
/* ------------------------------------------------------------------ */

int32_t rn8306_sample24(const uint8_t *data, bool byte_inv)
{
    uint32_t raw;
    if (byte_inv) {
        raw = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
    } else {
        raw = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | (uint32_t)data[2];
    }
    if ((raw & 0x800000u) != 0u) { raw |= 0xFF000000u; }   /* 24bit 符号拓展 */
    return (int32_t)raw;
}

/* ------------------------------------------------------------------ */
/* 构造                                                                */
/* ------------------------------------------------------------------ */

rn8306_err_t rn8306_init(rn8306_t *self, const rn8306_if_t *itf, const rn8306_config_t *cfg)
{
    if (self == NULL || itf == NULL || itf->io == NULL) { return RN8306_ERR_PARAM; }

    memset(self, 0, sizeof(*self));
    self->io       = itf->io;
    self->io_ctx   = itf->io_ctx;
    self->on_wave  = itf->on_wave;
    self->wave_ctx = itf->wave_ctx;
    self->on_evt   = itf->on_evt;
    self->evt_ctx  = itf->evt_ctx;
    self->cfg      = (cfg != NULL) ? *cfg : (rn8306_config_t)RN8306_CONFIG_DEFAULT;

    self->frame_payload = frame_payload_of(&self->cfg);
    rn8306_hsdc_resync(self);
    return RN8306_OK;
}
