/**************************************************************************//**
 * @file     Dl645_Fft.c
 * @brief    
 *           
 * @version  V1.2
 * @date     2017/5/26
 *
 * @note
 * Copyright (C) REnergy Micro-Electronics Co.,LTD.
  *--------------------------Update History-------------------------------------
 *            V1.1 add source code of fnRN8302_ReadBuf
 * 2017/5/26  V1.2 correct the calculation of total harmonic ftemp_P
 *
 ******************************************************************************/
#define  _DL645_fft_GLOBALS
#include <App.h>

const float Fftcoefficient[50]=					// 谐波计算时补偿系数
{
	1.0005,
	1.0027,
	1.0054,
	1.0087,
	1.0129,
	1.0175,
	1.0229,
	1.029,
	1.0362,
	1.0438,
	1.0524,
	1.0618,
	1.0718,
	1.0827,
	1.0944,
	1.1075,
	1.121,
	1.136,
	1.1517,
	1.1685,
	1.1862,
	1.2051,
	1.2255,
	1.2475,
	1.27,
	1.2942,
	1.3199,
	1.3468,
	1.3758,
	1.407,
	1.4421,
	1.4714,
	1.5107,
	1.5483,
	1.5873,
	1.6312,
	1.6727,
	1.7199,
	1.77,
	1.8231,
	1.8797,
	1.9448,
	2.0074,
	2.074,
	2.1534,
	2.2126,
	2.2919,
	2.3762,
	2.467,
	2.5639,
};

void fnRN8302_ReadBuf(u16 wReg,u8 *pBuf)
{
	u8 i,j,temp,chksum;
	PinMode_ADCS(GPIO_MODE_OUT);
	PinMode_ADSCLK(GPIO_MODE_OUT);
	PinMode_ADSDO(GPIO_MODE_OUT);
	PinMode_ADSDI(GPIO_MODE_IN);
	ENABLE_RN8302();//PinWrite_ADCS(0);
	temp = (u8)(wReg & 0x00ff);
	chksum = temp;
	fnSpi2_WriteByte( temp); //first write hight Addr
	temp = ((u8)(wReg >> 4))& 0xf0;
	temp = temp | 0x0c ;			//BRUST,?áè?μ??·?a16
	chksum += temp;
	fnSpi2_WriteByte( temp);
    
	for(j=0;j< 16 ; j++)
	{
		for(i = 3; i > 0;i--)
		{
			pBuf[(i+j*3-1)] = fnSpi2_ReadByte(); 
			chksum += pBuf[i-1];
		}
	}
	if(fnSpi2_ReadByte()!=chksum)  
	{
		DISABLE_RN8302();//PinWrite_ADCS(1);
	}
	PinWrite_ADCS(1);
	PinWrite_ADCS(1);
	PinWrite_ADSDO(1);
	PinWrite_ADSCLK(1);
}

void fnDl645Fft_init(void)
{
	Harmonictemp.Channel=0x00;
	Harmonictemp.DataCount=0;
    Harmonictemp.StarFlag=0;
    Harmonictemp.ADSPIBusy = 0;
    memset(&Harmonictemp,0,sizeof(sADE_Instantaneous_TypeDef));
    memset(&HarmonicData,0,sizeof(sDl645FftHarmonicData_TypeDef));
}


void SampleDataModifyF(float *piRetValue)
{
	u8 i;
	u32	Temp;
   	for(i=0; i<NUM_FFT; i++)
   	{
	   	piRetValue[i] = 0;
	   	Temp = 0;
	   	memcpy((u8 *)&Temp , &(Harmonictemp.InstantaneousData[i*3]) , 3);
   		if(Temp &0x800000) 
   		{
   			Temp = (0xffffff - Temp) + 1;
   			piRetValue[i] = -(float)Temp/8388608;
   		}
   		else piRetValue[i] = (float)Temp/8388608;
   	}
}

