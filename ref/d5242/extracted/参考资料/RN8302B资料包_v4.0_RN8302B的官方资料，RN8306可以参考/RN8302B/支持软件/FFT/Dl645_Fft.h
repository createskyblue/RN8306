#ifndef _DL645_Fft_H
#define _DL645_Fft_H

#define NUM_FFT 128 // Length of FFT to process
#define PI 3.1415926


typedef struct 
{
//	s16 iRealArray[NUM_FFT];
//	s16 iMageArray[NUM_FFT];
	float iRealArray[NUM_FFT];
	float iMageArray[NUM_FFT];
	u16	FU[3];			//---基波电压---NNN.N6
	u32	FI[4];			//---基波电流NNNN.NNNN
	u32 HarmonicpercentU[3][51];
	u32	HarmonicpercentI[3][51];			//---谐波含有率--NNN.N6
}sDl645FftHarmonicData_TypeDef;

typedef struct 
{
    u8  Channel;					 // 当前采样通道
    u16  ReadAdress;				 // 读取地址
    u8  StarFlag;					 // 开始进行谐波数据分析
    u8  ADSPIBusy;					 // 进行谐波分析瞬时值数据采样
    u16 DataCount;					 // 当前采样点数
    u16 ReadAddres;				 	 // 采样点数据地址
    u16	dwFreq;					 	 // 采样周期当前频率
    u16 TimeOutStamp;				 // 采样延时退出
    u8 InstantaneousData[384];      //当前采样128点瞬时数据
} sADE_Instantaneous_TypeDef;

typedef enum
{
	UAChannel		= 0x00,
	UBChannel		= 0x01,
	UCChannel		= 0x02,
	IAChannel		= 0x03,
	IBChannel		= 0x04,
	ICChannel		= 0x05
}eADEChannelFlag_TypeDef;
//

extern void fnDl645Fft_Harmonic_Exec(void);
extern void fnDl645Fft_init(void);
extern void Int_FFT(s16 ReArray[], s16 ImArray[]);
extern void Bit_Reverse(s16 BR_Array[]);

#endif



