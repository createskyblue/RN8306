void fnRN8302_HSDCMode_Init(void)
{
		sDataComm_TypeDef      EmuCommbuf;
	
		EmuCommbuf.ucTemp8 = RN8302_WREN_EN; // 写使能位
		fnRN8302_Write(RN8302_WREN,EmuCommbuf.ucTempBuf,1);
		
		EmuCommbuf.ucTempBuf[0] = 0xAA;
		EmuCommbuf.ucTempBuf[1] = RN8302_WREN_EN;   // WREN2寄存器使能
		EmuCommbuf.ucTempBuf[2] = 0;
		EmuCommbuf.ucTempBuf[3] = 0;
		fnRN8302_Write(RN8302_WREN2,EmuCommbuf.ucTempBuf,4);
	
		fnRN8302_Read(RN8302_SYSCFG, EmuCommbuf.ucTempBuf,4);
		EmuCommbuf.lTemp32 |= (0xEA000000|0x1000);
		fnRN8302_Write(RN8302_SYSCFG, EmuCommbuf.ucTempBuf,4);
	
		//复位MMU波形缓存DMA
		EmuCommbuf.ucTempBuf[0] = 0xE1;
		fnRN8302_Write(RN8302_SOFTRST,EmuCommbuf.ucTempBuf,1);
		//复位HSDC
		EmuCommbuf.ucTempBuf[0] = 0x4C;
		fnRN8302_Write(RN8302_SOFTRST,EmuCommbuf.ucTempBuf,1);
		
//		/*"Step3:配置HSDC的SPI  模式  "*/
//		EmuCommbuf.lTemp32 = 0x04; 
//		fnRN8302_Write(RN8306_HDSC_CTRL,EmuCommbuf.ucTempBuf,4);
		
		 /*"Step4:配置波形格式和EMMDMA模式  "*/
		/*"UA UB UC IA IB IC  固定256 "*/
		EmuCommbuf.ucTempBuf[0] = 0x80;//0x00; 
		fnRN8302_Write(RN8302_WSAVECON,EmuCommbuf.ucTempBuf,1);
		EmuCommbuf.lTemp32 = 0;//0x13F0;
		EmuCommbuf.ucTempBuf[0] = 0x04;
		fnRN8302_Write(RN8302_DMA_WAVECON,EmuCommbuf.ucTempBuf,4);
		EmuCommbuf.lTemp32 = 0xC00;		// 交叉缓存
		fnRN8302_Write(RN8306_DMA_WAVECON3,EmuCommbuf.ucTempBuf,4);
		
		/*"Step5:开启HSDC的SPI    "*/
		EmuCommbuf.ucTempBuf[0] = 0x0D;	// 配置HSDC 带校验  4.096Mhz
		EmuCommbuf.ucTempBuf[1] = 0x04;   		// 按字节反序---先发低字节  累加校验和
		EmuCommbuf.ucTempBuf[2] = 0;
		EmuCommbuf.ucTempBuf[3] = 0; 
		fnRN8302_Write(RN8306_HDSC_CTRL,EmuCommbuf.ucTempBuf,4);

		/*"Step6:开启EMMDMA模式    "*/
		EmuCommbuf.lTemp32 = 0xC00 | 0x01; //0x01; // 交叉缓存
		fnRN8302_Write(RN8306_DMA_WAVECON3,EmuCommbuf.ucTempBuf,4);

		EmuCommbuf.ucTempBuf[0] = 0x00;
		EmuCommbuf.ucTempBuf[1] = 0x07;   //EmuCommbuf.ucTempBuf[1] = 0x05;   
		EmuCommbuf.ucTempBuf[2] = 0;
		EmuCommbuf.ucTempBuf[3] = 0;
		fnRN8302_Write(RN8302_DMA_WAVECON2,EmuCommbuf.ucTempBuf,4);
		
//		// 使能半波有效值2计算
//		EmuCommbuf.ucTempBuf[0] = 0x01;
//		EmuCommbuf.ucTempBuf[1] = 0x00;    
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8302_EMUCFG3,EmuCommbuf.ucTempBuf,4);
//		// 半波有效值2配置
//		EmuCommbuf.ucTempBuf[0] = 0x7F;
//		EmuCommbuf.ucTempBuf[1] = 0x00;    
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8302_HWCNT,EmuCommbuf.ucTempBuf,4);
//		
//		// 使能半波有效值2更新---INT1中断
//		EmuCommbuf.ucTempBuf[0] = 0x00;
//		EmuCommbuf.ucTempBuf[1] = 0x00;    
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0x01;
//		fnRN8302_Write(RN8302_EMMIE2,EmuCommbuf.ucTempBuf,4);

		// 电压增益初始化x
		/*
		EmuCommbuf.ucTempBuf[0] = 0x00;
		EmuCommbuf.ucTempBuf[1] = 0x00;   
		EmuCommbuf.ucTempBuf[2] = 0;
		EmuCommbuf.ucTempBuf[3] = 0;
		fnRN8302_Write(RN8302_GSUA_WAVE, EmuCommbuf.ucTempBuf,2);
		fnRN8302_Write(RN8302_GSUB_WAVE, EmuCommbuf.ucTempBuf,2);
		fnRN8302_Write(RN8302_GSUC_WAVE, EmuCommbuf.ucTempBuf,2);
		
		// 电流增益初始化
		fnRN8302_Write(RN8302_GSIA_WAVE, EmuCommbuf.ucTempBuf,2);
		fnRN8302_Write(RN8302_GSIB_WAVE, EmuCommbuf.ucTempBuf,2);
		fnRN8302_Write(RN8302_GSIC_WAVE, EmuCommbuf.ucTempBuf,2);
		fnRN8302_Write(RN8302_GSIN_WAVE, EmuCommbuf.ucTempBuf,2);*/
		
