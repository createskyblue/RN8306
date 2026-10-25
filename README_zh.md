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
test/       主机自测：rn8306_selftest.c（内置"模拟芯片"，54 项断言）
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
rn8306_hsdc_start(&rn, true);  /* 之后 INT1 中断 -> SPI 从机收数 -> rn8306_hsdc_feed() */
```

完整接法见 [examples/rn8306_example.c](examples/rn8306_example.c)。
协议与寄存器依据见 [doc/RN8306_通信协议_SPI_HSDC.md](doc/RN8306_通信协议_SPI_HSDC.md)。

编译示例 / 自测（主机 gcc，无硬件依赖）：

```bash
cd examples && gcc -std=c99 -Wall -Wextra -I../src -o demo ../src/rn8306.c rn8306_example.c
cd test     && gcc -std=c99 -Wall -Wextra -I../src -o selftest ../src/rn8306.c rn8306_selftest.c && ./selftest
```

## ⚠️ 状态说明

**尚未上板测试。** 当前经过的验证仅有：

1. 多轮独立 AI 审核——两轮互不共享结论的子智能体对照官方手册逐页核对（SPI 帧格式、
   WREN/WREN2 写使能、BUFCFG 波形缓存格式、HDSC_CTL 位域、8K 采样率配置等），
   加一轮人工 400DPI 读表裁决；随后**两轮外部 AI 审核**又分别抓出 2 个默认配置下
   必挂的回读校验问题（WMSW 命令型寄存器/SYSCFG 只写密码位）和 1 个 burst 读
   CMD 位序 bug（由厂商官方 FFT 代码 `fnRN8302_ReadBuf` 铁证裁决），均已修复
   并有回归自测覆盖；
2. 主机端"模拟芯片"自测（54 项断言，含"命令型寄存器/只写位/CMD-BL 编码/速率守卫"等校验）。

**当前阶段无法从手册闭环的问题**（手册自相矛盾、多次审读冲突等）统一记录在
[doc/OPEN_QUESTIONS.md](doc/OPEN_QUESTIONS.md)，上板后逐项验证关闭。

上板前必测清单（详见协议文档 2.11 节）：WMSW=0xA2 必要性、BYTE_INV=1 帧尾校验字节序
（用 HSDC 寄存器 0x1CA 定序后锁定 `cs_order`）、DMA_WAVECON2 复位值中的 AGC 位、
GSxx_WAVE 增益、DeviceID 实际返回值。
