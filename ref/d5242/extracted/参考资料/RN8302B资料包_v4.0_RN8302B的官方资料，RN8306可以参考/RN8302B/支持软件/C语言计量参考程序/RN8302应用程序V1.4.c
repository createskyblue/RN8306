//修订记录
/*
2021.11.05 V1.4
1、删除了Rdata寄存器校验。
*/
typedef unsigned char  		u8;
typedef signed char  		s8;
typedef unsigned short  	u16;
typedef signed short  		s16;
typedef unsigned long int  	u32;
typedef signed long int  	s32;
typedef unsigned short      WORD;
typedef	unsigned char	BOOL;

#define PinWrite_ADSCLK(x)  	( (x) ? (P0_bit.no6 = 1 , PM0_bit.no6 = 1) : (PM0_bit.no6 = 0 , P0_bit.no6 = 0) )	//
#define PinMode_ADSCLK(x)		( (x == GPIO_MODE_OUT) ? (PM0_bit.no6 = 0) : (PM0_bit.no6 = 1) )


#define PinRead_ADSDI()			(P4_bit.no4)
#define PinWrite_ADSDO(x)  		( (x) ? (P6_bit.no0 = 1 , PM6_bit.no0 = 1) : (PM6_bit.no0 = 0 , P6_bit.no0 = 0) )	//	



#define PinWrite_ADCS(x)  	( (x) ? (P6_bit.no1 = 1 , PM6_bit.no1 = 1) : (PM6_bit.no1 = 0 , P6_bit.no1 = 0  ) )
#define PinWrite_ADRST(x)  	( (x) ? (P0_bit.no5 = 1 , PM0_bit.no5 = 1) : (PM0_bit.no5 = 0 , P0_bit.no5 = 0  ) )

typedef enum {ERROR = 0 , SUCCESS = !ERROR} ErrorStatus;

typedef struct {
//	u32 Dat : 23;
//	u32 S : 1;
	u8 Dat0;
	u8 Dat1;
	u8 Dat2 : 7;
	u8 S : 1;			
} sDF09;
typedef struct {
	u32 Dat;
} sDF11;
typedef	sDF11 sFsEgSh_TypeDef;	


#pragma vector = INTTM01_vect
__interrupt void MD_INTTM01(void)
{
	TMIF01 = 0;		/* INTTM01 interrupt flag clear */
	SysStamp ++ ;
}

u16 fnStamp_Through(u16 Ago)
{
	extern u16	SysStamp;
	if (SysStamp >= Ago)
		{return (SysStamp - Ago);}
	else
		{return ( (0xffff - Ago) + SysStamp);}
}

u16 fnHexToBcd_u16(u16 Dat)
{
	u16 Result = 0;

	Dat = Dat % 10000;
	Result += (Dat / 1000) * 0x1000;
	Dat = Dat % 1000;	
	Result += (Dat / 100) * 0x100;
	Dat = Dat % 100;	
	Result += (Dat / 10) * 0x10;
	Dat = Dat % 10;	
	Result += Dat;
		
	return(Result);	
}
u32	fnHexToBcd_u32(u32 Dat)
{	
	u32 result = 0;
	
	Dat = Dat % 100000000;
	result += (Dat / 10000000) * 0x10000000;
	Dat = Dat % 10000000;	
	result += (Dat / 1000000) * 0x1000000;
	Dat = Dat % 1000000;
	result += (Dat / 100000) * 0x100000;
	Dat = Dat % 100000;
	result += (Dat / 10000) * 0x10000;
	Dat = Dat % 10000;	
	result += (Dat / 1000) * 0x1000;
	Dat = Dat % 1000;
	result += (Dat / 100) * 0x100;
	Dat = Dat % 100;
	result += (Dat / 10) * 0x10;
	Dat = Dat % 10;	
	result += Dat;
	
	return(result);
}

u16 fnDFConver_Bcd16To16(s16 Dat)
{
	u16 Result;
	Result = abs(Dat) % 8000;			
	Result = fnHexToBcd_u16(Result);
        if(Dat < 0 ) Result |= 0x8000;
	else Result &= 0x7fff;	
	return(Result);
}
u32 fnDFConver_Bcd32To32(s32 Dat)
{
	u32 Result;
	Result = labs(Dat) % 80000000;			
	Result = fnHexToBcd_u32(Result);
        if(Dat < 0 ) Result |= 0x80000000;
	else Result &= 0x7fffffff;	
	return(Result);
}

sDF09 fnDFConver_Hex32ToDF09(s32 Dat)
{	
	sDF09	Result;	
	memset(&Result , 0 , sizeof(sDF09) );
	if(Dat < 0) Result.S = 1;
	else Result.S = 0;	
        
	Dat = labs(Dat) % 800000;
	Dat = fnHexToBcd_u32(Dat);
	
	Result.Dat0 = Dat;
	Result.Dat1 = Dat >> 8;
	Result.Dat2 = Dat >> 16;
	return(Result);	
}

typedef union                                         //公共数据运算区8字节
{
	u8  ucTempBuf[8];
	u32 lTemp32;
	u16 wTemp16;
	u8  ucTemp8;
}sDl645StruDataComm_TypeDef;

__no_init  sDl645StruDataComm_TypeDef      Dl645RN8302DataComm;
__no_init	sDl645FrontTmp_TypeDef	        Dl645FrontTmp;
__no_init	sDl645Front_TypeDef		        Dl645Front;
__no_init 	sDl645Eg_TypeDef 		        Dl645Eg;
__no_init   sDl645FirmParaFile_TypeDef          Dl645FirmPara;

typedef struct
{
    u8		ChkErrCnt;       //读错误计数1
    
    s32 	Pw[12];   		//{Pa Pb Pc P Qa Qb Qc Q Sa Sb Sc S}   48 
    s32 	UI[7];       	//Ua Ub Uc Ia Ib Ic Inal   28 
    s32		VectorU[9];		// 正序、负序、零序电压
    s32		VectorI[9];		// 正序、负序、零序电流
    s32  	Pf[4];       	//Pf Pfa Pfb Pfc      16     
    u32 	Frequency;   	//电网频率，单位：        4  
    s32  	YUI[3],YUU[2];  //20
                               	
    s32		Pulse[15];		//前台高频脉冲48
    //---电能脉冲---	
    s32		Pulse_EgTmp[20];	//高频脉冲{P,Q,Ps},{Pa,Qa,Psa},{Pb,Qb,Psb},{Pc,Qc,Psc}{Fp,Fq}{Fpa,Fqa}{Fpb,Fqb}{Fpc,Fqc}
    u32		Pulse_Eg[20];  //低频脉冲数
	//---需量脉冲---
	s32		Pulse_NeedTmp[12];	
  	u16		Pulse_Need[12]; //{PNeed,QNeed,PsNeed},{PNeeda,QNeeda,PsNeeda},{PNeedb,QNeedb,PsNeedb},{PNeedc,QNeedc,PsNeedc}48
    

    u16		Angle[9];
    u16 	PDirect;   //4
    u32 	ChkSum1;   //4
    u32 	ChkSum2;   //4
	
	u16		Temperature;	//温度4
	u32		ClockBat;		//时钟电池4
	u32		BackupBat;		//后备电池4
	
    u16   CF1DelayStamp;
    u16   CF2DelayStamp;
    
    u16   CfIn_P;
    u16   CfIn_q;
    
    u16   CfTime_P;
    u16   CfTime_q;
	
} sDl645FrontTmp_TypeDef;

typedef struct
{	
	struct sFrontPubData_TypeDef  {
		u16		U[3];			//---电压---NNN.N6
		u16		VectorU[9];		// 正序电压 0--2  负序电压 3---5  零序电压 6---8
		u32		I[4];			//---电流NNNN.NNNN(电流值要求3整3小，整定值要求2整4小，最高位表示方向)---16		
		u32		VectorI[9];		// 正序电流 0--2  负序电流 3---5  零序电流 6---8
		sDF09	Pw[12];			//---瞬时有功/无功/视在功率NN.NNNN---{P Pa Pb Pc Q Qa Qb Qc S Sa Sb Sc}36
		u16  	Pf[4];			//---功率因数N.NNN---	最高位表示方向{Pf Pfa Pfb Pfc}	8  sDF05
		u16		Angle[9];		//---相角NNN.N---		18
								//PhUb,PhUc, 以A相电压为基准，B、C相角度				//(hyg) BCD码
	        					//PhIa,PhIb,PhIc,A相电流与A相电压间角度、B相电流与B相电压间角度、C相电流与C相电压间角度
	        					//Angle A,Angle B,Angle C, A相电流与A相电压间角度、B相电流与A相电压间角度、C相电流与A相电压间角度
	        					//Angle C-Angle A 
		u32		UnblU;			//电压不平衡度NNNN.NN%4
		u32		UnblI;			//电流不平衡度NNNN.NN%4
		
		u16		FuzzyU[3];		//---电压波形失真度NN.NN%---	6	
		u16		FuzzyI[3];		//---电流波形失真度NN.NN%---	6	
		u16		WaveU[3][21];	//---相电压谐波含量NN.NN%---	126	
		u16		WaveI[3][21];	//---相电流谐波含量NN.NN%---    126
		//---其他---
		u16		Frequency;		//NN.NN 2
		u32		PPwave;			//NN.NNNN 4
		
		u16		Temperature;	//NNN.N 2
		u16		ClockBat;		//NN.NN 2
		u16		BackupBat;		//NN.NN 2
		u8		PDirect;		//原功率方向，用于需量处理(0总/1A/2B/3C , 注意与FrontTmp不同) 1
	} PubData;
	
	struct sFrontPriData_TypeDef  {		
		u8		Flag;			//---工作异常标志---1
		u8      PhaseCalStep;
		u16     FrontStamp;
//		u8      BatCalStep;
		u16     BatStamp;
//		u16     BatDetStamp;
	} PriData;	
	
	struct sFrontPriPara_TypeDef  {		
		u32	 PConstE;			//有功常数
		u32	 QConstE;			//无功常数(Hex码)4
		u16	 Crc;//2
	} PriPara;		
} sDl645Front_TypeDef;	//缓冲区转换后的有效数据

