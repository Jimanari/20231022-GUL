/*********************************************************************************
Copyright(c) 2019 Analog Devices, Inc. All Rights Reserved.
This software is proprietary and confidential. By using this software you agree
to the terms of the associated Analog Devices License Agreement.
*********************************************************************************/

/*****************************************************************************
 * Audio_Loopback_TDM.c
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/platform.h>
#include <sys/adi_core.h>
#include "adi_initialize.h"
#include <services/int/adi_int.h>
#include <drivers/sport/adi_sport.h>
#include <services/spu/adi_spu.h>
#include <drivers/twi/adi_twi.h>
#include "ADAU_1962Common.h"
#include "ADAU_1979Common.h"
#include "math.h"
#include <SRU.h>
#include "Audio_Loopback_TDM.h"

float Audio_channel1[256];
float Audio_channel2[256];
float Audio_channel3[256];
float Audio_channel4[256];
float Audio_channel5[256];
float Audio_channel6[256];
float Audio_channel7[256];
float Audio_channel8[256];

/* SPORT buffers */
int int_SP0ABuffer1[COUNT*2];
int int_SP0ABuffer2[COUNT*2];
int int_SP0ABuffer4[COUNT];
int int_SP0ABuffer5[COUNT];

struct Config_Table
{
	short Reg_Add;
	char  Value;
};

char Config_read_DAC[28];
char Config_read_ADC[16];

/* Dev buffer for configuring ADC-DAC through TWI*/
static uint8_t devBuffer[BUFFER_SIZE];

extern void ConfigSoftSwitches_ADC_DAC(void);

extern void ConfigSoftSwitches_ADAU_Reset(void);

struct Config_Table Config_array_DAC[28] = {
		   	    {     ADAU1962_PDN_CTRL_1,		0x00},
		   	    {     ADAU1962_PDN_CTRL_2,	    0xff},
		   	    {     ADAU1962_PDN_CTRL_3,	    0x0f},
		   	    {     ADAU1962_DAC_CTRL0,		0x01},
		   	    {     ADAU1962_DAC_CTRL1,		0x01},
		   	    {     ADAU1962_DAC_CTRL2,		0x00},
		   	    {     ADAU1962_DAC_MUTE1,	    0x0},
		   	    {     ADAU1962_DAC_MUTE2,	    0x00},
		   	    {     ADAU1962_MSTR_VOL,		0x00},
			    {     ADAU1962_DAC1_VOL,	    0x00},
			    {     ADAU1962_DAC2_VOL,		0x00},
				{     ADAU1962_DAC3_VOL,		0x00},
				{     ADAU1962_DAC4_VOL,		0x00},
				{     ADAU1962_DAC5_VOL,		0x00},
				{     ADAU1962_DAC6_VOL,		0x00},
				{     ADAU1962_DAC7_VOL,		0x00},
				{     ADAU1962_DAC8_VOL,	    0x00},
				{     ADAU1962_DAC9_VOL,		0x00},
				{     ADAU1962_DAC10_VOL,		0x00},
				{     ADAU1962_DAC11_VOL,		0x00},
				{     ADAU1962_DAC12_VOL,		0x00},
				{     ADAU1962_PAD_STRGTH,		0x00},
				{     ADAU1962_DAC_PWR1,		0xaa},
				{     ADAU1962_DAC_PWR2,		0xaa},
				{     ADAU1962_DAC_PWR3,		0xaa},
				{     ADAU1962_PDN_CTRL_2,	    0x00},
				{     ADAU1962_PDN_CTRL_3,	    0x00},
				{     ADAU1962_DAC_CTRL0,		0x18}

};

struct Config_Table Config_array_ADC[16] = {