void FFT(float dataR[NUM_FFT],float dataI[NUM_FFT])
{
	int x0,x1,x2,x3,x4,x5,x6,xx;
	int i,j,k,b,p,L;
	float TR,TI,temp;
	
	/********** following code invert sequence ************/
	for ( i=0;i<NUM_FFT;i++ )
	{
		x0=x1=x2=x3=x4=x5=x6=0;
		x0=i&0x01; x1=(i/2)&0x01; x2=(i/4)&0x01; x3=(i/8)&0x01;x4=(i/16)&0x01; x5=(i/32)&0x01; x6=(i/64)&0x01;
		xx=x0*64+x1*32+x2*16+x3*8+x4*4+x5*2+x6;
		dataI[xx]=dataR[i];
	}
	for ( i=0;i<NUM_FFT;i++ )
	{
		dataR[i]=dataI[i]; dataI[i]=0; 
	}

	/************** following code FFT *******************/
	for ( L=1;L<=7;L++ )
	{ /* for(1) */
		b=1; i=L-1;
		while ( i>0 ) 
		{
			b=b*2; i--;
		} /* b= 2^(L-1) */
		for ( j=0;j<=b-1;j++ ) /* for (2) */
		{
			p=1; i=7-L;
			while ( i>0 ) /* p=pow(2,7-L)*j; */
			{
				p=p*2; i--;
			}
			p=p*j;
			for ( k=j;k<128;k=k+2*b ) /* for (3) */
			{
				TR=dataR[k]; TI=dataI[k]; temp=dataR[k+b];
				dataR[k]=dataR[k]+dataR[k+b]*cos_tab[p]+dataI[k+b]*sin_tab[p];
				dataI[k]=dataI[k]-dataR[k+b]*sin_tab[p]+dataI[k+b]*cos_tab[p];
				dataR[k+b]=TR-dataR[k+b]*cos_tab[p]-dataI[k+b]*sin_tab[p];
				dataI[k+b]=TI+temp*sin_tab[p]-dataI[k+b]*cos_tab[p];
			} /* END for (3) */
		} /* END for (2) */
	} /* END for (1) */
} /* END FFT */


void InitForFFT()
{
	int i;
	
	for ( i=0;i<NUM_FFT;i++ )
	{
		sin_tab[i]=sin(PI*2*i/NUM_FFT);
		cos_tab[i]=cos(PI*2*i/NUM_FFT);
	}
}