typedef struct 
{		
	u8 FractionI;/*电流小数位数*/
	u8 FractionU;/*电压小数位数*/
	u8 FractionE;/*电能小数位数*/
	u8 FractionP;/*功率需量小数位数*/
	u16 ConstE;/*电表有功常数*/
	u16 Pulse_deltaE;/*数*/
	u8  HighPulseRate;
	u8 PhaseCalStep;
	u32 IRmsConst;			//10
	u16	Reserve[2];
	u16	NOLOAD;
	u16	ZEROSTAR;					//20
	u32 ChkSum1;
	u16 HFConst1;
	u16 HFConst2;
	u32 VRmsConst;			//30
	u16	Reserve1;
	u32 PRmsConst;
	u16	Reserve2;
	u16	VGain[3];				// 42
	u16	IGain[4];				//48
	u8	PHSU[2];				// 56
	u32	PHSI[3];
	u16	PRth[4];				//70
	u16	UI_Offset[7];
	u16	P_PHS[3];/*有功相位校正寄存器*/				// 92
	u16	PGain[3];/*有功增益校正寄存器*/				
}sDl645FirmParaFile_TypeDef;

typedef struct {
#pragma pack(1)
	struct sDl645EgPubData_TypeDef
	{
		sFsEgSh_TypeDef AllPEgSh[DL645_MAX_FL + 1];		
		sFsEgSh_TypeDef PtPEgSh[DL645_MAX_FL + 1];      
		sFsEgSh_TypeDef NtPEgSh[DL645_MAX_FL + 1];      
		
		sFsEgSh_TypeDef AllQEgSh[DL645_MAX_FL + 1];     
		sFsEgSh_TypeDef PtQEgSh[DL645_MAX_FL + 1];      
		sFsEgSh_TypeDef NtQEgSh[DL645_MAX_FL + 1];      
		
		sFsEgSh_TypeDef Qd1QEgSh[DL645_MAX_FL + 1];		
		sFsEgSh_TypeDef Qd2QEgSh[DL645_MAX_FL + 1];		
		sFsEgSh_TypeDef Qd3QEgSh[DL645_MAX_FL + 1];		
		sFsEgSh_TypeDef Qd4QEgSh[DL645_MAX_FL + 1];		
		
		sFsEgSh_TypeDef PtPsEgSh[DL645_MAX_FL + 1];		
		sFsEgSh_TypeDef NtPsEgSh[DL645_MAX_FL + 1];		
		
		sFsEgSh_TypeDef AssEgSh;						
		sFsEgSh_TypeDef BasePtPEgSh;					
		sFsEgSh_TypeDef BaseNtPEgSh;					
		sFsEgSh_TypeDef WavePtPEgSh;					
		sFsEgSh_TypeDef WaveNtPEgSh;					
		sFsEgSh_TypeDef CopPEgSh;						
		sFsEgSh_TypeDef IronPEgSh;							
//-------------	
		sFsEgSh_TypeDef PtPEgSha;						
		sFsEgSh_TypeDef NtPEgSha;						
		
		sFsEgSh_TypeDef PtQEgSha;						
		sFsEgSh_TypeDef NtQEgSha;						
		
		sFsEgSh_TypeDef Qd1QEgSha;						
		sFsEgSh_TypeDef Qd2QEgSha;						
		sFsEgSh_TypeDef Qd3QEgSha;						
		sFsEgSh_TypeDef Qd4QEgSha;						
		
		sFsEgSh_TypeDef PtPsEgSha;						
		sFsEgSh_TypeDef NtPsEgSha;						
		
		sFsEgSh_TypeDef AssEgSha;						
		sFsEgSh_TypeDef BasePtPEgSha;					
		sFsEgSh_TypeDef BaseNtPEgSha;					
		sFsEgSh_TypeDef WavePtPEgSha;					
		sFsEgSh_TypeDef WaveNtPEgSha;						
		sFsEgSh_TypeDef CopPEgSha;						
		sFsEgSh_TypeDef IronPEgSha;							
//-------------	
		sFsEgSh_TypeDef PtPEgShb;						
		sFsEgSh_TypeDef NtPEgShb;						
		
		sFsEgSh_TypeDef PtQEgShb;						
		sFsEgSh_TypeDef NtQEgShb;						
		
		sFsEgSh_TypeDef Qd1QEgShb;						
		sFsEgSh_TypeDef Qd2QEgShb;						
		sFsEgSh_TypeDef Qd3QEgShb;						
		sFsEgSh_TypeDef Qd4QEgShb;						
		
		sFsEgSh_TypeDef PtPsEgShb;						
		sFsEgSh_TypeDef NtPsEgShb;						
		
		sFsEgSh_TypeDef AssEgShb;						
		sFsEgSh_TypeDef BasePtPEgShb;					
		sFsEgSh_TypeDef BaseNtPEgShb;					
		sFsEgSh_TypeDef WavePtPEgShb;					
		sFsEgSh_TypeDef WaveNtPEgShb;						
		sFsEgSh_TypeDef CopPEgShb;						
		sFsEgSh_TypeDef IronPEgShb;							
//-------------	
		sFsEgSh_TypeDef PtPEgShc;						
		sFsEgSh_TypeDef NtPEgShc;						
		
		sFsEgSh_TypeDef PtQEgShc;						
		sFsEgSh_TypeDef NtQEgShc;						
		
		sFsEgSh_TypeDef Qd1QEgShc;						
		sFsEgSh_TypeDef Qd2QEgShc;						
		sFsEgSh_TypeDef Qd3QEgShc;						
		sFsEgSh_TypeDef Qd4QEgShc;						
		
		sFsEgSh_TypeDef PtPsEgShc;						
		sFsEgSh_TypeDef NtPsEgShc;						
		
		sFsEgSh_TypeDef AssEgShc;						
		sFsEgSh_TypeDef BasePtPEgShc;					
		sFsEgSh_TypeDef BaseNtPEgShc;					
		sFsEgSh_TypeDef WavePtPEgShc;					
		sFsEgSh_TypeDef WaveNtPEgShc;						
		sFsEgSh_TypeDef CopPEgShc;						
		sFsEgSh_TypeDef IronPEgShc;	
    } PubData; 						
#pragma pack()

	struct sDl645EgPubDataCrc_TypeDef
	{
		u16 AllPEgSh;		
		u16 PtPEgSh;      
		u16 NtPEgSh;      
		
		u16 AllQEgSh;     
		u16 PtQEgSh;      
		u16 NtQEgSh;      
		
		u16 Qd1QEgSh;		
		u16 Qd2QEgSh;		
		u16 Qd3QEgSh;		
		u16 Qd4QEgSh;		
		
		u16 PtPsEgSh;		
		u16 NtPsEgSh;		
		
		u16 AssEgSh;						
		u16 BasePtPEgSh;					
		u16 BaseNtPEgSh;					
		u16 WavePtPEgSh;					
		u16 WaveNtPEgSh;					
		u16 CopPEgSh;						
		u16 IronPEgSh;							
//-------------	
		u16 PtPEgSha;						
		u16 NtPEgSha;						
		
		u16 PtQEgSha;						
		u16 NtQEgSha;						
		
		u16 Qd1QEgSha;						
		u16 Qd2QEgSha;						
		u16 Qd3QEgSha;						
		u16 Qd4QEgSha;						
		
		u16 PtPsEgSha;						
		u16 NtPsEgSha;						
		
		u16 AssEgSha;						
		u16 BasePtPEgSha;					
		u16 BaseNtPEgSha;					
		u16 WavePtPEgSha;					
		u16 WaveNtPEgSha;						
		u16 CopPEgSha;						
		u16 IronPEgSha;							
//-------------	
		u16 PtPEgShb;						
		u16 NtPEgShb;						
		
		u16 PtQEgShb;						
		u16 NtQEgShb;						
		
		u16 Qd1QEgShb;						
		u16 Qd2QEgShb;						
		u16 Qd3QEgShb;						
		u16 Qd4QEgShb;						
		
		u16 PtPsEgShb;						
		u16 NtPsEgShb;						
		
		u16 AssEgShb;						
		u16 BasePtPEgShb;					
		u16 BaseNtPEgShb;					
		u16 WavePtPEgShb;					
		u16 WaveNtPEgShb;						
		u16 CopPEgShb;						
		u16 IronPEgShb;							
//-------------	
		u16 PtPEgShc;						
		u16 NtPEgShc;						
		
		u16 PtQEgShc;						
		u16 NtQEgShc;						
		
		u16 Qd1QEgShc;						
		u16 Qd2QEgShc;						
		u16 Qd3QEgShc;						
		u16 Qd4QEgShc;						
		
		u16 PtPsEgShc;						
		u16 NtPsEgShc;						
		
		u16 AssEgShc;						
		u16 BasePtPEgShc;					
		u16 BaseNtPEgShc;					
		u16 WavePtPEgShc;					
		u16 WaveNtPEgShc;						
		u16 CopPEgShc;						
		u16 IronPEgShc;	
    } PubDataCrc; 		
 	
    struct sDl645EgPriData_TypeDef 
    {        
        u32 PieceEg[20];
        u16	Crc;
    } PriData;
    
	struct sDl645EgPubPara_TypeDef  
	{
        u8 		PExpression;       	
		u8 		PtQExpression;      
		u8 		NtQExpression;     
        u8      InvalidByte1;
        u16  	PPlsDeltaE;   		
        u16  	QPlsDeltaE;  		
        u16  	PsPlsDeltaE;   		
        
        u8   	FractionE;      	
 		u8      InvalidByte2;
 		u16		Crc;
 		 		
 		u8		fMinute;
 		u8      InvalidByte3;	
 	} PubPara;
}sDl645Eg_TypeDef;	//863BYTE	