		 {ADAU1979_REG_BOOST			,	0x00},
		 {ADAU1979_REG_MICBIAS			,	0x00},
		 {ADAU1979_REG_BLOCK_POWER_SAI	,	0x30},
		 {ADAU1979_REG_SAI_CTRL0		,	0x1B},/*I2S 48kHz*/
		 {ADAU1979_REG_SAI_CTRL1		,	0x08},
		 {ADAU1979_REG_CMAP12			,	0x01},
		 {ADAU1979_REG_CMAP34			,	0x23},
		 {ADAU1979_REG_SAI_OVERTEMP		,	0xf0},
		 {ADAU1979_REG_POST_ADC_GAIN1 	,	0xA0},
		 {ADAU1979_REG_POST_ADC_GAIN2 	,	0xA0},
		 {ADAU1979_REG_POST_ADC_GAIN3 	,	0xA0},
		 {ADAU1979_REG_POST_ADC_GAIN4 	,	0xA0},
		 {ADAU1979_REG_ADC_CLIP			,	0x00},
		 {ADAU1979_REG_DC_HPF_CAL		,	0x00},
		 {ADAU1979_REG_BLOCK_POWER_SAI	,	0x3f},
		 {ADAU1979_REG_MISC_CONTROL		,	0x00}
};

/* Destination PDMA lists */
ADI_PDMA_DESC_LIST iDESC_LIST_1_SP4A;
ADI_PDMA_DESC_LIST iDESC_LIST_2_SP4A ;

/* Source PDMA lists */
ADI_PDMA_DESC_LIST iSRC_LIST_1_SP4B ;
ADI_PDMA_DESC_LIST iSRC_LIST_2_SP4B ;


/* Prepares descriptors for SPORT DMA */
static void PrepareDescriptors (void);

/* Count to track the number of callBacks for SPORT transfer */
volatile uint8_t CallbackCount = 0;

volatile uint32_t TestCallbackCount = 0;

/* Memory required for SPORT */
static uint8_t SPORTMemory4A[ADI_SPORT_MEMORY_SIZE];
static uint8_t SPORTMemory4B[ADI_SPORT_MEMORY_SIZE];

/* SPORT Handle */
static ADI_SPORT_HANDLE hSPORTDev4ATx;//TX
static ADI_SPORT_HANDLE hSPORTDev4BRx;//RX

/* Memory required for TWI */
uint8_t TwideviceMemory[ADI_TWI_MEMORY_SIZE];

/* TWI driver handle */
static ADI_TWI_HANDLE hTwiDevice;

/* SPU handle */
static ADI_SPU_HANDLE      ghSpu;

/* Memory required for the SPU operation */
uint8_t             SpuMemory[ADI_SPU_MEMORY_SIZE];

int delay;

static int ADAU_1962_Pllinit(void);
static int ADAU_1979_Pllinit(void);
void Switch_Configurator(void);
static int ADAU_1962_init(void);
static int ADAU_1979_init(void);
void SRU_Init(void);
static int Sport_Init(void);
static int Sport_Stop(void);
static int SPU_init(void);
static void PrepareDescriptors (void);
static int Init_TWI(void);
static int Stop_TWI(void);