//		fnRN8302_Write(RN8302_GSUA_WAVE,(u8 *)&MeasureFirmPara.GSU[0],2);
//		fnRN8302_Write(RN8302_GSUB_WAVE,(u8 *)&MeasureFirmPara.GSU[1],2);
//		fnRN8302_Write(RN8302_GSUC_WAVE,(u8 *)&MeasureFirmPara.GSU[2],2);
//		
//		// 电流增益初始化
//		fnRN8302_Write(RN8302_GSIA_WAVE,(u8 *)&MeasureFirmPara.GSI[0],2);
//		fnRN8302_Write(RN8302_GSIB_WAVE,(u8 *)&MeasureFirmPara.GSI[1],2);
//		fnRN8302_Write(RN8302_GSIC_WAVE,(u8 *)&MeasureFirmPara.GSI[2],2);
//		fnRN8302_Write(RN8302_GSIN_WAVE,(u8 *)&MeasureFirmPara.GSI[3],2);

//		EmuCommbuf.ucTemp8 = 0x10; // 清空采样数据缓存区
//		fnRN8302_Write(RN8302_WSAVECON,EmuCommbuf.ucTempBuf,1);
//						
//		EmuCommbuf.ucTempBuf[0] = 0;
//		EmuCommbuf.ucTempBuf[1] = 0x9A;   // 打开HSDC
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0xEA;
//		fnRN8302_Write(RN8302_SYSCFG,EmuCommbuf.ucTempBuf,4);
//		
//		fnRN8302_Read(RN8302_SYSCFG,EmuCommbuf.ucTempBuf,4);
//		
//		EmuCommbuf.ucTempBuf[0] = 0;	// 关闭波形缓存
//		EmuCommbuf.ucTempBuf[1] = 0;   
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8306_DMA_WAVECON3,EmuCommbuf.ucTempBuf,4);
//		
//		//复位MMU波形缓存DMA
//		EmuCommbuf.ucTempBuf[0] = 0xE1;
//		fnRN8302_Write(RN8302_SOFTRST,EmuCommbuf.ucTempBuf,1);
//		//复位HSDC
//		EmuCommbuf.ucTempBuf[0] = 0x4C;
//		fnRN8302_Write(RN8302_SOFTRST,EmuCommbuf.ucTempBuf,1);
//		//写DMA_WAVECON寄存器---128点
///*		EmuCommbuf.ucTempBuf[0] = 0x00;	
//		EmuCommbuf.ucTempBuf[1] = 0;   
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8302_DMA_WAVECON,EmuCommbuf.ucTempBuf,4);	*/