typedef struct
{
	u8 	Head1;
	u8 	Addr[6];
	u8 	Head2;
	
//	u8 Ctl;
	u8	AFN : 5;
	u8	Persist : 1;	
	u8	Ack : 1;
	u8	Dir : 1;	
	
	u8 Len;
	
	struct	{
		u8	DI0;
		u8	DI1;
		u8	DI2;
		u8	DI3;		
		u8	Dat[MAX_COMPACK_SIZE - 16];
	} UDat;
			
	u8	Cs;
	u8	Tail;

} sFrmDl645B_TypeDef;	//DL645-2007协议帧结构

void fnRN8302_Init(void)
{
	Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
	fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
	
	Dl645RN8302DataComm.ucTemp8 = 0xA2; // 切换到EMM模式
	fnRN8302_Write(WMSW,Dl645RN8302DataComm.ucTempBuf,1);
	
	Dl645RN8302DataComm.ucTemp8 = 0xfa; // 写使能位
	fnRN8302_Write(0x0182,Dl645RN8302DataComm.ucTempBuf,1);
	SystemDelay(10);
	Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
	fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
	
	Dl645RN8302DataComm.ucTemp8 = 0xA2; // 切换到EMM模式
	fnRN8302_Write(WMSW,Dl645RN8302DataComm.ucTempBuf,1);
	
	Dl645RN8302DataComm.ucTempBuf[0] = 0x77; // 计量控制位
	Dl645RN8302DataComm.ucTempBuf[1] = 0x77;
	Dl645RN8302DataComm.ucTempBuf[2] = 0x77;
	fnRN8302_Write(0x0162,Dl645RN8302DataComm.ucTempBuf,3);
	
	Dl645RN8302DataComm.wTemp16 = Dl645FirmPara.HFConst1;
	fnRN8302_Write(HFCONST1,Dl645RN8302DataComm.ucTempBuf,2);
	
	// 电压增益初始化
	fnRN8302_Write(GSUA,(u8 *)&Dl645FirmPara.VGain[0],2);
	fnRN8302_Write(GSUB,(u8 *)&Dl645FirmPara.VGain[1],2);
	fnRN8302_Write(GSUC,(u8 *)&Dl645FirmPara.VGain[2],2);
	
	// 电流增益初始化
	fnRN8302_Write(GSIA,(u8 *)&Dl645FirmPara.IGain[0],2);
	fnRN8302_Write(GSIB,(u8 *)&Dl645FirmPara.IGain[1],2);
	fnRN8302_Write(GSIC,(u8 *)&Dl645FirmPara.IGain[2],2);
	fnRN8302_Write(GSIN,(u8 *)&Dl645FirmPara.IGain[3],2);
	
	// 通道相位校正
	fnRN8302_Write(PRTH1L,(u8 *)&Dl645FirmPara.PRth[0],2);
	fnRN8302_Write(PRTH1H,(u8 *)&Dl645FirmPara.PRth[1],2);
	fnRN8302_Write(PRTH2L,(u8 *)&Dl645FirmPara.PRth[2],2);
	fnRN8302_Write(PRTH2H,(u8 *)&Dl645FirmPara.PRth[3],2);	// 通道相位分段参数
	
	
	// 通道相位校正
	fnRN8302_Write(PHSIA,(u8 *)&Dl645FirmPara.PHSI[0],3);
	fnRN8302_Write(PHSIB,(u8 *)&Dl645FirmPara.PHSI[1],3);
	fnRN8302_Write(PHSIC,(u8 *)&Dl645FirmPara.PHSI[2],3);
	
	fnRN8302_Write(PA_PHS,(u8 *)&Dl645FirmPara.P_PHS[0],2);
	fnRN8302_Write(PB_PHS,(u8 *)&Dl645FirmPara.P_PHS[1],2);
	fnRN8302_Write(PC_PHS,(u8 *)&Dl645FirmPara.P_PHS[2],2);
	
	fnRN8302_Write(GPA,(u8 *)&Dl645FirmPara.PGain[0],2);
	fnRN8302_Write(GPB,(u8 *)&Dl645FirmPara.PGain[1],2);
	fnRN8302_Write(GPC,(u8 *)&Dl645FirmPara.PGain[2],2);
	
	fnRN8302_Write(GQA,(u8 *)&Dl645FirmPara.PGain[0],2);
	fnRN8302_Write(GQB,(u8 *)&Dl645FirmPara.PGain[1],2);
	fnRN8302_Write(GQC,(u8 *)&Dl645FirmPara.PGain[2],2);
	
	fnRN8302_Write(GSA,(u8 *)&Dl645FirmPara.PGain[0],2);
	fnRN8302_Write(GSB,(u8 *)&Dl645FirmPara.PGain[1],2);
	fnRN8302_Write(GSC,(u8 *)&Dl645FirmPara.PGain[2],2);
	
	// 通道功率OFFSET校正
	fnRN8302_Write(PA_OS,(u8 *)&Dl645FirmPara.UI_Offset[0],2);
	fnRN8302_Write(PB_OS,(u8 *)&Dl645FirmPara.UI_Offset[1],2);
	fnRN8302_Write(PC_OS,(u8 *)&Dl645FirmPara.UI_Offset[2],2);
	
	fnRN8302_Write(IStart_PS,(u8 *)&Dl645FirmPara.NOLOAD,2);
	fnRN8302_Write(ZXOT,(u8 *)&Dl645FirmPara.ZEROSTAR,2);
	
	Dl645RN8302DataComm.ucTemp8 = 0x42; 
	fnRN8302_Write(0x0184,Dl645RN8302DataComm.ucTempBuf,1);
	Dl645RN8302DataComm.ucTemp8 = 0;   
	fnRN8302_Write(0x0185,Dl645RN8302DataComm.ucTempBuf,1);
	Dl645RN8302DataComm.ucTemp8 = 0; 
	fnRN8302_Write(0x0184,Dl645RN8302DataComm.ucTempBuf,1);
	
	Dl645RN8302DataComm.ucTempBuf[0] = 0x10; 
	Dl645RN8302DataComm.ucTempBuf[1] = 0x32;
	Dl645RN8302DataComm.ucTempBuf[2] = 0x07;
	fnRN8302_Write(CFCFG,Dl645RN8302DataComm.ucTempBuf,3);
	Dl645RN8302DataComm.ucTempBuf[0] = 0x40; // 计量控制位 默认值 400000
	Dl645RN8302DataComm.ucTempBuf[1] = 0x00;
	Dl645RN8302DataComm.ucTempBuf[2] = 0x00;
	fnRN8302_Write(0x0161,Dl645RN8302DataComm.ucTempBuf,3);
	
	
	Dl645RN8302DataComm.ucTemp8 = 0x10; // 清空采样数据缓存区
	fnRN8302_Write(0x0163,Dl645RN8302DataComm.ucTempBuf,1);
	
	Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
	fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
	Dl645FirmPara.FractionP=0x04;
	Dl645FirmPara.FractionI=0x04;
	Dl645FirmPara.FractionU=0x02;
}