static void SPORTCallback(
    void        *pAppHandle,
    uint32_t     nEvent,
    void        *pArg
)
{
	int i;
	int j;

	ADI_SPORT_RESULT eResult;
    /* CASEOF (event type) */
    switch (nEvent)
    {
        /* CASE (buffer processed) */
        case ADI_SPORT_EVENT_RX_BUFFER_PROCESSED:
        		TestCallbackCount += 1;
        		CallbackCount +=1;

        		if(CallbackCount==1)
        		{
        		   for(i=0,j=0;i<COUNT;i+=4,j+=8)
        		   {

        			   int_SP0ABuffer1[j]  =int_SP0ABuffer4[i];/*Copy ADC buffer to DAC buffer */

        			   int_SP0ABuffer1[j+4]=int_SP0ABuffer4[i];/*Copy ADC buffer to DAC buffer */

        			   int_SP0ABuffer1[j+1]=int_SP0ABuffer4[i+1];/*Copy ADC buffer to DAC buffer */

        			   int_SP0ABuffer1[j+5]=int_SP0ABuffer4[i+1];/*Copy ADC buffer to DAC buffer */

        			   int_SP0ABuffer1[j+2]=int_SP0ABuffer4[i+2];/*Copy ADC buffer to DAC buffer */

        			   int_SP0ABuffer1[j+6]=int_SP0ABuffer4[i+2];/*Copy ADC buffer to DAC buffer */

        			   int_SP0ABuffer1[j+3]=int_SP0ABuffer4[i+3];/*Copy ADC buffer to DAC buffer */

        			   int_SP0ABuffer1[j+7]=int_SP0ABuffer4[i+3];/*Copy ADC buffer to DAC buffer */

        		   }
        		}

        		if(CallbackCount==2)
        			{
        			   for(i=0,j=0;i<COUNT;i+=4,j+=8)
        			   {
        				   int_SP0ABuffer2[j]  =int_SP0ABuffer5[i];/*Copy ADC buffer to DAC buffer */

        				   int_SP0ABuffer2[j+4]=int_SP0ABuffer5[i];/*Copy ADC buffer to DAC buffer */

        				   int_SP0ABuffer2[j+1]=int_SP0ABuffer5[i+1];/*Copy ADC buffer to DAC buffer */

        				   int_SP0ABuffer2[j+5]=int_SP0ABuffer5[i+1];/*Copy ADC buffer to DAC buffer */

        				   int_SP0ABuffer2[j+2]=int_SP0ABuffer5[i+2];/*Copy ADC buffer to DAC buffer */

        				   int_SP0ABuffer2[j+6]=int_SP0ABuffer5[i+2];/*Copy ADC buffer to DAC buffer */

        				   int_SP0ABuffer2[j+3]=int_SP0ABuffer5[i+3];/*Copy ADC buffer to DAC buffer */

        				   int_SP0ABuffer2[j+7]=int_SP0ABuffer5[i+3];/*Copy ADC buffer to DAC buffer */

        			   }
        			   CallbackCount=0;
        			}

        		break;
        default:
        	 break;
    }
    /* return */
}

/*
 * Prepares descriptors for Memory DMA copy.
 *
 * Parameters
 *  None
 *
 * Returns
 *  None
 *
 */

static void PrepareDescriptors (void)
{

	iDESC_LIST_1_SP4A.pStartAddr	= (int *)int_SP0ABuffer1;
	iDESC_LIST_1_SP4A.Config		= ENUM_DMA_CFG_XCNT_INT ;
	iDESC_LIST_1_SP4A.XCount		= COUNT*2;
	iDESC_LIST_1_SP4A.XModify		= 4;
	iDESC_LIST_1_SP4A.YCount		= 0;
	iDESC_LIST_1_SP4A.YModify		= 0;
	iDESC_LIST_1_SP4A.pNxtDscp		= &iDESC_LIST_2_SP4A;

	iDESC_LIST_2_SP4A.pStartAddr	= (int *)int_SP0ABuffer2;
	iDESC_LIST_2_SP4A.Config		= ENUM_DMA_CFG_XCNT_INT ;
	iDESC_LIST_2_SP4A.XCount		= COUNT*2;
	iDESC_LIST_2_SP4A.XModify		= 4;
	iDESC_LIST_2_SP4A.YCount		= 0;
	iDESC_LIST_2_SP4A.YModify		= 0;
	iDESC_LIST_2_SP4A.pNxtDscp		= &iDESC_LIST_1_SP4A;

	iSRC_LIST_1_SP4B.pStartAddr		=(int *)int_SP0ABuffer4;
	iSRC_LIST_1_SP4B.Config			= ENUM_DMA_CFG_XCNT_INT ;
	iSRC_LIST_1_SP4B.XCount			= COUNT;
	iSRC_LIST_1_SP4B.XModify		= 4;
	iSRC_LIST_1_SP4B.YCount			= 0;
	iSRC_LIST_1_SP4B.YModify		= 0;
	iSRC_LIST_1_SP4B.pNxtDscp		= &iSRC_LIST_2_SP4B;

	iSRC_LIST_2_SP4B.pStartAddr		=(int *)int_SP0ABuffer5;
	iSRC_LIST_2_SP4B.Config			= ENUM_DMA_CFG_XCNT_INT;
	iSRC_LIST_2_SP4B.XCount			= COUNT;
	iSRC_LIST_2_SP4B.XModify		= 4;
	iSRC_LIST_2_SP4B.YCount			= 0;
	iSRC_LIST_2_SP4B.YModify		= 0;
	iSRC_LIST_2_SP4B.pNxtDscp		= &iSRC_LIST_1_SP4B;
}

