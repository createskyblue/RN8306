# RN8306 驱动（4V+3I / 8K / HSDC 连续波形流）

[English](README.md) | **简体中文**

## RN8306 是什么？

RN8306 是深圳锐能微科技（Renergy，隶属上海贝岭/华大半导体体系）推出的三相电能计量专用芯片，属 RN830x/RN730x V3 家族。片内集成 7 路 Sigma-Delta ADC——3 路相电压、3 路相电流和 1 路独立差分输入 IN（可作第 4 路电压或零线电流），可完成三相电压/电流/功率/电能的高精度计量与谐波分析，符合 IEC 与 IR46 相关标准。芯片对外提供两条串行链路：SPI 从机接口供 MCU 读写寄存器（配置、校表、读计量结果）；HSDC 高速接口由芯片作主机，把 ADC 原始波形缓存以最高 4.096MHz 时钟连续推送给 MCU。典型应用为智能电表、能源管理与电力监测。

> 说明：**HSDC**（本驱动实现的芯片高速波形接口）与 **HDSC**（华大半导体，锐能微的控股集团：
> 中国电子 CEC → 华大半导体 → 上海贝岭 → 锐能微）是两件不相关的事，纯属缩写巧合。

## 驱动简介

平台无关的 C 语言驱动，面向锐能微 **RN8306** 三相计量芯片（RN830x/RN730x V3 家族）。
OOP 风格对象 + 依赖注入（用户注入一个 IO 回调，内部 switch-case 自己处理 SPI 收发/延时/复位），
通过芯片的 **HSDC 高速接口**（芯片为 SPI 主机、MCU 作从机接收）获取 **8K 采样率、7 通道**
（UA/UB/UC + 第 4 路电压接 IN + IA/IB/IC）的连续 24bit 波形数据流——无内部缓冲，收到即回调上抛。

## 目录结构

```
src/        驱动本体：rn8306.h / rn8306.c（仅依赖 C 标准库）
examples/   使用示例：rn8306_example.c（依赖注入回调、中断入口、数据流接法）
test/       主机自测：rn8306_selftest.c（内置"模拟芯片"，73 项断言）
doc/        协议文档：RN8306_通信协议_SPI_HSDC.md（SPI + HSDC 协议整理、4V+3I 配置序列、踩坑清单）
            rn8306.pdf（官方《RN830x_RN730x V3 数据手册》Rev1.5）
ref/        官方参考资料：用户手册 V1.2、数据手册 V1.3、厂商 HSDC.c 参考源码、RN8302B 资料包
```

## 快速上手

```c
rn8306_if_t itf = { .io = my_io_cb, .on_wave = my_wave_cb, .on_evt = my_evt_cb };
rn8306_config_t cfg = RN8306_CONFIG_DEFAULT;   /* 8K / SUM32 / 字节反序 */
cfg.wave_channels = 7;                          /* 4V+3I: UA,UB,UC,IA,IB,IC,IN */

rn8306_init(&rn, &itf, &cfg);
rn8306_check_device(&rn);      /* 读 DeviceID 自检 */
rn8306_hsdc_setup(&rn);        /* 含 WMSW 切 EMM 模式、两级写使能、BUFCFG=10010 */
/* 启动推流前，先将 MCU 的 SPI 从机接收缓冲入队。 */
rn8306_hsdc_start(&rn, true);  /* SPI 从机接收完成 -> rn8306_hsdc_feed() */
```

完整接法见 [examples/rn8306_example.c](examples/rn8306_example.c)。
协议与寄存器依据见 [doc/RN8306_通信协议_SPI_HSDC.md](doc/RN8306_通信协议_SPI_HSDC.md)。

编译示例 / 自测（主机 gcc，无硬件依赖）：

```bash
cd examples && gcc -std=c99 -Wall -Wextra -I../src -o demo ../src/rn8306.c rn8306_example.c
cd test     && gcc -std=c99 -Wall -Wextra -I../src -o selftest ../src/rn8306.c rn8306_selftest.c && ./selftest
```

## 实板验证

**2026-10-29 已在 ESP32-S3 + RN8306 实板验证通过，寄存器通信、连续波形采集和 SD 记录均工作正常。**

| 验证项 | 结果 |
|--------|------|
| DeviceID | `0x830200`，寄存器 SPI 帧校验通过 |
| 波形采集 | 7 通道、24 bit；产品集成配置实测约 6.4 kHz，源驱动默认 8 kHz |
| 连续运行 | 130 秒，帧校验错误、帧长错误、管线丢帧均为 0 |
| SD 记录 | 每个完整 30 秒分段含 192000 个采样帧 |
| 文件完整性 | 文件头、波形数据、收尾块 CRC 全部通过 |
| 驱动主机自测 | 73 项断言全部通过 |

WSAVECON 回读校验已按实板行为修正：`[5:4]` 为实时缓存状态，其余配置位仍严格校验，
解决了写入 `0x82`、回读 `0xB2` 时误报 `ERR_VERIFY` 的问题。

上述实测覆盖通信、采集和存储链路。模拟增益标定、相位精度和物理输入对应关系需结合信号发生器测量。
寄存器细节记录见 [doc/OPEN_QUESTIONS.md](doc/OPEN_QUESTIONS.md)。

若使用同步采样（`dma_wavecon` 的 DS_CFG=00 档），必须显式设置
`cfg.sample_rate_hz = 期望波形采样率（Hz）`（合法范围 2560~35840Hz），并按需设置
`cfg.sync_follow_frequency` 跟踪输入频率；固定 8K 档（DS_CFG=01/10/11）必须传入
`RN8306_SAMPLE_RATE_8K_HZ`。否则 `hsdc_setup()` 返回 `ERR_PARAM`；HSCLK 选档判据按
手册 6.6：占用率 ≤100%。