// 比较各通道电压或电流数据是否大于一定值，如小于不进行此通道的谐波分析
u8 fnDl645Fft_DataCmp(u8 Channel)
{
	u8 Btemp=1;
	switch(Channel)
	{
		case 0x00:
			if(Dl645Bkgrd.PriPara.WireMode == 1) {if(Dl645Front.PubData.U[0]<0x0070) Btemp=0;}
			else 	{if(Dl645Front.PubData.U[0]<0x1540) Btemp=0;}
			break;
		case 0x01:
			if(Dl645Bkgrd.PriPara.WireMode == 1) {if(Dl645Front.PubData.U[1]<0x0070) Btemp=0;}
			else {if(Dl645Front.PubData.U[1]<0x1540) Btemp=0;}
			break;
		case 0x02:
			if(Dl645Bkgrd.PriPara.WireMode == 1) {if(Dl645Front.PubData.U[2]<0x0070) Btemp=0;}
			else {if(Dl645Front.PubData.U[2]<0x1540) Btemp=0;}
			break;
		case 0x03:
			if((Dl645Front.PubData.I[0]& 0x7fffffff) < 0x750) Btemp=0;
			break;
		case 0x04:
			if((Dl645Front.PubData.I[1]& 0x7fffffff) < 0x750) Btemp=0;
			break;
		case 0x05:
			if((Dl645Front.PubData.I[2]& 0x7fffffff) < 0x750) Btemp=0;
			break;
	}
	return(Btemp);
}
void fnDl645Fft_Harmonic_Exec(void)
{
	
	float ftemp_N,ftemp_P;
	float fBase;  //基波的平方
	s16 i;
		
	if((Harmonictemp.StarFlag==0) && (SysClock.Second == 0x15 || SysClock.Second == 0x45))	
	{
		Harmonictemp.StarFlag =1;
		memset(&HarmonicData,0,sizeof(sDl645FftHarmonicData_TypeDef));
		Harmonictemp.TimeOutStamp=SysStamp?SysStamp:(SysStamp-1);
		Dl645RN8302DataComm.ucTemp8 = 0xe5; // 写使能位
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);
		Dl645RN8302DataComm.ucTemp8 = 0xa0; // 启动采样值写缓存
		fnRN8302_Write(0x0163,Dl645RN8302DataComm.ucTempBuf,1);
		Dl645RN8302DataComm.ucTemp8 = 0xDC; // 写保护
		fnRN8302_Write(0x0180,Dl645RN8302DataComm.ucTempBuf,1);			
		Harmonictemp.Channel=0x00;
		Harmonictemp.ReadAdress = 0x0200;
		InitForFFT();
		return;
	}
	if(Harmonictemp.StarFlag == 0x01)
	{
		if(fnStamp_Through(Harmonictemp.TimeOutStamp)<25)	return;
		Harmonictemp.StarFlag = 0x02;
		fnRN8302_Read( 0x0163 , Dl645RN8302DataComm.ucTempBuf , 1 );
		if(Dl645RN8302DataComm.ucTempBuf[0] & 0x30)  return;
		for(i=0;i<7;i++)
		{
			Dl645FrontTmp.UI[i] = 0 ;
	    	fnRN8302_Read( 0x0058+i , (u8 *)&Dl645FrontTmp.UI[i] , 4 ) ;
		}
		for(i=0;i<3;i++)
		{
			HarmonicData.FU[i] = ((s16)(Dl645FrontTmp.UI[i]/(10*(Dl645FirmPara.VRmsConst))));
		}
		for(i=0;i<3;i++)
		{
			HarmonicData.FI[i] = (s32)(Dl645FrontTmp.UI[i+3]/(Dl645FirmPara.IRmsConst));
		}
		return;
	}
	if(Harmonictemp.StarFlag == 0x02)
	{
		for(i=0;i<8;i++)
		{
			fnRN8302_ReadBuf(((Harmonictemp.ReadAdress)+i*16),(u8 *)&Harmonictemp.InstantaneousData[i*48]);
		}
		
		// 采样完成后进行谐波计算
		SampleDataModifyF(&HarmonicData.iRealArray[0]);	//采样数据修正
		for(i=0;i<NUM_FFT;i++)  HarmonicData.iMageArray[i]=0;
		FFT(HarmonicData.iRealArray, HarmonicData.iMageArray);
		
		ftemp_N = (float)(((float)HarmonicData.iRealArray[1] *(float)HarmonicData.iRealArray[1]) + ((float)HarmonicData.iMageArray[1] * (float)HarmonicData.iMageArray[1]));
		ftemp_N = sqrt(ftemp_N);
		fBase	= ftemp_N;
		ftemp_P = 0;
		for(i=0;i<50;i++)
	    {
	    	ftemp_N = (float)(((float)HarmonicData.iRealArray[i+2] *(float)HarmonicData.iRealArray[i+2]) + ((float)HarmonicData.iMageArray[i+2] * (float)HarmonicData.iMageArray[i+2]));
	    	ftemp_N = sqrt(ftemp_N);
	    	ftemp_N = ( ftemp_N * 1000000*Fftcoefficient[i]) /fBase);
	    	ftemp_P += (ftemp_N * ftemp_N);
	    	if(Harmonictemp.Channel<0x03) 
	    	{	    		
	    		HarmonicData.HarmonicpercentU[Harmonictemp.Channel][i+1] = (fnHexToBcd_u32((u32)(ftemp_N)) ;	    		
	    	}
	    	else 
	    	{	    		
		    	HarmonicData.HarmonicpercentI[Harmonictemp.Channel-3][i+1] = (fnHexToBcd_u32((u32)ftemp_N)) ;		    	
	    	}
	    }
		ftemp_P = sqrt(ftemp_P);
		if(Harmonictemp.Channel<0x03)
		{			
			HarmonicData.HarmonicpercentU[Harmonictemp.Channel][0] = fnHexToBcd_u32((u32)(ftemp_P));
		}
		else
		{
			HarmonicData.HarmonicpercentI[Harmonictemp.Channel-3][0] = (fnHexToBcd_u32((u32)ftemp_P)) ;
		}		
		Harmonictemp.Channel++;
		Harmonictemp.ReadAdress+=128;
		for(i=Harmonictemp.Channel;i<6;i++)
		{
			if(fnDl645Fft_DataCmp(Harmonictemp.Channel)) break;
			Harmonictemp.Channel++;
			Harmonictemp.ReadAdress+=128;
		}
		if(Harmonictemp.Channel > 5 ) 
		{	
			Harmonictemp.StarFlag = 0;
			return;
		}

	}
	if(fnStamp_Through(Harmonictemp.TimeOutStamp)>8000)
	{
		Harmonictemp.StarFlag = 0;
	}
}