static int Sport_Init()
{
    /* SPORT return code */
    ADI_SPORT_RESULT    eResult;

	/* Open the SPORT Device 4A */
	eResult = adi_sport_Open(SPORT_DEVICE_4A,ADI_HALF_SPORT_A,ADI_SPORT_DIR_TX, ADI_SPORT_MC_MODE, SPORTMemory4A,ADI_SPORT_MEMORY_SIZE,&hSPORTDev4ATx);
	CHECK_RESULT(eResult);
	/* Open the SPORT Device 4B*/
	eResult = adi_sport_Open(SPORT_DEVICE_4B,ADI_HALF_SPORT_B,ADI_SPORT_DIR_RX, ADI_SPORT_MC_MODE, SPORTMemory4B,ADI_SPORT_MEMORY_SIZE,&hSPORTDev4BRx);
	CHECK_RESULT(eResult);

	/* Configure the data,clock,frame sync and MCTL of SPORT Device 4A*/
	eResult = adi_sport_ConfigData(hSPORTDev4ATx,ADI_SPORT_DTYPE_SIGN_FILL,31,false,false,false);
	CHECK_RESULT(eResult);
	eResult = adi_sport_ConfigClock(hSPORTDev4ATx,32,false,false,false);
	CHECK_RESULT(eResult);
	eResult = adi_sport_ConfigFrameSync(hSPORTDev4ATx,31,false,false,false,true,false,false);
	CHECK_RESULT(eResult);
	eResult = adi_sport_ConfigMC(hSPORTDev4ATx,1u,7u,0u,true);
	CHECK_RESULT(eResult);
	eResult = adi_sport_SelectChannel(hSPORTDev4ATx,0u,7u);
	CHECK_RESULT(eResult);

	/* Configure the data,clock,frame sync and MCTL of SPORT Device 4B*/
	eResult = adi_sport_ConfigData(hSPORTDev4BRx,ADI_SPORT_DTYPE_SIGN_FILL,31,false,false,false);
	CHECK_RESULT(eResult);
	eResult = adi_sport_ConfigClock(hSPORTDev4BRx,32,false,false,false);
	CHECK_RESULT(eResult);
	eResult = adi_sport_ConfigFrameSync(hSPORTDev4BRx,31,false,false,false,true,false,false);
	CHECK_RESULT(eResult);
	eResult = adi_sport_ConfigMC(hSPORTDev4BRx,1u,7u,0u,true);
	CHECK_RESULT(eResult);
	eResult = adi_sport_SelectChannel(hSPORTDev4BRx,0u,3u);
	CHECK_RESULT(eResult);

	/* Register SPORT Callback function */
	eResult = adi_sport_RegisterCallback(hSPORTDev4BRx,SPORTCallback,NULL);
	CHECK_RESULT(eResult);

	/* Prepare descriptors */
	PrepareDescriptors();

	/* Submit the first buffer for Rx.  */
	eResult = adi_sport_DMATransfer(hSPORTDev4BRx,&iSRC_LIST_1_SP4B,(DMA_NUM_DESC),ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM);
	CHECK_RESULT(eResult);
	/* Submit the first buffer for Tx.  */
	eResult = adi_sport_DMATransfer(hSPORTDev4ATx,&iDESC_LIST_1_SP4A,(DMA_NUM_DESC),ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM);
	CHECK_RESULT(eResult);

	/*Enable the Sport Device 4B */
	eResult = adi_sport_Enable(hSPORTDev4BRx,true);
	CHECK_RESULT(eResult);
	/*Enable the Sport Device 4A */
	eResult = adi_sport_Enable(hSPORTDev4ATx,true);
	CHECK_RESULT(eResult);

	return eResult;

}

