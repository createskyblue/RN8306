/**
 * ============================================================================
 *  rn8306_selftest.c - 用"模拟芯片"验证驱动协议逻辑(无需真实硬件)
 * ============================================================================
 *  模拟芯片行为(按《用户手册》协议):
 *    - 读帧: 芯片收到 [ADDR][CMD] 后, 在数据段返回寄存器内容, 帧尾附校验和
 *            CS = ~(ADDR+CMD+DATA);
 *    - 写帧: 校验 MCU 发来的 [ADDR][CMD][DATA][CS], 错误则记失败;
 *    - HSDC: 按驱动配置生成 "payload + 帧尾校验" 字节流, 喂给驱动。
 *  编译: gcc -std=c99 -Wall -Wextra -I../src -o rn8306_selftest ../src/rn8306.c rn8306_selftest.c
 * ============================================================================
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rn8306.h"

/* ------------------------------------------------------------------ */
/* 模拟芯片                                                            */
/* ------------------------------------------------------------------ */
static uint8_t  mock_regs[0x800];
static uint8_t  mock_wren2[4];      /* WREN2(0x17F,4B): 真实芯片为独立寄存器,
                                     * 不与扁平地址 0x180(WREN)/0x181(WMSW)/0x182(SOFTRST) 相互覆盖 */
static uint8_t  mock_emmif2[4];     /* EMMIF2(0x16C,4B): 同理, 不与 EMMIE2(0x16B..0x16E) 窗口重叠 */
static int      mock_bad_write_frames;
static int      mock_bad_cmd_frames;    /* CMD 的 BL 编码与数据长度不符的帧数 */
static uint32_t mock_wren_count;

static void mock_reset(void)
{
    memset(mock_regs, 0, sizeof(mock_regs));
    memset(mock_wren2, 0, sizeof(mock_wren2));
    memset(mock_emmif2, 0, sizeof(mock_emmif2));
    mock_regs[0x18F] = 0x83; mock_regs[0x190] = 0x02; mock_regs[0x191] = 0x00;  /* DeviceID */
    mock_regs[0x178] = 0x00; mock_regs[0x179] = 0x03;                           /* SYSCFG = 0x00038A00 */
    mock_regs[0x17A] = 0x8A; mock_regs[0x17B] = 0x00;
    mock_regs[0x1C9] = 0x00; mock_regs[0x1CA] = 0x00;                           /* HDSC_CTL 复位=0x04 */
    mock_regs[0x1CC] = 0x04;
    mock_bad_write_frames = 0;
    mock_bad_cmd_frames = 0;
    mock_wren_count = 0;
}