void fnDl645Front_Exec(void)
{
	//读电压电流
    for(i=0;i<4;i++)
	{
		Dl645FrontTmp.UI[i] = 0 ;
	    fnRN8302_Read( 0x000b+i , (u8 *)&Dl645FrontTmp.UI[i] , 4 ) ;
	    
	}
	for(i=0;i<3;i++)
	{
		Dl645FrontTmp.UI[i+4] = 0 ;
	    fnRN8302_Read( 0x0007+i , (u8 *)&Dl645FrontTmp.UI[i+4] , 4 ) ;
	    
	}
    
    //读功率
	for(i=0;i<12;i++)
	{
		Dl645FrontTmp.Pw[i] = 0 ;
		fnRN8302_Read(0x0014+i,(u8 *)&Dl645FrontTmp.Pw[i],4);
        if((Dl645FrontTmp.Pw[i]<30)&&(Dl645FrontTmp.Pw[i]>-30))
        Dl645FrontTmp.Pw[i]=0;
	}
    

    //读功率方向
    fnRN8302_Read(PQSign,(u8 *)&Dl645FrontTmp.PDirect,2);
    
    //读电压电流角度值
    for(i=0;i<5;i++)
	{
		Dl645FrontTmp.Angle[i] = 0 ;
		fnRN8302_Read(YUB+i,(u8 *)&Dl645FrontTmp.Angle[i],3);
		Dl645Front.PubData.Angle[i] = fnHexToBcd_u16((u16)(((float)Dl645FrontTmp.Angle[i]/16777216)*3600));
	}
    
    //功率计算
    for(j=0;j<3;j++)
	{
		
		for(i=0;i<3;i++)
		{
			Dl645Front.PubData.Pw[j*4+i+1]=fnDFConver_Hex32ToDF09((s32)((Dl645FrontTmp.Pw[j*4+i])/((Dl645FirmPara.PRmsConst))));//计算｛Pa Pb Pc｝、｛Qa Qb Qc｝、｛Sa Sb Sc｝
		}
	}
	Tempw = (s32)((Dl645FrontTmp.Pw[3])/((Dl645FirmPara.PRmsConst))) ;
	Dl645Front.PubData.Pw[0]=fnDFConver_Hex32ToDF09(Tempw);
	Tempq = (s32)((Dl645FrontTmp.Pw[7])/((Dl645FirmPara.PRmsConst))) ;
	Dl645Front.PubData.Pw[4]=fnDFConver_Hex32ToDF09(Tempq);
	// 计算总视在功率
	Tempws = (s32)sqrt((float)Tempw*(float)Tempw+(float)Tempq*(float)Tempq);
	Dl645Front.PubData.Pw[8] = fnDFConver_Hex32ToDF09(Tempws);//计算P、Q
	
	//功率因数计算
	//总功率因数
	Dl645Front.PubData.Pf[0]=fnDFConver_Bcd16To16((s16)((1000*(double)(Tempw))/(Tempws)));
	
	//电压电流计算
	for(i=0;i<3;i++)
	{
		Dl645Front.PubData.U[i]=0x7fff&(fnDFConver_Bcd16To16((s16)(Dl645FrontTmp.UI[i+4]/(10*(Dl645FirmPara.VRmsConst))))); //电压
	}
    
    
    TempIa = (s32)(Dl645FrontTmp.UI[0]/(Dl645FirmPara.IRmsConst));
    TempIb = (s32)(Dl645FrontTmp.UI[1]/(Dl645FirmPara.IRmsConst));
    TempIc = (s32)(Dl645FrontTmp.UI[2]/(Dl645FirmPara.IRmsConst));
    Dl645Front.PubData.I[0] = fnDFConver_Bcd32To32(TempIa);
    Dl645Front.PubData.I[1] = fnDFConver_Bcd32To32(TempIb);
    Dl645Front.PubData.I[2] = fnDFConver_Bcd32To32(TempIc);
    Dl645Front.PubData.I[3] = fnDFConver_Bcd32To32((s32)(Dl645FrontTmp.UI[3]/(Dl645FirmPara.IRmsConst)));

	if(Dl645FrontTmp.PDirect&0x0001) Dl645Front.PubData.I[0] |= 0x80000000;
	if(Dl645FrontTmp.PDirect&0x0002) Dl645Front.PubData.I[1] |= 0x80000000;
	if(Dl645FrontTmp.PDirect&0x0004) Dl645Front.PubData.I[2] |= 0x80000000;    

    
	//频率计算
	if((Dl645FrontTmp.Frequency>1638400)||(Dl645FrontTmp.Frequency<1092266))  //when 50HZ is 5120 ;+- 20% frequence is 6144 and 4096
		Dl645FrontTmp.Frequency=1310720;
	Dl645Front.PubData.Frequency=fnHexToBcd_u16((u16)(65536000/(Dl645FrontTmp.Frequency/100)));
	
	//转换功率方向
	Dl645Front.PubData.PDirect = ( (Dl645FrontTmp.PDirect << 1) & 0xee) | ( (Dl645FrontTmp.PDirect >> 3) & 0x11);

	
	if(Dl645Bkgrd.PriPara.WireMode==0x01)   //三相三线
	{
		Dl645Front.PubData.Angle[3]=0;
		Dl645Front.PubData.Angle[6]=0;
	}
	
	
	//从RN8302读出的即为脉冲数//{P,Q,Ps},{Pa,Qa,Psa},{Pb,Qb,Psb},{Pc,Qc,Psc}{Fp,Fq}{Fpa,Fqa}{Fpb,Fqb}{Fpc,Fqc}
    /*读电能寄存器*/
	for(i=0;i<3;i++)
	{
		Dl645FrontTmp.Pulse[(i+1)*3] = 0;
		fnRN8302_Read(EPA+i,(u8 *)&Dl645FrontTmp.Pulse[(i+1)*3],3);   //有功
		Dl645FrontTmp.Pulse[(i+1)*3+1] = 0;
		fnRN8302_Read(EQA+i,(u8 *)&Dl645FrontTmp.Pulse[(i+1)*3+1],3);  //无功
		Dl645FrontTmp.Pulse[(i+1)*3+2] = 0;
		fnRN8302_Read(ESA+i,(u8 *)&Dl645FrontTmp.Pulse[(i+1)*3+2],3);   //视在有功
		Dl645FrontTmp.Pulse[14+i*2] =0;
		fnRN8302_Read(FEPA+i,(u8 *)&Dl645FrontTmp.Pulse[14+i*2],3);      //基波有功
		Dl645FrontTmp.Pulse[15+i*2] = 0;
		fnRN8302_Read(FEQA+i,(u8 *)&Dl645FrontTmp.Pulse[15+i*2],3);      //基波无功
	}
	Dl645FrontTmp.Pulse[0] = 0;
	fnRN8302_Read(EPT,(u8 *)&Dl645FrontTmp.Pulse[0],3);   		//总有功
	Dl645FrontTmp.Pulse[1] = 0;
	fnRN8302_Read(EQT,(u8 *)&Dl645FrontTmp.Pulse[1],3);   		//总无功
	Dl645FrontTmp.Pulse[2] = 0;
	fnRN8302_Read(EST,(u8 *)&Dl645FrontTmp.Pulse[2],3);   		//总视在
	Dl645FrontTmp.Pulse[12] =0;
	fnRN8302_Read(FEPT,(u8 *)&Dl645FrontTmp.Pulse[12],3);      //总基波有功
	Dl645FrontTmp.Pulse[13] =0;
	fnRN8302_Read(FEQT,(u8 *)&Dl645FrontTmp.Pulse[13],3);      //总基波无功
	

    for(i=0;i<20;i++)//电能高频脉冲转换成低频脉冲
	{
		Dl645FrontTmp.Pulse_Eg[i]+=Dl645FrontTmp.Pulse[i];
    }
}