static int Sport_Stop(void)
{
    /* SPORT return code */
    ADI_SPORT_RESULT    eResult;

    /*Stop the DMA transfer of  Sport Device 4B */
	eResult = adi_sport_StopDMATransfer(hSPORTDev4BRx);
	CHECK_RESULT(eResult);
	/*Stop the DMA transfer of  Sport Device 4A */
	eResult = adi_sport_StopDMATransfer(hSPORTDev4ATx);
	CHECK_RESULT(eResult);
	/*Close Sport Device 4B */
	eResult = adi_sport_Close(hSPORTDev4BRx);
	CHECK_RESULT(eResult);
	/*Close Sport Device 4A */
	eResult = adi_sport_Close(hSPORTDev4ATx);
	CHECK_RESULT(eResult);

	return eResult;
}

/*
 * Prepares Switch configuration.
 *
 * Parameters
 *  None
 *
 * Returns
 *  None
 *
 */
void Switch_Configurator()
{
	int delay11=0xffff;


	/* Software Switch Configuration for Enabling ADC-DAC */
	ConfigSoftSwitches_ADC_DAC();

	while(delay11--)
	{
		asm("nop;");
	}

	/* Software Switch Configuration for Re-Setting ADC-DAC  */
	ConfigSoftSwitches_ADAU_Reset();


	/* wait for Codec to up */
	delay11=0xffff;
	while(delay11--)
	{
		asm("nop;");
	}
}



/*
 * Prepares SRU configuration.
 *
 * Parameters
 *  None
 *
 * Returns
 *  None
 *
 */
void SRU_Init()
{

	*pREG_PADS0_DAI0_IE = BITM_PADS_DAI0_IE_VALUE;
	*pREG_PADS0_DAI1_IE = BITM_PADS_DAI1_IE_VALUE;

    SRU2(LOW,DAI1_PBEN05_I);

    SRU2(DAI1_PB05_O,SPT4_ACLK_I); /*DAC clock to SPORT 4A*/
    SRU2(DAI1_PB05_O,SPT4_BCLK_I); /*DAC clock to SPORT 4B*/

    SRU2(DAI1_PB04_O,SPT4_AFS_I);  /*DAC FS to SPORT 4A*/
    SRU2(DAI1_PB04_O,SPT4_BFS_I);  /*DAC FS to SPORT 4B*/
    SRU2(LOW,DAI1_PBEN04_I);

    SRU2(SPT4_AD0_O,DAI1_PB01_I); /* SPORT 4A to DAC*/
    SRU2(HIGH,DAI1_PBEN01_I);

#if defined(__ADSP21566__)|| defined(__ADSP21567__) || defined(__ADSP21569__)
    SRU2(DAI1_PB05_O,DAI1_PB12_I);  /*DAC clock to ADC */
    SRU2(HIGH,DAI1_PBEN12_I);
#elif defined(__ADSP21562__)|| defined(__ADSP21563__) || defined(__ADSP21565__)
    SRU2(DAI1_PB05_O,DAI1_PB09_I);  /*DAC clock to ADC */
    SRU2(HIGH,DAI1_PBEN09_I);
#endif

    SRU2(DAI1_PB04_O,DAI1_PB20_I);  /*DAC FS to ADC */
    SRU2(HIGH,DAI1_PBEN20_I);

    SRU2(DAI1_PB06_O,SPT4_BD0_I);
    SRU2(LOW,DAI1_PBEN06_I);

}

/*
 * Prepares SPU configuration.
 *
 * Parameters
 *  None
 *
 * Returns
 *  None
 *
 */