/* 用户 IO 回调: 模拟一颗 RN8306 */
static int mock_io(void *ctx, rn8306_io_op_t op, rn8306_io_msg_t *m)
{
    (void)ctx;
    switch (op) {
    case RN8306_IO_SPI_XFER: {
        uint16_t sum = (uint16_t)m->tx[0] + (uint16_t)m->tx[1];
        size_t   data_len = m->len - 3u;              /* 去掉 ADDR/CMD/CS */
        size_t   i;

        if ((m->tx[1] & 0x80u) == 0u) {
            /* 读帧: 返回寄存器内容 + 校验和 */
            uint16_t base = (uint16_t)(((uint16_t)(m->tx[1] >> 4) & 0x07u) << 8);
            uint16_t ra = (uint16_t)(base + m->tx[0]);
            uint8_t bl_field = (uint8_t)(((m->tx[1] >> 2) & 0x03u) | (((m->tx[1] >> 1) & 0x01u) << 2));
            static const uint16_t bl_len[8] = { 1u, 4u, 8u, 16u, 32u, 64u, 0u, 0u };

            /* CMD 的 BL 编码须与数据长度一致(正文序 BL[1:0]@bit[3:2], BL[2]@bit[1]):
             * 仅对波形 burst(3 字节对齐且>=12B)校验, 单寄存器读(1~4B)不适用 */
            if (data_len >= 12u && (data_len % 3u) == 0u && bl_len[bl_field] != data_len / 3u) {
                mock_bad_cmd_frames++;
            }
            for (i = 0; i < data_len; i++) {
                uint8_t d;
                if (ra == 0x016Cu && data_len == 4u) {
                    d = mock_emmif2[i];               /* EMMIF2: 独立寄存器, 读写一致 */
                } else {
                    d = mock_regs[ra + i];
                }
                if (m->rx != NULL) { m->rx[2u + i] = d; }
                sum += d;
            }
            if (m->rx != NULL) { m->rx[2u + data_len] = (uint8_t)(~(uint8_t)sum); }
        } else {
            /* 写帧: 校验 MCU 发来的校验和, 并入寄存器存储 */
            uint16_t base = (uint16_t)(((uint16_t)(m->tx[1] >> 4) & 0x07u) << 8);
            for (i = 0; i < data_len; i++) { sum += m->tx[2u + i]; }
            if ((uint8_t)(~(uint8_t)sum) != m->tx[2u + data_len]) {
                mock_bad_write_frames++;
            } else {
                uint16_t ra = (uint16_t)(base + m->tx[0]);
                if (ra == 0x017Fu && data_len == 4u) {
                    memcpy(mock_wren2, &m->tx[2], 4u);   /* WREN2: 独立 4 字节寄存器 */
                } else if (ra == 0x016Cu && data_len == 4u) {
                    memcpy(mock_emmif2, &m->tx[2], 4u);  /* EMMIF2: 独立 4 字节寄存器 */
                } else if (ra == 0x0181u && data_len == 1u && m->tx[2] == 0xA2u) {
                    mock_regs[0x181] = 0x01u;  /* WMSW 命令型: 写 0xA2 后读出为模式标志 2'b01 */
                    mock_regs[0x18A] = 0x40u;  /* SYSSR.WMS[1:0]=2'b01(bits[15:14]), 高字节=0x40 */
                    mock_regs[0x18B] = 0x00u;
                } else if (ra == 0x0178u && data_len == 4u) {
                    /* SYSCFG: 高 8 位 SYS_PS 为 WO 写密码(0xEA), 回读恒为 0 */
                    mock_regs[0x178] = 0x00u;
                    mock_regs[0x179] = m->tx[3];
                    mock_regs[0x17A] = m->tx[4];
                    mock_regs[0x17B] = m->tx[5];
                } else {
                    for (i = 0; i < data_len; i++) { mock_regs[ra + i] = m->tx[2u + i]; }
                    if (ra == 0x0180u && data_len == 1u && m->tx[2] == RN8306_WREN_CMD) {
                        mock_wren_count++;
                    }
                }
            }
        }
        return 0;
    }
    case RN8306_IO_DELAY_US:
    case RN8306_IO_RESET:
        return 0;
    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* 回调捕获                                                            */
/* ------------------------------------------------------------------ */
static uint8_t  wave_buf[8192];
static uint32_t wave_len;
static int      wave_calls;
static rn8306_evt_t last_evt;
static int      evt_count;

static void test_wave(void *ctx, rn8306_t *self, const uint8_t *data, uint32_t len)
{
    (void)ctx; (void)self;
    if (wave_len + len <= sizeof(wave_buf)) { memcpy(&wave_buf[wave_len], data, len); }
    wave_len += len;
    wave_calls++;
}

static void test_evt(void *ctx, rn8306_t *self, rn8306_evt_t evt)
{
    (void)ctx; (void)self;
    last_evt = evt;
    evt_count++;
}

/* ------------------------------------------------------------------ */
/* 断言                                                                */
/* ------------------------------------------------------------------ */
static int failures;

#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { printf("PASS  %s\n", name); }                          \
        else      { printf("FAIL  %s\n", name); failures++; }              \
    } while (0)

/* ------------------------------------------------------------------ */
/* 用例                                                                */
/* ------------------------------------------------------------------ */
static void test_register_access(void)
{
    rn8306_t    rn;
    rn8306_if_t itf;
    uint8_t     v3[3];
    uint32_t    v32;
    rn8306_err_t err;

    mock_reset();
    memset(&itf, 0, sizeof(itf));
    itf.io = mock_io;
    itf.on_evt = test_evt;
    err = rn8306_init(&rn, &itf, NULL);
    CHECK(err == RN8306_OK, "init default cfg");

    /* DeviceID: 3 字节读 + 帧校验 */
    err = rn8306_read_reg(&rn, RN8306_REG_DEVICEID, v3, 3u);
    CHECK(err == RN8306_OK && v3[0] == 0x83 && v3[1] == 0x02 && v3[2] == 0x00,
          "read_reg DeviceID(0x830200)");
    CHECK(rn8306_check_device(&rn) == RN8306_OK, "check_device");

    /* 32 位读 + 读改写: SYSCFG |= 0xEA001000
     * (mock 模拟 SYSCFG 高 8 位为 WO 写密码: 回读恒无 0xEA) */
    err = rn8306_read_reg32(&rn, RN8306_REG_SYSCFG, &v32);
    CHECK(err == RN8306_OK && v32 == 0x00038A00u, "read_reg32 SYSCFG reset value");

    err = rn8306_update_reg32(&rn, RN8306_REG_SYSCFG, 0u, 0xEA000000u | 0x1000u);
    CHECK(err == RN8306_OK, "update_reg32 SYSCFG");

    err = rn8306_read_reg32(&rn, RN8306_REG_SYSCFG, &v32);
    CHECK(err == RN8306_OK && v32 == 0x00039A00u,
          "SYSCFG read-back merged (high 8B WO, no 0xEA)");

    /* update_reg32 结束时应恢复写保护(WRDIS=0xDC 写入 WREN) */
    CHECK(mock_regs[0x180] == RN8306_WRDIS_CMD, "update_reg32 relocks (WRDIS=0xDC)");

    /* 驱动发出的所有写帧校验和都被模拟芯片认可 */
    CHECK(mock_bad_write_frames == 0, "all write frames have valid checksum");
    CHECK(mock_bad_cmd_frames == 0, "burst CMD BL encoding matches length (vendor order)");

    /* 写使能确实发生过(update_reg32 内部两级写使能) */
    CHECK(mock_wren_count >= 1, "WREN(0xE5) issued");

    /* 整片软复位: 需先解锁(SOFTRST 要求写使能状态), 复位值 0xFA 写入 0x182 */
    err = rn8306_soft_reset(&rn);
    CHECK(err == RN8306_OK && mock_regs[0x182] == 0xFAu, "soft_reset 0xFA written (unlocked)");
}

static void test_burst_read(void)
{
    rn8306_t    rn;
    rn8306_if_t itf;
    uint8_t     wave[64u * 3u];
    uint8_t     i;
    rn8306_err_t err;

    mock_reset();
    memset(&itf, 0, sizeof(itf));
    itf.io = mock_io;
    rn8306_init(&rn, &itf, NULL);

    /* 预填波形缓存: mock 按 SPI burst 读的递增地址返回 0x200 起的内容 */
    for (i = 0; i < 192u; i++) { mock_regs[0x200u + i] = (uint8_t)(i + 1u); }

    err = rn8306_burst_read_wave(&rn, 0x0200u, 64u, wave);
    CHECK(err == RN8306_OK, "burst_read_wave 64 points");
    CHECK(wave[0] == 0x01 && wave[1] == 0x02 && wave[191] == 0xC0,
          "burst data consecutive");
}

static void test_hsdc_stream(void)
{
    rn8306_t      rn;
    rn8306_if_t   itf;
    rn8306_config_t cfg = RN8306_CONFIG_DEFAULT;
    uint32_t      payload;                     /* 64 点 x 7 通道 x 3B = 1344B */
    uint32_t      sum = 0;
    uint8_t      *frame;
    uint32_t      i;
    rn8306_stats_t st;

    /* 用户硬件: 4V(UA/UB/UC+IN) + 3I(IA/IB/IC) = 7 通道 */
    cfg.wave_channels = 7u;
    payload = 64u * 7u * 3u;

    mock_reset();
    memset(&itf, 0, sizeof(itf));
    itf.io = mock_io;
    itf.on_wave = test_wave;
    itf.on_evt = test_evt;
    rn8306_init(&rn, &itf, &cfg);

    /* --- 配置阶段: 驱动应按 7 通道选择 BUFCFG=10010 并正确拼装寄存器值 --- */
    CHECK(rn8306_hsdc_setup(&rn) == RN8306_OK, "hsdc_setup");
    /* mock 模拟命令型 WMSW: 写 0xA2 后寄存器读出为模式标志 0x01。
     * 本断言同时是 A1 回归: verify_writes=true 下 setup 必须仍返回 OK */
    CHECK(mock_regs[0x181] == 0x01u, "WMSW readback = mode flag 0x01 (cmd-type reg)");
    CHECK(rn.frame_payload == payload, "frame_payload = 64*7*3 (BUFCFG=10010)");
    CHECK(mock_regs[0x163] == 0x82u, "WSAVECON = 0x82 (BUFCFG[3:0]=0010, 7 通道交叉)");
    CHECK(mock_regs[0x196] == 0x01u, "DMA_WAVECON = 0x01 (固定 8K)");
    CHECK(rn8306_hsdc_start(&rn, true) == RN8306_OK, "hsdc_start");
    /* 大端: 0x00000403(交叉+BUFCFG[4]+启动) 的低字节在第 4 字节 */
    CHECK(mock_regs[0x1E5] == 0x00u && mock_regs[0x1E8] == 0x03u,
          "DMA_WAVECON3 = 0x403(交叉+BUFCFG[4]+启动)");
    /* EMMIE2 先于 HDSC_CTL.EN 配置(手册 6.5), 最终值不变 */
    CHECK(mock_regs[0x16B] == 0x00 && mock_regs[0x16C] == 0x00 &&
          mock_regs[0x16D] == 0x18 && mock_regs[0x16E] == 0x00,
          "EMMIE2 = 0x1800 (TWAVE_HIE|ERR_IE)");
    /* HDSC_CTL 期望: EN|CPHA|CHK_EN|BYTE_INV|CLKDIV=0 = 0x0000040D
     * (写后回读校验 verify_writes=true 生效: mock 回读值与写入一致) */
    CHECK(mock_regs[0x1C9] == 0x00 && mock_regs[0x1CA] == 0x00 &&
          mock_regs[0x1CB] == 0x04 && mock_regs[0x1CC] == 0x0D,
          "HDSC_CTL = 0x0000040D (with write verify)");
    /* setup 结束应恢复写保护 */
    CHECK(mock_regs[0x180] == RN8306_WRDIS_CMD, "setup relocks at end");

    /* --- 帧接收: "payload + 4B 校验", 分 3 段喂入模拟 DMA 分块到达 --- */
    frame = malloc(payload + 4u);
    for (i = 0; i < payload; i++) { frame[i] = (uint8_t)(i * 7u + 3u); }
    for (i = 0; i < payload; i++) { sum += frame[i]; }

    wave_len = 0; wave_calls = 0; evt_count = 0;
    /* case1: 校验字段低字节在前(与 byte_inv=1 对应) */
    frame[payload + 0u] = (uint8_t)(sum);
    frame[payload + 1u] = (uint8_t)(sum >> 8);
    frame[payload + 2u] = (uint8_t)(sum >> 16);
    frame[payload + 3u] = (uint8_t)(sum >> 24);

    rn8306_hsdc_feed(&rn, frame, 400u);
    rn8306_hsdc_feed(&rn, frame + 400u, 500u);
    rn8306_hsdc_feed(&rn, frame + 900u, payload + 4u - 900u);

    rn8306_get_stats(&rn, &st);
    CHECK(wave_calls == 3u && wave_len == payload, "on_wave got full payload (no buffering)");
    CHECK(memcmp(wave_buf, frame, payload) == 0, "payload bytes intact");
    CHECK(st.frames_ok == 1u && st.frames_err == 0u, "frame ok");
    CHECK(last_evt == RN8306_EVT_WAVE_FRAME_OK, "FRAME_OK event");

    /* case2: 坏帧(校验全 0, 大端/小端序都不可能匹配) */
    memset(&frame[payload], 0, 4u);
    evt_count = 0;
    rn8306_hsdc_feed(&rn, frame, payload + 4u);
    rn8306_get_stats(&rn, &st);
    CHECK(st.frames_err == 1u && last_evt == RN8306_EVT_WAVE_FRAME_ERR, "bad cs -> FRAME_ERR");

    /* case3: resync 后重新对齐(低字节在前的好帧) */
    frame[payload + 0u] = (uint8_t)(sum);
    frame[payload + 1u] = (uint8_t)(sum >> 8);
    frame[payload + 2u] = (uint8_t)(sum >> 16);
    frame[payload + 3u] = (uint8_t)(sum >> 24);
    rn8306_hsdc_resync(&rn);
    rn8306_hsdc_feed(&rn, frame, payload + 4u);
    rn8306_get_stats(&rn, &st);
    CHECK(st.frames_ok == 2u, "resync + good frame");

    /* case4: 校验字段高字节在前也应通过(AUTO 模式两种序任一匹配;
     * 现场应用 HSDC_CRC(0x1CA) 定序后应把 cs_order 锁定为 BE/LE) */
    frame[payload + 0u] = (uint8_t)(sum >> 24);
    frame[payload + 1u] = (uint8_t)(sum >> 16);
    frame[payload + 2u] = (uint8_t)(sum >> 8);
    frame[payload + 3u] = (uint8_t)(sum);
    rn8306_hsdc_feed(&rn, frame, payload + 4u);
    rn8306_get_stats(&rn, &st);
    CHECK(st.frames_ok == 3u, "big-endian cs order also accepted (AUTO)");

    /* case5: cs_order 锁定为 LE 后, 大端序校验字段应判 FAIL */
    rn.cfg.cs_order = RN8306_CS_LE;
    rn8306_hsdc_feed(&rn, frame, payload + 4u);
    rn8306_get_stats(&rn, &st);
    CHECK(st.frames_err == 2u, "cs_order=LE rejects BE frame");
    rn.cfg.cs_order = RN8306_CS_AUTO;

    /* case6: hsdc_start(false) 应关 EN/START/EMMIE2 并使 feed 拒收 */
    CHECK(rn8306_hsdc_start(&rn, false) == RN8306_OK, "hsdc_stop");
    CHECK(rn8306_hsdc_feed(&rn, frame, payload + 4u) == RN8306_ERR_STATE,
          "feed rejected after stop");
    CHECK((mock_regs[0x16E] & 0x18u) == 0x00u, "EMMIE2 bits cleared on stop");

    /* --- 中断入口 --- */
    rn8306_int_handler(&rn, RN8306_INT_WAVE_HALF);
    CHECK(last_evt == RN8306_EVT_WAVE_HALF_READY, "int handler -> HALF_READY");
    rn8306_int_handler(&rn, RN8306_INT_HSDC_ERR);
    CHECK(last_evt == RN8306_EVT_HSDC_ERROR, "int handler -> HSDC_ERROR");

    free(frame);
}

static void test_sample24(void)
{
    uint8_t le[3]  = { 0x78, 0x56, 0x34 };   /* byte_inv: 低字节在前 -> 0x345678 */
    uint8_t le_n[3] = { 0xFF, 0xFF, 0xF7 };  /* 0xF7FFFF -> 符号拓展 -0x080001 */
    CHECK(rn8306_sample24(le, true) == 0x345678, "sample24 byte_inv");
    CHECK(rn8306_sample24(le_n, true) == (int32_t)0xFFF7FFFFu, "sample24 sign extend");
}

static void test_rate_guard(void)
{
    rn8306_t      rn;
    rn8306_if_t   itf;
    rn8306_config_t def = RN8306_CONFIG_DEFAULT;
    rn8306_config_t cfg;

    mock_reset();
    memset(&itf, 0, sizeof(itf));
    itf.io = mock_io;

    /* 守卫矩阵(64 点/半块, SUM32, 80% 余量):
     * 5ch@1.024M 发送 7.53ms > 6.4ms -> 拒; 4ch@1.024M 6.03ms -> 放行 */
    cfg = def;
    cfg.wave_channels = 7u; cfg.hsdc_clk = RN8306_HSDC_CLK_1M024;   /* dma_wavecon=0x01 */
    rn8306_init(&rn, &itf, &cfg);
    CHECK(rn8306_hsdc_setup(&rn) == RN8306_ERR_PARAM, "guard rejects 7ch@1.024M (DS_CFG=01)");

    cfg = def;
    cfg.wave_channels = 5u; cfg.hsdc_clk = RN8306_HSDC_CLK_1M024;
    rn8306_init(&rn, &itf, &cfg);
    CHECK(rn8306_hsdc_setup(&rn) == RN8306_ERR_PARAM, "guard rejects 5ch@1.024M");

    cfg = def;
    cfg.wave_channels = 4u; cfg.hsdc_clk = RN8306_HSDC_CLK_1M024;
    rn8306_init(&rn, &itf, &cfg);
    CHECK(rn8306_hsdc_setup(&rn) == RN8306_OK, "guard allows 4ch@1.024M");

    /* DS_CFG=10/11 也是固定 8K(手册 3.4.21.2), 守卫须覆盖 */
    cfg = def;
    cfg.wave_channels = 7u; cfg.hsdc_clk = RN8306_HSDC_CLK_512K;
    cfg.dma_wavecon = 0x02u;                                          /* DS_CFG=10 */
    rn8306_init(&rn, &itf, &cfg);
    CHECK(rn8306_hsdc_setup(&rn) == RN8306_ERR_PARAM, "guard covers DS_CFG=10 (fixed 8K)");

    cfg.dma_wavecon = 0x03u;                                          /* DS_CFG=11 */
    rn8306_init(&rn, &itf, &cfg);
    CHECK(rn8306_hsdc_setup(&rn) == RN8306_ERR_PARAM, "guard covers DS_CFG=11 (fixed 8K)");

    /* DS_CFG=00 为同步采样(非固定 8K), 守卫跳过 */
    cfg.dma_wavecon = 0x00u;
    rn8306_init(&rn, &itf, &cfg);
    CHECK(rn8306_hsdc_setup(&rn) == RN8306_OK, "guard skips DS_CFG=00 (sync mode)");

    /* set_layout 不能绕过守卫: 4ch@1.024M 合法, 改 7ch 应被拒且布局不变 */
    cfg = def;
    cfg.wave_channels = 4u; cfg.hsdc_clk = RN8306_HSDC_CLK_1M024;
    rn8306_init(&rn, &itf, &cfg);
    (void)rn8306_hsdc_setup(&rn);
    CHECK(rn8306_hsdc_set_layout(&rn, 7u, 64u, false) == RN8306_ERR_PARAM,
          "set_layout rejected by rate guard");
    CHECK(rn.frame_payload == 4u * 64u * 3u, "layout unchanged after rejection");
    CHECK(rn8306_hsdc_set_layout(&rn, 3u, 64u, false) == RN8306_OK, "set_layout valid change OK");
    CHECK(rn.frame_payload == 3u * 64u * 3u, "layout applied after valid change");
}

int main(void)
{
    test_register_access();
    test_burst_read();
    test_hsdc_stream();
    test_sample24();
    test_rate_guard();
    printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return (failures == 0) ? 0 : 1;
}