void fnDl645Energy_Exec(void)
{
	u8 Ep[20];
	if( !(Dl645FrontTmp.Pulse_Eg[0] | Dl645FrontTmp.Pulse_Eg[1] | Dl645FrontTmp.Pulse_Eg[2] | Dl645FrontTmp.Pulse_Eg[3] |
		Dl645FrontTmp.Pulse_Eg[4] | Dl645FrontTmp.Pulse_Eg[5] | Dl645FrontTmp.Pulse_Eg[6] | Dl645FrontTmp.Pulse_Eg[7] |
		Dl645FrontTmp.Pulse_Eg[8] | Dl645FrontTmp.Pulse_Eg[9] | Dl645FrontTmp.Pulse_Eg[10] | Dl645FrontTmp.Pulse_Eg[11] |
		Dl645Eg.PubPara.fMinute) ) 
	{
		return;
	}
	//Dl645Eg.PubPara.PPlsDeltaE为脉冲数，如脉冲数为800，因电能小数为2位，故此值为8
	for(i = 0 ; i < 12 ; i++)
	{
		Ep[i] = 0;
		
		if(Dl645FrontTmp.Pulse_Eg[i])
		{
			Dl645Eg.PriData.PieceEg[i] += Dl645FrontTmp.Pulse_Eg[i];
			Dl645FrontTmp.Pulse_Eg[i] = 0;
			
			switch(i % 3)
			{
			case 0:
				if(Dl645Eg.PubPara.PPlsDeltaE == 0) break;				
				while (Dl645Eg.PriData.PieceEg[i] >= Dl645Eg.PubPara.PPlsDeltaE)//需按电能小数位数进行计算
		  	    {
		  	    	Ep[i]++;
		  	    	Dl645Eg.PriData.PieceEg[i] -= Dl645Eg.PubPara.PPlsDeltaE;
		  	    }
				break;
			case 1:
				if(Dl645Eg.PubPara.QPlsDeltaE == 0) break;
				while (Dl645Eg.PriData.PieceEg[i] >= Dl645Eg.PubPara.QPlsDeltaE)//需按电能小数位数进行计算
		  	    {
		  	    	Ep[i]++;
		  	    	Dl645Eg.PriData.PieceEg[i] -= Dl645Eg.PubPara.QPlsDeltaE;
		  	    }
				break;
			default:
				if(Dl645Eg.PubPara.PsPlsDeltaE == 0) break;
				while (Dl645Eg.PriData.PieceEg[i] >= Dl645Eg.PubPara.PsPlsDeltaE)//需按电能小数位数进行计算
		  	    {
		  	    	Ep[i]++;
		  	    	Dl645Eg.PriData.PieceEg[i] -= Dl645Eg.PubPara.PsPlsDeltaE;
		  	    }
				break;	
			}
		}	
	}
	
	for(i = 0 ; i < 8 ; i++)
	{
		Ep[i+12] = 0;
		
		if(Dl645FrontTmp.Pulse_Eg[i+12])
		{
			Dl645Eg.PriData.PieceEg[i+12] += Dl645FrontTmp.Pulse_Eg[i+12];
			Dl645FrontTmp.Pulse_Eg[i+12] = 0;
			
			switch((i+12) % 2)
			{
			case 0:
				if(Dl645Eg.PubPara.PPlsDeltaE == 0) break;				
				while (Dl645Eg.PriData.PieceEg[i+12] >= Dl645Eg.PubPara.PPlsDeltaE)//需按电能小数位数进行计算
		  	    {
		  	    	Ep[i+12]++;
		  	    	Dl645Eg.PriData.PieceEg[i+12] -= Dl645Eg.PubPara.PPlsDeltaE;
		  	    }
				break;
			default :
				if(Dl645Eg.PubPara.QPlsDeltaE == 0) break;
				while (Dl645Eg.PriData.PieceEg[i+12] >= Dl645Eg.PubPara.QPlsDeltaE)//需按电能小数位数进行计算
		  	    {
		  	    	Ep[i+12]++;
		  	    	Dl645Eg.PriData.PieceEg[i+12] -= Dl645Eg.PubPara.QPlsDeltaE;
		  	    }
				break;
			}
		}	
	}
}


	// RxFrm为通讯数据指针，指到07协议的第1个68数据，为sFrmDl645B_TypeDef类型指针
	//校表进行增益校准和相位校准，可选择进行相位分段校准
	// 校表部分程序
	case 0x00f81000:										//电流电压功率初校,HFCONST等参数设置
		if(RxFrm->Len != (12+48)) return(DL645B_ERRINFO_DATA);
		
		memcpy(&tempconst , &(RxFrm->UDat.Dat[8]) , 2); 
		memcpy(&temphfconst , &(RxFrm->UDat.Dat[10]) , 2);
		memcpy(&tempub , &(RxFrm->UDat.Dat[12]) , 2);
		memcpy(&tempus , &(RxFrm->UDat.Dat[14]) , 4);
		memcpy(&tempib , &(RxFrm->UDat.Dat[18]) , 2);
		memcpy(&tempis , &(RxFrm->UDat.Dat[20]) , 4);
		
		
		Dl645FirmPara.ConstE=tempconst*10L;
		Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		Dl645FirmPara.HFConst1 = temphfconst;
		fnRN8302_Write(HFCONST1,(u8 *)&Dl645FirmPara.HFConst1,2);
		fnDl645File_Write(Dl645FileId_FirmPara,26,(u8 *)&Dl645FirmPara.HFConst1,2);
		
		for(i=0;i<3;i++)  //
		{
			memcpy(&temppw[0] , &(RxFrm->UDat.Dat[32+4*i]) , 4);
			temppw[1] = tempus/tempub;
			temppw[1] = (temppw[1] *temppw[0])/10;
			fnRN8302_Read( 0x0007+ i , (u8 *)&temppw[0] , 4 ) ;
			ADErr=((float)temppw[0]-(float)temppw[1])/temppw[1];
			ADErr=(-ADErr/(1+ADErr));
			if(ADErr>0) Dl645FirmPara.VGain[i]=(u16)(ADErr*32768);
			else Dl645FirmPara.VGain[i] = (u16)(65535 + ADErr*32768);
			fnRN8302_Write(GSUA+i,(u8 *)&Dl645FirmPara.VGain[i],2);
			fnRN8302_Read( GSUA+i , Dl645RN8302DataComm.ucTempBuf , 2 ) ;
		}
		fnDl645File_Write(Dl645FileId_FirmPara,42,(u8 *)&Dl645FirmPara.VGain[0],6);
		
		for(i=0;i<3;i++)   //电流增益校正
		{
			memcpy(&temppw[0] , &(RxFrm->UDat.Dat[44+4*i]) , 4);
			temppw[1] = tempis/tempib;
			temppw[1] = (temppw[1] *temppw[0])/10;
			fnRN8302_Read( 0x000B+ i , (u8 *)&temppw[0] , 4 ) ;
			ADErr=((float)temppw[0]-(float)temppw[1])/temppw[1];
			ADErr=((-ADErr)/(1+ADErr));
			if(ADErr>0) Dl645FirmPara.IGain[i]=(u16)(ADErr*32768);
			else Dl645FirmPara.IGain[i] = (u16)(65535 + ADErr*32768);
			fnRN8302_Write(GSIA+i,(u8 *)&Dl645FirmPara.IGain[i],2);
		}
		fnDl645File_Write(Dl645FileId_FirmPara,48,(u8 *)&Dl645FirmPara.IGain[0],8);
		
		
		tempconst = fnHexToBcd_u16(tempconst) ;
		ConstE.Dat0 = (((u8)(tempconst & 0x000f))<<4)&0xf0;
		ConstE.Dat1 = (tempconst & 0x0ff0)>>4;
		ConstE.Dat2 = (u8)((tempconst & 0xf000)>>12);
		
		
		fnDl645File_Write(Dl645FileId_HighPara , Dl645FileItemInfoOffAddr_HighPara_PConstE , (u8 *)&ConstE , 3); 
		fnDl645File_Write(Dl645FileId_HighPara , Dl645FileItemInfoOffAddr_HighPara_QConstE , (u8 *)&ConstE , 3);
		
		i=Dl645FirmPara.FractionI;
		B=1;
		while(i--)
		{
			B*=10;
		}
		B = B/1000;
		Dl645FirmPara.IRmsConst=((float)tempis/((float)tempib*B));
		fnDl645File_Write(Dl645FileId_FirmPara,10,(u8 *)&Dl645FirmPara.IRmsConst,4);
		
		i=Dl645FirmPara.FractionU;
		B=1;
		while(i--)
		{
			B*=10;
		}
		B = B/100;
		Dl645FirmPara.VRmsConst=((float)tempus/(tempub*B));
		fnDl645File_Write(Dl645FileId_FirmPara,30,(u8 *)&Dl645FirmPara.VRmsConst,4);
		
		Dl645FirmPara.NOLOAD = (u16)((tempis*0.0003)/16);
		Dl645FirmPara.ZEROSTAR=(u16)((tempis*0.005)/4096);
		fnRN8302_Write(IStart_PS,(u8 *)&Dl645FirmPara.NOLOAD,2);
		fnRN8302_Write(ZXOT,(u8 *)&Dl645FirmPara.ZEROSTAR,2);
		fnDl645File_Write(Dl645FileId_FirmPara,18,(u8 *)&Dl645FirmPara.NOLOAD,4);
		
		// 功率系数计算
		Dl645FirmPara.PRmsConst=(u32)((float)tempus*(float)tempis/((float)tempub*(float)tempib*838.8608));
		fnDl645File_Write(Dl645FileId_FirmPara,36,(u8 *)&Dl645FirmPara.PRmsConst,4);
		
		memcpy(&tempub , &(RxFrm->UDat.Dat[24]) , 2);
		memcpy(&tempib , &(RxFrm->UDat.Dat[26]) , 2);
		
		Dl645FirmPara.PRth[0] = tempub;
		Dl645FirmPara.PRth[1] = tempib;
		
		fnRN8302_Write(PRTH1L,(u8 *)&Dl645FirmPara.PRth[0],2);
		fnRN8302_Write(PRTH1H,(u8 *)&Dl645FirmPara.PRth[1],2);
		
		memcpy(&tempub , &(RxFrm->UDat.Dat[28]) , 2);
		memcpy(&tempib , &(RxFrm->UDat.Dat[30]) , 2);
		
		Dl645FirmPara.PRth[2] = tempub;
		Dl645FirmPara.PRth[3] = tempib;
		
		fnRN8302_Write(PRTH2L,(u8 *)&Dl645FirmPara.PRth[2],2);
		fnRN8302_Write(PRTH2H,(u8 *)&Dl645FirmPara.PRth[3],2);
		fnDl645File_Write(Dl645FileId_FirmPara,70,(u8 *)&Dl645FirmPara.PRth[0],8);
		
		Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);	
		Dl645FirmPara.ChkSum1 = 0;
		SystemDelay(10);
		fnRN8302_Read( CheckSum1 , (u8 *)&Dl645FirmPara.ChkSum1 , 3 ) ;
		fnDl645File_Write(Dl645FileId_FirmPara,22,(u8 *)&Dl645FirmPara.ChkSum1,3);
		break;
	case 0x00f81100:			//功率相位校准
		if((RxFrm->Len != (12+18))&&(RxFrm->Len != (12+20))&&(RxFrm->Len != (12+5))) return(DL645B_ERRINFO_DATA);
		if(RxFrm->Len == (12+5))
		{
        	memcpy(&tempis , &(RxFrm->UDat.Dat[9]) , 4);
			Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
			fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
			if(tempis > 0x7fffffff) ADErr=-(float)(0xffffffff - tempis)/100000;
        	else ADErr=(float)tempis/100000;
			ADErr = (asin(-ADErr/1.732))*57.29578/0.017578;				// 弧度转成角度
			if(Dl645FirmPara.PRth[0]==0 && Dl645FirmPara.PRth[1]==0 && Dl645FirmPara.PRth[2]==0 && Dl645FirmPara.PRth[3]==0)
			{
				if(ADErr == 0) Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = 0x808080;
				else if(ADErr>0)  Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = 0x808080 - (u8)ADErr;
				else {ADErr = -ADErr;Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = 0x808080 + (u8)ADErr;}
			}
			else
			{
				if(ADErr == 0) Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = (Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]]&0x00ff00ff)|0x8000;
				else if(ADErr>0)  Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] - (((u32)((u8)ADErr)<<8)&0x0000ff00);
				else {ADErr = -ADErr;Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] + (((u32)((u8)ADErr)<<8)&0x0000ff00);}
			}
			fnRN8302_Write(PHSIA+RxFrm->UDat.Dat[8],(u8 *)&Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]],3);
		}
		else
		{
			memcpy(&tempis , &(RxFrm->UDat.Dat[8]) , 4);
			memcpy(&tempus , &(RxFrm->UDat.Dat[12]) , 4);
			
			Dl645FirmPara.PRmsConst=((float)tempus*100/(tempis/10));
			
			fnDl645File_Write(Dl645FileId_FirmPara,36,(u8 *)&Dl645FirmPara.PRmsConst,4);
			
			Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
			fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
			if((RxFrm->Len == (12+18)))
			{
				tempangle[0] =0;
				
				for(i=0;i<2;i++)
				{
					fnRN8302_Read( YUB+i , Dl645RN8302DataComm.ucTempBuf , 3 ) ;
					temppw[0] =Dl645RN8302DataComm.lTemp32 & 0x00ffffff;
					tempangle[i+1] = (u16)(((float)temppw[0]/16777216)*36000);
				}
				
				for(i=0;i<3;i++)
				{
					memcpy(&tempangle[i+3] , &(RxFrm->UDat.Dat[20+2*i]) , 2);
					fnRN8302_Read( YIA+i , Dl645RN8302DataComm.ucTempBuf , 3 ) ;
					temppw[0] =Dl645RN8302DataComm.lTemp32 & 0x00ffffff; 
					ADErr = ((((float)temppw[0]/16777216)*36000-(float)(tempangle[i+3]+tempangle[i]))/1.7578);
					if(Dl645FirmPara.PRth[0]==0 && Dl645FirmPara.PRth[1]==0 && Dl645FirmPara.PRth[2]==0 && Dl645FirmPara.PRth[3]==0)
					{
						if(ADErr>0)  Dl645FirmPara.PHSI[i] = 0x808080 - (u8)ADErr;
						else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = 0x808080 + (u8)ADErr;}
					}
					else
					{
						if(ADErr>0)  Dl645FirmPara.PHSI[i] = Dl645FirmPara.PHSI[i] - (((u32)((u8)ADErr)<<8)&0x0000ff00);
						else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = Dl645FirmPara.PHSI[i] + (((u32)((u8)ADErr)<<8)&0x0000ff00);}
					}
					fnRN8302_Write(PHSIA+i,(u8 *)&Dl645FirmPara.PHSI[i],3);
				}
			}
			else 
			{
				for(i=0;i<3;i++)
				{
					fnRN8302_Read( 0x0014+i , Dl645RN8302DataComm.ucTempBuf , 4 ) ;
					temppw[0] =Dl645RN8302DataComm.lTemp32;
					
					memcpy(&temppw[1] , &(RxFrm->UDat.Dat[16 + 4*i]) , 4);
					temppw[1] = (u32)(((float)tempus/(float)tempis)*(float)temppw[1]);
					ADErr=((float)temppw[0]-(float)temppw[1])/(float)temppw[1];
					ADErr = (asin(-ADErr/1.732))*57.29578/0.017578;				// 弧度转成角度
					if(Dl645FirmPara.PRth[0]==0 && Dl645FirmPara.PRth[1]==0 && Dl645FirmPara.PRth[2]==0 && Dl645FirmPara.PRth[3]==0)
					{
						if(ADErr>0)  Dl645FirmPara.PHSI[i] = 0x808080 - (u8)ADErr;
						else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = 0x808080 + (u8)ADErr;}
					}
					else
					{
						if(ADErr>0)  Dl645FirmPara.PHSI[i] = Dl645FirmPara.PHSI[i] - (((u32)((u8)ADErr)<<8)&0x0000ff00);
						else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = Dl645FirmPara.PHSI[i] + (((u32)((u8)ADErr)<<8)&0x0000ff00);}
					}
					fnRN8302_Write(PHSIA+i,(u8 *)&Dl645FirmPara.PHSI[i],3);
				}
			}
		}
		fnDl645File_Write(Dl645FileId_FirmPara,56,(u8 *)&Dl645FirmPara.PHSU[0],14);
		
		Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		Dl645FirmPara.ChkSum1 = 0;
		SystemDelay(10);
		fnRN8302_Read( CheckSum1 , (u8 *)&Dl645FirmPara.ChkSum1 , 3 ) ;
		fnDl645File_Write(Dl645FileId_FirmPara,22,(u8 *)&Dl645FirmPara.ChkSum1,3);
		break;
	case 0x00f81102:			//功率相位校准分段1
		if((RxFrm->Len != (12+10))&&(RxFrm->Len != (12+20))&&(RxFrm->Len != (12+5))) return(DL645B_ERRINFO_DATA);
		if(Dl645FirmPara.PRth[0]==0 && Dl645FirmPara.PRth[1]==0 && Dl645FirmPara.PRth[2]==0 && Dl645FirmPara.PRth[3]==0) return(DL645B_ERRINFO_DATA);
		Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		if(RxFrm->Len == (12+10))
		{
			tempangle[0] =0;
			for(i=0;i<2;i++)
			{
				fnRN8302_Read( YUB+i , Dl645RN8302DataComm.ucTempBuf , 3 ) ;
				temppw[0] =Dl645RN8302DataComm.lTemp32 & 0x00ffffff;
				tempangle[i+1] = (u16)(((float)temppw[0]/16777216)*36000);
			}
			
			for(i=0;i<3;i++)
			{
				memcpy(&tempangle[i+3] , &(RxFrm->UDat.Dat[12+2*i]) , 2);
				fnRN8302_Read( YIA+i , Dl645RN8302DataComm.ucTempBuf , 3 ) ;
				//fnRN8302_Read( YIA+i , (u8 *)&temppw[0] , 3 ) ;
				temppw[0] =Dl645RN8302DataComm.lTemp32 & 0x00ffffff;
				ADErr = ((((float)temppw[0]/16777216)*36000-(tempangle[i+3]+tempangle[i]))/1.7578);
				if(ADErr>0)  Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0xffff00)|0x000080) - (u8)ADErr;
				else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0xffff00)|0x000080) + (u8)ADErr;}
				fnRN8302_Write(PHSIA+i,(u8 *)&Dl645FirmPara.PHSI[i],3);
			}
		}
		else if(RxFrm->Len == (12+20))
		{
			memcpy(&tempis , &(RxFrm->UDat.Dat[8]) , 4);
			memcpy(&tempus , &(RxFrm->UDat.Dat[12]) , 4);
			
			for(i=0;i<3;i++)
			{
				fnRN8302_Read( 0x0014+i , Dl645RN8302DataComm.ucTempBuf , 4 ) ;
				temppw[0] =Dl645RN8302DataComm.lTemp32;
				
				memcpy(&temppw[1] , &(RxFrm->UDat.Dat[16 + 4*i]) , 4);
				temppw[1] = (u32)(((float)tempus/(float)tempis)*(float)temppw[1]);
				ADErr=((float)temppw[0]-(float)temppw[1])/(float)temppw[1];
				ADErr = (asin(-ADErr/1.732))*57.29578/0.017578;				// 弧度转成角度
				if(ADErr>0)  Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0xffff00)|0x000080) - (u8)ADErr;
				else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0xffff00)|0x000080) + (u8)ADErr;}
				fnRN8302_Write(PHSIA+i,(u8 *)&Dl645FirmPara.PHSI[i],3);
			}
		}
		else
		{
        	memcpy(&tempis , &(RxFrm->UDat.Dat[9]) , 4);
			Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
			fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
			if(tempis > 0x7fffffff) ADErr=-(float)(0xffffffff - tempis)/100000;
        	else ADErr=(float)tempis/100000;
			ADErr = (asin(-ADErr/1.732))*57.29578/0.017578;				// 弧度转成角度
			if(ADErr>0)  Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = ((Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]]&0xffff00)|0x000080) - (u8)ADErr;
			else {ADErr = -ADErr;Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = ((Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]]&0xffff00)|0x000080) + (u8)ADErr;}
			fnRN8302_Write(PHSIA+RxFrm->UDat.Dat[8],(u8 *)&Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]],3);
		}
		fnDl645File_Write(Dl645FileId_FirmPara,56,(u8 *)&Dl645FirmPara.PHSU[0],14);
		Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		Dl645FirmPara.ChkSum1 = 0;
		SystemDelay(10);
		fnRN8302_Read( CheckSum1 , (u8 *)&Dl645FirmPara.ChkSum1 , 3 ) ;
		fnDl645File_Write(Dl645FileId_FirmPara,22,(u8 *)&Dl645FirmPara.ChkSum1,3);
		break;
	case 0x00f81103:
		if((RxFrm->Len != (12+10))&&(RxFrm->Len != (12+20))&&(RxFrm->Len != (12+5))) return(DL645B_ERRINFO_DATA);
		if(Dl645FirmPara.PRth[0]==0 && Dl645FirmPara.PRth[1]==0 && Dl645FirmPara.PRth[2]==0 && Dl645FirmPara.PRth[3]==0) return(DL645B_ERRINFO_DATA);
		Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		if(RxFrm->Len == (12+10))
		{	
			tempangle[0] =0;
			for(i=0;i<2;i++)
			{
				fnRN8302_Read( YUB+i , Dl645RN8302DataComm.ucTempBuf , 3 ) ;
				temppw[0] =Dl645RN8302DataComm.lTemp32 & 0x00ffffff;
				tempangle[i+1] = (u16)(((float)temppw[0]/16777216)*36000);
			}
			for(i=0;i<3;i++)
			{
				memcpy(&tempangle[i+3] , &(RxFrm->UDat.Dat[12+2*i]) , 2);
				fnRN8302_Read( YIA+i , Dl645RN8302DataComm.ucTempBuf , 3 ) ;
				temppw[0] =Dl645RN8302DataComm.lTemp32 & 0x00ffffff;
				ADErr = ((((float)temppw[0]/16777216)*36000-(tempangle[i+3]+tempangle[i]))/1.7578);
				if(ADErr>0)  Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0x00ffff)|0x800000) - (((u32)((u8)ADErr)*65536)&0x00ff0000);
				else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0x00ffff)|0x800000) + (((u32)((u8)ADErr)*65536)&0x00ff0000);}
				fnRN8302_Write(PHSIA+i,(u8 *)&Dl645FirmPara.PHSI[i],3);
			}
		}
		else if((RxFrm->Len == (12+20)))
		{
			memcpy(&tempis , &(RxFrm->UDat.Dat[8]) , 4);
			memcpy(&tempus , &(RxFrm->UDat.Dat[12]) , 4);
			
			for(i=0;i<3;i++)
			{
				fnRN8302_Read( 0x0014+i , Dl645RN8302DataComm.ucTempBuf , 4 ) ;
				temppw[0] =Dl645RN8302DataComm.lTemp32;
				
				memcpy(&temppw[1] , &(RxFrm->UDat.Dat[16 + 4*i]) , 4);
				temppw[1] = (u32)(((float)tempus/(float)tempis)*(float)temppw[1]);
				ADErr=((float)temppw[0]-(float)temppw[1])/(float)temppw[1];
				ADErr = (asin(-ADErr/1.732))*57.29578/0.017578;				// 弧度转成角度
				if(ADErr>0)  Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0x00ffff)|0x800000) - (((u32)((u8)ADErr)*65536)&0x00ff0000);
				else {ADErr = -ADErr;Dl645FirmPara.PHSI[i] = ((Dl645FirmPara.PHSI[i]&0x00ffff)|0x800000) + (((u32)((u8)ADErr)*65536)&0x00ff0000);}
				fnRN8302_Write(PHSIA+i,(u8 *)&Dl645FirmPara.PHSI[i],3);
			}
		}
		else
		{
			memcpy(&tempis , &(RxFrm->UDat.Dat[9]) , 4);
			Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
			fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
			if(tempis > 0x7fffffff) ADErr=-(float)(0xffffffff - tempis)/100000;
        	else ADErr=(float)tempis/100000;
			ADErr = (asin(-ADErr/1.732))*57.29578/0.017578;				// 弧度转成角度
			if(ADErr>0)  Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = ((Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]]&0x00ffff)|0x800000) - (((u32)((u8)ADErr)*65536)&0x00ff0000);
			else {ADErr = -ADErr;Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]] = ((Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]]&0x00ffff)|0x800000) + (((u32)((u8)ADErr)*65536)&0x00ff0000);}
			fnRN8302_Write(PHSIA+RxFrm->UDat.Dat[8],(u8 *)&Dl645FirmPara.PHSI[RxFrm->UDat.Dat[8]],3);
		}
		fnDl645File_Write(Dl645FileId_FirmPara,56,(u8 *)&Dl645FirmPara.PHSU[0],14);
		
		Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		Dl645FirmPara.ChkSum1 = 0;
		SystemDelay(10);
		fnRN8302_Read( CheckSum1 , (u8 *)&Dl645FirmPara.ChkSum1 , 3 ) ;
		fnDl645File_Write(Dl645FileId_FirmPara,22,(u8 *)&Dl645FirmPara.ChkSum1,3);
		break;
	case 0x00f81501:		// 误差校正功率增益
		if(RxFrm->Len != (12+5)) return(DL645B_ERRINFO_DATA);
		if(RxFrm->UDat.Dat[8]>0x03) return(DL645B_ERRINFO_DATA);
		memcpy(&tempis , &(RxFrm->UDat.Dat[9]) , 4);
		Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
        if(tempis > 0x7fffffff) ADErr=-(float)(0xffffffff - tempis)/100000;
        else ADErr=(float)tempis/100000;
		ADErr=((-ADErr)/(1+ADErr));
		if(ADErr>=0) Dl645FirmPara.PGain[RxFrm->UDat.Dat[8]]=(u16)(ADErr*32768);
		else Dl645FirmPara.PGain[RxFrm->UDat.Dat[8]] = (u16)(65535 + ADErr*32768);
		fnRN8302_Write(GPA+RxFrm->UDat.Dat[8],(u8 *)&Dl645FirmPara.PGain[RxFrm->UDat.Dat[8]],2);
		fnRN8302_Write(GQA+RxFrm->UDat.Dat[8],(u8 *)&Dl645FirmPara.PGain[RxFrm->UDat.Dat[8]],2);
		fnRN8302_Write(GSA+RxFrm->UDat.Dat[8],(u8 *)&Dl645FirmPara.PGain[RxFrm->UDat.Dat[8]],2);
		Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		fnDl645File_Write(Dl645FileId_FirmPara,98,(u8 *)&Dl645FirmPara.PGain[0],6);
		Dl645FirmPara.ChkSum1 = 0;
		SystemDelay(10);
		fnRN8302_Read( CheckSum1 , (u8 *)&Dl645FirmPara.ChkSum1 , 3 ) ;
		fnDl645File_Write(Dl645FileId_FirmPara,22,(u8 *)&Dl645FirmPara.ChkSum1,3);
        break;
	case 0x00f81600:
		if((RxFrm->Len != (12 + 1)) && (RxFrm->UDat.Dat[8] != 0x5A)) return(DL645B_ERRINFO_DATA);
		Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		for(i=0;i<3;i++)
		{
			Dl645FirmPara.VGain[i]=0;
			fnRN8302_Write(GSUA+i,(u8 *)&Dl645FirmPara.VGain[i],2);
			Dl645FirmPara.IGain[i]=0;		
			fnRN8302_Write(GSIA+i,(u8 *)&Dl645FirmPara.IGain[i],2);								
		}
		for(i=0;i<2;i++)
		{
			Dl645FirmPara.PHSU[i]=0x80;
			fnRN8302_Write(PHSUB+i,(u8 *)&Dl645FirmPara.PHSU[i],1);
		}
		for(i=0;i<3;i++)
		{
			Dl645FirmPara.PHSI[i]=0x808080;
			fnRN8302_Write(PHSUB+i,(u8 *)&Dl645FirmPara.PHSI[i],3);
		}
		for(i=0;i<3;i++)
		{
			Dl645FirmPara.P_PHS[i]=0;
			fnRN8302_Write(PA_PHS+i,(u8 *)&Dl645FirmPara.P_PHS[i],2);
		}
		for(i=0;i<3;i++)
		{
			Dl645FirmPara.P_PHS[i]=0;
			fnRN8302_Write(PA_PHS+i,(u8 *)&Dl645FirmPara.P_PHS[i],2);
		}
		for(i=0;i<3;i++)
		{
			Dl645FirmPara.PGain[i]=0;
			fnRN8302_Write(GPA+i,(u8 *)&Dl645FirmPara.PGain[i],2);
			fnRN8302_Write(GQA+i,(u8 *)&Dl645FirmPara.PGain[i],2);
			fnRN8302_Write(GSA+i,(u8 *)&Dl645FirmPara.PGain[i],2);
		}
		Dl645FirmPara.PRth[0] = 0x0;
		Dl645FirmPara.PRth[1] = 0x0;
		Dl645FirmPara.PRth[2] = 0x0;
		Dl645FirmPara.PRth[3] = 0x0;
		fnRN8302_Write(PRTH1L,(u8 *)&Dl645FirmPara.PRth[0],2);
		fnRN8302_Write(PRTH1H,(u8 *)&Dl645FirmPara.PRth[1],2);
		fnRN8302_Write(PRTH2L,(u8 *)&Dl645FirmPara.PRth[2],2);
		fnRN8302_Write(PRTH2H,(u8 *)&Dl645FirmPara.PRth[3],2);
		
		Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		fnDl645File_Write(Dl645FileId_FirmPara,42,(u8 *)&Dl645FirmPara.VGain[0],6);
		fnDl645File_Write(Dl645FileId_FirmPara,48,(u8 *)&Dl645FirmPara.IGain[0],8);
		fnDl645File_Write(Dl645FileId_FirmPara,56,(u8 *)&Dl645FirmPara.PHSU[0],14);
		fnDl645File_Write(Dl645FileId_FirmPara,70,(u8 *)&Dl645FirmPara.PRth[0],8);
		fnDl645File_Write(Dl645FileId_FirmPara,92,(u8 *)&Dl645FirmPara.P_PHS[0],12);
		fnDl645File_Write(Dl645FileId_FirmPara,98,(u8 *)&Dl645FirmPara.PGain[0],6);
		Dl645FirmPara.ChkSum1 = 0;
		SystemDelay(10);
		fnRN8302_Read( CheckSum1 , (u8 *)&Dl645FirmPara.ChkSum1 , 3 ) ;
		fnDl645File_Write(Dl645FileId_FirmPara,22,(u8 *)&Dl645FirmPara.ChkSum1,3);
		break; 
				