int SPU_init(void)
{
    if(adi_spu_Init(0, SpuMemory, NULL, NULL, &ghSpu) != ADI_SPU_SUCCESS)
    {
    	REPORT_ERROR("Failed to initialize SPU service\n");
		return FAILED;
    }

    /* Make SPORT 0A to generate secure transactions */
    if(adi_spu_EnableMasterSecure(ghSpu, SPORT_4A_SPU, true) != ADI_SPU_SUCCESS)
    {
    	REPORT_ERROR("Failed to enable Master secure for SPORT0A\n");
		return FAILED;
    }

    /* Make SPORT 0B to generate secure transactions */
    if(adi_spu_EnableMasterSecure(ghSpu, SPORT_4B_SPU, true) != ADI_SPU_SUCCESS)
    {
    	REPORT_ERROR("Failed to enable Master secure for SPORT0B\n");
		return FAILED;
    }

    return SUCCESS;
}

void Write_TWI_8bit_Reg(unsigned char Reg_ID, unsigned char Tx_Data)
{
	devBuffer[0] = Reg_ID;
	devBuffer[1] = Tx_Data;
	adi_twi_Write(hTwiDevice, devBuffer, 2u, false);
}

unsigned char Read_TWI_8bit_Reg(unsigned char Reg_ID)
{
	ADI_TWI_RESULT eResult;
	unsigned char Rx_Data;

	/* write register address */
	devBuffer[0] = Reg_ID;
	eResult = adi_twi_Write(hTwiDevice, devBuffer, 1u, true);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI write failed 0x%08X\n", eResult);
	}

	/* read register value */
	eResult = adi_twi_Read(hTwiDevice, &Rx_Data, 1u, false);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Read failed 0x%08X\n", eResult);
	}

	return Rx_Data;
}

static int Init_TWI(void)
{

	ADI_TWI_RESULT eResult;

	eResult = adi_twi_Open(TWIDEVNUM, ADI_TWI_MASTER, &TwideviceMemory[0],
			   	ADI_TWI_MEMORY_SIZE, &hTwiDevice);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Open failed 0x%08X\n", eResult);
	}

	eResult = adi_twi_SetPrescale(hTwiDevice, PRESCALEVALUE);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Set Prescale failed 0x%08X\n", eResult);
	}

	eResult = adi_twi_SetBitRate(hTwiDevice, BITRATE);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Set Bitrate failed 0x%08X\n", eResult);
	}


	eResult = adi_twi_SetDutyCycle(hTwiDevice, DUTYCYCLE);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Set Duty cycle failed 0x%08X\n", eResult);
	}

	eResult = adi_twi_SetHardwareAddress(hTwiDevice, TARGETADDR);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Set Hw address failed 0x%08X\n", eResult);
	}

	return eResult;
}

static int Stop_TWI(void)
{
	ADI_TWI_RESULT eResult;

	eResult = adi_twi_Close(hTwiDevice);
	CHECK_RESULT(eResult);

	return eResult;
}

/*****************************************************************************************************************************/

static int ADAU_1962_init(void)
{	int i;
	ADAU_1962_Pllinit();
	for(i=0;i<28;i++)
	{
		/* write value */
		Write_TWI_8bit_Reg(Config_array_DAC[i].Reg_Add,Config_array_DAC[i].Value);
		Config_read_DAC[i]=Read_TWI_8bit_Reg(Config_array_DAC[i].Reg_Add);
		if(Config_array_DAC[i].Value!= Config_read_DAC[i])
		{
			DEBUG_INFORMATION("\n Configuring ADAU_1962 failed");
			return FAILED;
		}
	}

	return SUCCESS;
}