//		EmuCommbuf.ucTempBuf[0] = 0xF4;	
//		EmuCommbuf.ucTempBuf[1] = 0x09;   //256点同步采样，固定wavecnt
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8302_DMA_WAVECON,EmuCommbuf.ucTempBuf,4);	

///*
//		EmuCommbuf.ucTempBuf[0] = 0x90;	
//		EmuCommbuf.ucTempBuf[1] = 0x14;   //128点同步采样，固定cnt
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8302_DMA_WAVECON,EmuCommbuf.ucTempBuf,4);	
//*/

///*		EmuCommbuf.ucTempBuf[0] = 0x01;	//8K采样
//		EmuCommbuf.ucTempBuf[1] = 0x00;   
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8302_DMA_WAVECON,EmuCommbuf.ucTempBuf,4);*/
//		
//		//写WSAVECON寄存器
//		EmuCommbuf.ucTemp8 = 0x01;//0xAC;//0xA0;//0xA2; // 0x01
//		fnRN8302_Write(RN8302_WSAVECON,EmuCommbuf.ucTempBuf,1);
////		//写EMMIE2寄存器
////		EmuCommbuf.ucTempBuf[0] = 0;	// 
////		EmuCommbuf.ucTempBuf[1] = 0x0A;   
////		EmuCommbuf.ucTempBuf[2] = 0x10;
////		EmuCommbuf.ucTempBuf[3] = 0;
////		fnRN8302_Write(RN8302_EMMIE,EmuCommbuf.ucTempBuf,4);
////		//写EMMIF2寄存器
////		EmuCommbuf.ucTempBuf[0] = 0;	// 
////		EmuCommbuf.ucTempBuf[1] = 0x0A;   
////		EmuCommbuf.ucTempBuf[2] = 0x00;
////		EmuCommbuf.ucTempBuf[3] = 0;
////		fnRN8302_Write(RN8302_EMMIF,EmuCommbuf.ucTempBuf,4);
//		
//		EmuCommbuf.ucTempBuf[0] = 0x1D;	// 配置HSDC 带校验
//		#ifdef EnCrc
//		EmuCommbuf.ucTempBuf[0] = 0x1D;	// 配置HSDC
//		#endif
//		EmuCommbuf.ucTempBuf[1] = 0x04;   		// 按字节反序---先发低字节  累加校验和
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8306_HDSC_CTRL,EmuCommbuf.ucTempBuf,4);
//		
//		fnRN8302_Read(RN8306_HDSC_CTRL,EmuCommbuf.ucTempBuf,4);
//		
//		EmuCommbuf.ucTempBuf[0] = 0x00;//0x05;//0x01;//0x03;	
//		EmuCommbuf.ucTempBuf[1] = 0x00;   
//		EmuCommbuf.ucTempBuf[2] = 0;
//		EmuCommbuf.ucTempBuf[3] = 0;
//		fnRN8302_Write(RN8306_DMA_WAVECON3,EmuCommbuf.ucTempBuf,4);
//		
//		fnRN8302_Read( RN8302_WSAVECON , EmuCommbuf.ucTempBuf , 1 );
//		EmuCommbuf.ucTemp8 = ((EmuCommbuf.ucTemp8 & 0xCF)|0x20); //启动波形缓存
//		fnRN8302_Write(RN8302_WSAVECON,EmuCommbuf.ucTempBuf,1);	
	
		EmuCommbuf.ucTemp8 = RN8302_WREN_DIS; // 
		fnRN8302_Write(RN8302_WREN,EmuCommbuf.ucTempBuf,1);
		
		EmuCommbuf.ucTempBuf[0] = 0xAA;
		EmuCommbuf.ucTempBuf[1] = RN8302_WREN_DIS;   // 
		EmuCommbuf.ucTempBuf[2] = 0;
		EmuCommbuf.ucTempBuf[3] = 0;
		fnRN8302_Write(RN8302_WREN2,EmuCommbuf.ucTempBuf,4);

}