//低层函数
ErrorStatus fnRN8302_Write(u16 wReg,u8 *pBuf,u8 DatLen)
{
	u8 i,temp,chksum,Repeat;
	ErrorStatus	err = SUCCESS;
	if( (DatLen == 0) || (DatLen > 4) ) return(ERROR);
	for( Repeat =3; Repeat != 0 ; Repeat--)		
	{
		PinWrite_ADCS(0);
		NOP();
		temp = (u8)(wReg & 0x00ff);
		chksum = temp;
		fnSpi2_WriteByte( temp); //first write hight Addr
		temp = (((u8)(wReg >> 4)) & 0xf0) + 0x80;
		chksum += temp;
		fnSpi2_WriteByte( temp);
		
		for(i = DatLen; i > 0;i-- )
		{
			fnSpi2_WriteByte(pBuf[i-1]);  // first write high data
			chksum +=pBuf[i-1];
		}
		chksum = ~chksum;
		fnSpi2_WriteByte(chksum);
//		PinWrite_ADCS(1);
//		NOP();
//读WData寄存器检查-----------------------
//		PinWrite_ADCS(0);	
//		NOP();
		
		fnSpi2_WriteByte( 0x8D); 
		chksum = 0x8D;
		temp =  ( (u8)(0x018D >> 4) & 0xf0);			
		fnSpi2_WriteByte( temp );
		chksum += temp;
		for(i = 3 ; i > 0 ; i--)
		{
			temp = fnSpi2_ReadByte(); 
			if(DatLen >= i)
			{					
				if(temp != pBuf[i - 1]) 
				{
					err = ERROR;
					break;	
				}	
			}
			chksum += temp;
		}
		if(err == SUCCESS) 
        {
	        chksum = ~chksum;
	        temp = fnSpi2_ReadByte();   
			if(temp != chksum)  err = ERROR; 
       	}
		
		PinWrite_ADCS(1);
		if(err == SUCCESS) break;
		fnDelay2us();
		fnDelay2us();
	}
	PinWrite_ADSDO(1);
	PinWrite_ADSCLK(1);
	return(err);	
}