static int ADAU_1962_Pllinit(void)
{
	int status,delay1=0xffff;

	ADI_TWI_RESULT eResult;

   	eResult = adi_twi_SetHardwareAddress(hTwiDevice, TARGETADDR_1962);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Set Hw address failed 0x%08X\n", eResult);
	}

	Write_TWI_8bit_Reg(ADAU1962_PLL_CTL_CTRL0,0x01);
	while(delay1--)
	{
		asm("nop;");
	}

	Write_TWI_8bit_Reg(ADAU1962_PLL_CTL_CTRL0,0x05);

	delay1=0xffff;
	while(delay1--)
	{
		asm("nop;");
	}

	Write_TWI_8bit_Reg(ADAU1962_PLL_CTL_CTRL1,0x22);
	delay1=0xffff;
	while(delay1--)
	{
		asm("nop;");
	}
	status=Read_TWI_8bit_Reg(ADAU1962_PLL_CTL_CTRL1);
	while(!((status & 0x4)>>2)  )
	{
		status=Read_TWI_8bit_Reg(ADAU1962_PLL_CTL_CTRL1);
	}

	return eResult;
}




static int ADAU_1979_init(void)
{	int i;

	ADAU_1979_Pllinit();

	for(i=0;i<16;i++)
	{
		Write_TWI_8bit_Reg(Config_array_ADC[i].Reg_Add,Config_array_ADC[i].Value);
		Config_read_ADC[i]=Read_TWI_8bit_Reg(Config_array_ADC[i].Reg_Add);
		if(Config_array_ADC[i].Value!= Config_read_ADC[i])
		{
			DEBUG_INFORMATION("\n Configuring ADAU_1979 failed");
			return FAILED;
		}
	}

	return SUCCESS;
}

static int ADAU_1979_Pllinit(void)
{

	int status,delay1=0xffff;

	ADI_TWI_RESULT eResult;

   	eResult = adi_twi_SetHardwareAddress(hTwiDevice, TARGETADDR_1979);
	if(eResult!=ADI_TWI_SUCCESS)
	{
		REPORT_ERROR("TWI Set Hw address failed 0x%08X\n", eResult);
	}

   	Write_TWI_8bit_Reg(ADAU1979_REG_POWER,0x01);
	Write_TWI_8bit_Reg(ADAU1979_REG_PLL,0x03);
	status=Read_TWI_8bit_Reg(ADAU1979_REG_PLL);
	while(delay1--)
			{
				asm("nop;");
			}
	while(!((status & 0x80)>>7)  )
	{
		status=Read_TWI_8bit_Reg(ADAU1979_REG_PLL);
		asm("nop;");
	}

	return eResult;

}

void main(int argc, char *argv[])
{
	/**
	 * Initialize managed drivers and/or services that have been added to
	 * the project.
	 * @return zero on success
	 */
	/* My original comment added 2023 10 22 */
	/* My original comment added 2023 10 22 No 2 */

	uint32_t Result=0;

	adi_initComponents();

	/* Begin adding your custom code here */

	DEBUG_INFORMATION( "ADC/DAC Audio talk-through test TDM\n" );

   /* SPU initialization */
	if (Result==0u)
	{
		Result=SPU_init();
	}

	/* Switch Configuration */
	Switch_Configurator();

	/* SRU Configuration */
	SRU_Init();

	/* TWI Initialization */
	if (Result==0u)
	{
		Result=Init_TWI();
	}

	/* ADAU1962 Initialization */
	if (Result==0u)
	{
		Result=ADAU_1962_init();
	}

	/* ADAU1979 Initialization */
	if (Result==0u)
	{
		Result=ADAU_1979_init();
	}

	/* SPORT Initialization */
	if (Result==0u)
	{
		Result=Sport_Init();
	}

	/* Close TWI */
	if (Result==0u)
	{
		Result=Stop_TWI();
	}

#if (ADI_CONFIG_CONTINUOUS_AUDIO==0)

	while(1)
	{
		if(TestCallbackCount == 8000)
		{
			delay=0xffffff;
			while(delay--)
			{
				asm("nop;");
			}

			if (Result==0u)
			{
				Result=Sport_Stop();
			}

			if(Result==0u)
			{
				DEBUG_INFORMATION("All done\n");
			}
			else
			{
				DEBUG_INFORMATION("Example failed.\n");
			}

			break;
		}
	}
#elif (ADI_CONFIG_CONTINUOUS_AUDIO==1)
	while(1);
#endif

}