//////////////////////////////////////////////////////////////////////////

ErrorStatus fnRN8302_Read(u16 wReg,u8 *pBuf,u8 DatLen)
{
	u8 i,temp,chksum,Repeat;
	ErrorStatus	err = SUCCESS;
	if(DatLen == 0) return(ERROR);
	for( Repeat=3; Repeat != 0 ; Repeat--)		
	{
		PinWrite_ADCS(0);
		fnDelay2us();
		temp = (u8)(wReg & 0x00ff);
		chksum = temp;
		fnSpi2_WriteByte( temp); //first write hight Addr
		temp = ((u8)(wReg >> 4))& 0xf0;
		chksum += temp;
		fnSpi2_WriteByte( temp);
	    
		for(i = DatLen; i > 0;i--)
		{
			pBuf[i-1] = fnSpi2_ReadByte(); 
			chksum += pBuf[i-1];
		}
		chksum = ~chksum;
		if(fnSpi2_ReadByte()!=chksum)  err = ERROR;
//		PinWrite_ADCS(1);
//---读RData寄存器检查------------------------------------
		if(err != SUCCESS) continue;
/*		
		fnSpi2_WriteByte( 0x8c); 
		chksum = 0x8c;
		temp =  ( (u8)(0x018C >> 4) & 0xf0);			
		fnSpi2_WriteByte( temp );
		chksum += temp;
		for(i = 4 ; i > 0 ; i--)
		{
			temp = fnSpi2_ReadByte(); 
			if(DatLen >= i)
			{					
				if(temp != pBuf[i - 1]) 
				{
					err = ERROR;
					break;	
				}	
			}
			chksum += temp;
		}
		if(err == SUCCESS) 
    {
      chksum = ~chksum;
      temp = fnSpi2_ReadByte();   
			if(temp != chksum)  err = ERROR; 
   	}
		
		PinWrite_ADCS(1);*/
		if(err == SUCCESS) break;	
			
		fnDelay2us();
		fnDelay2us();
	}
	PinWrite_ADCS(1);
	PinWrite_ADSDO(1);
	PinWrite_ADSCLK(1);
	return(err);
}

void fnSpi2_WriteByte(u8 Dat)
{
	u8 i;
	
	for(i=0;i<8;i++)
	{
		PinWrite_ADSCLK(1);
		PinWrite_ADSCLK(1);
		fnDelay2us();
		fnDelay2us();
		if(Dat & 0x80)
		{
			PinWrite_ADSDO(1);
			PinWrite_ADSDO(1);
			fnDelay2us();
			fnDelay2us();
			fnDelay2us();
		}
		else
		{
			PinWrite_ADSDO(0);
			PinWrite_ADSDO(0);
			fnDelay2us();
			fnDelay2us();
			fnDelay2us();
		}
		PinWrite_ADSCLK(0);
		PinWrite_ADSCLK(0);
		fnDelay2us();
		fnDelay2us();
		fnDelay2us();
		Dat <<= 1;
	}
	PinWrite_ADSDO(1);
	PinWrite_ADSDO(1);
	fnDelay2us();
	fnDelay2us();
	fnDelay2us();
}




u8 fnSpi2_ReadByte(void)
{
    u8 i,B=0;
    for(i=0;i<8;i++)
    {
    	B<<=1;
    	PinWrite_ADSCLK(1);
		PinWrite_ADSCLK(1);
		fnDelay2us();
		fnDelay2us();
		fnDelay2us();
		fnDelay2us();
		PinWrite_ADSCLK(0);
		PinWrite_ADSCLK(0);
		fnDelay2us();
		fnDelay2us();
		fnDelay2us();
		fnDelay2us();
		if (PinRead_ADSDI()){B=B+0x1;}
		fnDelay2us();
		fnDelay2us();
		fnDelay2us();
    }
    return	B;
}
		


// 文件操作函数
ErrorStatus fnDl645File_Read(u8 FileId , u32 OffAddr , void *Dst , u32 Len)
{
	if(FileId >= Dl645FileId_Max) return(ERROR);
	if( (OffAddr + Len) > Dl645FileInfo[FileId].Len) return(ERROR);	
	return(fnMemory_Read(Dst , Dl645FileInfo[FileId].StartAddr + OffAddr , Len) );
}

ErrorStatus fnDl645File_Write(u8 FileId , u32 OffAddr , void *Src , u32 Len)
{
	if(FileId >= Dl645FileId_Max) return(ERROR);
	if( (OffAddr + Len) > Dl645FileInfo[FileId].Len) return(ERROR);	
	return(fnMemory_Write(Dl645FileInfo[FileId].StartAddr + OffAddr , Src , Len) );	
}

//存储器读取函数
ErrorStatus fnMemory_Read(u8 *Dst , u32 Src , u32 DatLen)
{
	ErrorStatus err;
	if( (FM24C_START_ADDR <= Src) && 
		( (Src + DatLen) < (FM24C_START_ADDR + FM24C_SIZE) ) )
	{		
		#if  !DL645SOFT_DEBUG	
		err = fnFM24CXX_Read(Dst , Src - FM24C_START_ADDR , DatLen , 3);
		if(err==ERROR) Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_INTERMEMORY;
		else Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_OK;
		#endif
	}	
	
	else if( (AT45DB_START_ADDR <= Src) && 
		( (Src + DatLen) < (AT45DB_START_ADDR + AT45DB_SIZE) ) )
	{	
		#if  !DL645SOFT_DEBUG
		err = fnAT45DBXX_Read(Dst , Src - AT45DB_START_ADDR , DatLen , 3);
		if(err==ERROR) Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_INTERMEMORY;
		else Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_OK;
		#endif
	}
	
	else
	{	
		err = ERROR;
	}
	
	return(err);
}

//存储器写入函数
ErrorStatus fnMemory_Write(u32 Dst , u8 *Src , u32 DatLen)
{
	ErrorStatus err;

	if( (FM24C_START_ADDR <= Dst) && 
		( (Dst + DatLen) < (FM24C_START_ADDR + FM24C_SIZE) ) )
	{		
		#if  !DL645SOFT_DEBUG	
		err = fnFM24CXX_Write(Dst- FM24C_START_ADDR , Src  , DatLen , 3);
		if(err==ERROR) Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_INTERMEMORY;
		else Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_OK;
		#endif
	}
	
	else if( (AT45DB_START_ADDR <= Dst) && 
		( (Dst + DatLen) < (AT45DB_START_ADDR + AT45DB_SIZE) ) )
	{		
		#if  !DL645SOFT_DEBUG
		err = fnAT45DBXX_Write( Dst - AT45DB_START_ADDR , Src , DatLen , 3);
		if(err==ERROR) Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_INTERMEMORY;
		else Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_OK;
		#endif
	}
	
	else
	{	
		err = ERROR;
		Dl645Bkgrd.PubData.EventErr[5]=BKGRD_EVENTERR_INTERMEMORY;
	}
	
	return(err);	
}
	
		
#define	EPA			0x0030
#define	EPB			0x0031
#define	EPC			0x0032
#define	EPT			0x0033
#define	PosEPA		0x0034
#define	PosEPB		0x0035
#define	PosEPC		0x0036
#define	PosEPT		0x0037
#define	NegEPA		0x0038
#define	NegEPB		0x0039
#define	NegEPC		0x003A
#define	NegEPT		0x003B

#define	EQA			0x003C
#define	EQB			0x003D
#define	EQC			0x003E
#define	EQT			0x003F
#define	PosEQA		0x0040
#define	PosEQB		0x0041
#define	PosEQC		0x0042
#define	PosEQT		0x0043
#define	NegEQA		0x0044
#define	NegEQB		0x0045
#define	NegEQC		0x0046
#define	NegEQT		0x0047

#define	ESA			0x0048
#define	ESB			0x0049
#define	ESC			0x004A
#define	EST			0x004B

#define YUA			0x0050
#define YUB			0x0051
#define YUC			0x0052
#define YIA			0x0053
#define YIB			0x0054
#define YIC			0x0055
#define YIN			0x0056

#define	FEPA		0x007A
#define	FEPB		0x007B
#define	FEPC		0x007C
#define	FEPT		0x007D
#define	PosFEPA		0x007E
#define	PosFEPB		0x007F
#define	PosFEPC		0x0080
#define	PosFEPT		0x0081
#define	NegFEPA		0x0082
#define	NegFEPB		0x0083
#define	NegFEPC		0x0084
#define	NegFEPT		0x0085

#define	FEQA		0x0086
#define	FEQB		0x0087
#define	FEQC		0x0088
#define	FEQT		0x0089
#define	PosFEQA		0x008A
#define	PosFEQB		0x008B
#define	PosFEQC		0x008C
#define	PosFEQT		0x008D
#define	NegFEQA		0x008E
#define	NegFEQB		0x008F
#define	NegFEQC		0x0090
#define	NegFEQT		0x0091

#define	FESA		0x0092
#define	FESB		0x0093
#define	FESC		0x0094
#define	FEST		0x0095

#define	HFCONST1	0x0100
#define	HFCONST2	0x0101
#define	IStart_PS	0x0102
#define	ZXOT		0x0105
#define	PRTH1L		0x0106
#define	PRTH1H		0x0107
#define	PRTH2L		0x0108
#define	PRTH2H		0x0109
#define	PHSUA		0x010C
#define	PHSUB		0x010D
#define	PHSUC		0x010E
#define	PHSIA		0x010F
#define	PHSIB		0x0110
#define	PHSIC		0x0111
#define	GSUA		0x0113
#define	GSUB		0x0114
#define	GSUC		0x0115
#define	GSIA		0x0116
#define	GSIB		0x0117
#define	GSIC		0x0118
#define	GSIN		0x0119
#define	DCOS_UA		0x011A
#define	DCOS_UB		0x011B
#define	DCOS_UC		0x011C
#define	GPA			0x0128
#define	GPB			0x0129
#define GPC			0x012A
#define	GQA			0x012B
#define	GQB			0x012C
#define	GQC			0x012D
#define	GSA			0x012E
#define	GSB			0x012F
#define	GSC			0x0130
#define	PA_PHS		0x0131
#define	PB_PHS		0x0132
#define	PC_PHS		0x0133
#define	PA_OS		0x0137
#define	PB_OS		0x0138
#define	PC_OS		0x0139

#define GFPA		0X0143
#define GFPB		0X0144
#define	GFPC		0X0145

#define	GFSA		0x0149
#define	GFSB		0x014A
#define	GFSC		0x014B

#define	CFCFG		0x0160
#define EMUCON      0x0162
#define	PQSign		0x0166
#define	CheckSum1	0x016A
#define	WMSW		0x0181
#define	DeviceID	0x018f