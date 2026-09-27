/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_basic.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2024/08/15
 * Description        : 2.4G�����ģʽ�շ���������
 *
 *                      ��������
 *                      RFIP_SetTxPower
 *                      1. ֧��-20dBm ~ 4dBm ��̬����
 *
 *                      ����״̬�л��ȶ�ʱ��
 *                      RFIP_SetTxDelayTime
 *                      1.�����Ҫ�л�ͨ�����ͣ��ȶ�ʱ�䲻����80us
 *
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/******************************************************************************/
/* ͷ�ļ����� */
#include "rf.h"

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
#define  ALIGNED4(x)       ((x+3)/4*4)
__attribute__((__aligned__(4))) uint8_t RxBuf[264]; // ����DMA buf����С��264�ֽ�

rfipTx_t gTxParam;
rfipRx_t gRxParam;
rfStatusCBs_t *pCbs;

/*******************************************************************************
 * @fn      LLE_IRQHandler
 *
 * @brief   LLE_IRQHandler
 *
 * @return  None.
 */
__INTERRUPT
__HIGH_CODE
void LLE_IRQHandler( void )
{
    LLE_LibIRQHandler( );
}

/*******************************************************************************
 * @fn      BB_IRQHandler
 *
 * @brief   BB_IRQHandler
 *
 * @return  None.
 */
__INTERRUPT
__HIGH_CODE
void BB_IRQHandler( void )
{
    BB_LibIRQHandler( );
}

/******************************** ������غ��� ********************************/
/**
 * @brief   ���÷��͵�Ƶ��
 *
 * @param   f - ��Ҫ���õ�Ƶ��
 *
 * @return  None.
 */
__HIGH_CODE
void rf_tx_set_frequency( uint32_t f )
{
    gTxParam.frequency = f;
}

/*******************************************************************************
 * @brief   ���÷��͵ĵ�ַ
 *
 * @param   sync_word - ��Ҫ���õĽ����ַ
 *
 * @return  None.
 */
__HIGH_CODE
void rf_tx_set_sync_word( uint32_t sync_word )
{
    gTxParam.accessAddress = sync_word;
}

/*******************************************************************************
 * @brief   ���÷��͵�PHY����
 *
 * @param   phy - phy����
 *
 * @return  None.
 */
__HIGH_CODE
void rf_tx_set_phy_type( uint8_t phy )
{
    gTxParam.properties = (gTxParam.properties&~PHY_MODE_MASK)|(phy<<4);
}

/*******************************************************************************
 * @brief   rf���������ӳ���
 *
 * @param   pBuf - ���͵�DMA��ַ
 *
 * @return  None.
 */
__HIGH_CODE
void rf_tx_start( void *pBuf, uint16_t rfon_us )
{
    // ���͵�DMA��ַ
    gTxParam.txDMA = (uint32_t)pBuf;
    gTxParam.waitTime = rfon_us*2; // �����Ҫ�л�ͨ�����ͣ��ȶ�ʱ�䲻����80us
    RFIP_StartTx( &gTxParam );
}

/******************************** ������غ��� ********************************/
/**
 * @brief   ���ý��յĵ�ַ
 *
 * @param   sync_word - ��Ҫ���õĽ����ַ
 *
 * @return  None.
 */
__HIGH_CODE
void rf_rx_set_sync_word( uint32_t sync_word )
{
    gRxParam.accessAddress = sync_word;
}

/*******************************************************************************
 * @fn      rf_rx_set_frequency
 *
 * @param   f - ��Ҫ���õ�Ƶ��
 *
 * @return  None.
 */
__HIGH_CODE
void rf_rx_set_frequency( uint32_t f )
{
    gRxParam.frequency = f;
}

/*******************************************************************************
 * @brief   ���ý��յ�PHY����
 *
 * @param   phy - phy����
 *
 * @return  None.
 */
__HIGH_CODE
void rf_rx_set_phy_type( uint8_t phy )
{
    gRxParam.properties = (gRxParam.properties&~PHY_MODE_MASK)|(phy<<4);
}

/*******************************************************************************
 * @fn      rf_rx_start
 *
 * @brief   rf���������ӳ���
 *
 * @return  None.
 */
__HIGH_CODE
void rf_rx_start( uint32_t rx_us )
{
    // ���ý��յĳ�ʱʱ�䣬����ַ��Ч������Ҫ���������ݰ�
    gRxParam.timeOut = rx_us*2;
    RFIP_SetRx( &gRxParam );
}

/*******************************************************************************
 * @fn      RF_ProcessCallBack
 *
 * @brief   rf�жϴ�������
 *
 * @param   sta - �ж�״̬.
 *          id - ����
 *
 * @return  None.
 */
__HIGH_CODE
void RF_ProcessCallBack( rfRole_States_t sta,uint8_t id  )
{
    if( sta&RF_STATE_RX )
    {
        if( pCbs && pCbs->pfnRxCB ) pCbs->pfnRxCB( (rfPackage_t *)RxBuf );
    }
    if( sta&RF_STATE_RX_CRCERR )
    {
        if(  pCbs && pCbs->pfnCrcErrCB ) pCbs->pfnCrcErrCB( );
    }
    if( sta&RF_STATE_TX_FINISH )
    {
        if(  pCbs && pCbs->pfnTxCB ) pCbs->pfnTxCB( );
    }
    if( sta&RF_STATE_TIMEOUT )
    {
        if(  pCbs && pCbs->pfnTimeoutCB )pCbs->pfnTimeoutCB( );
    }
}

/*******************************************************************************
 * @fn      RFRole_RegisterStatusCbs
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */
void RFRole_RegisterStatusCbs( rfStatusCBs_t * p )
{
    pCbs = p;
}

/*******************************************************************************
 * @fn      RFRole_Init
 *
 * @brief   RFӦ�ò��ʼ��
 *
 * @param   None.
 *
 * @return  None.
 */
void RFRole_Init(void)
{
    {
        rfRoleConfig_t conf ={0};
        conf.rfProcessCB = RF_ProcessCallBack;
        conf.processMask = RF_STATE_RX|RF_STATE_RX_CRCERR|RF_STATE_TX_FINISH|RF_STATE_TIMEOUT;
        RFRole_BasicInit( &conf );
    }
    TPROPERTIES_CFG Properties;
    {
        Properties.cfgVal = TEST_PHY_MODE;
#if(TEST_PHY_MODE == PHY_MODE_2G4 )
        Properties.whitOff = 1;
        Properties.lengthCrc = CRC_LEN;
        Properties.ctlFiled = CTL_FILED;
        Properties.lengthAA = AA_LEN;
        Properties.lengthPreamble = PRE_LEN;
        Properties.dplEnable = DPL_EN;
        Properties.mode2G4 = MODE_2G4;
        Properties.bitOrderData = DATA_ORDER;
        Properties.crcXOREnable = CRC_XOR_EN;
#endif
        PRINT("cfgVal=%x\n",Properties.cfgVal);
    }

    // TX��ز�����ȫ�ֱ���
    {
        gTxParam.accessAddress = AA;
        gTxParam.accessAddressEx = AA_EX;
        gTxParam.frequency = DEF_FREQUENCY;
        gTxParam.crcInit = CRC_INIT;
        gTxParam.crcPoly = CRC_POLY;
        gTxParam.properties = Properties.cfgVal;
        gTxParam.waitTime = 80*2;
        gTxParam.txPowerVal = LL_TX_PWR_0_DBM;
    }

    // RX��ز�����ȫ�ֱ���
    {
        gRxParam.accessAddress = AA;
        gRxParam.accessAddressEx = AA_EX;
        gRxParam.frequency = DEF_FREQUENCY;
        gRxParam.crcInit = CRC_INIT;
        gRxParam.crcPoly = CRC_POLY;
        gRxParam.properties = Properties.cfgVal;
        gRxParam.rxDMA = (uint32_t)RxBuf;
        gRxParam.rxMaxLen = DATA_LEN_MAX_RX;
    }
    pCbs = NULL;
    PFIC_EnableIRQ( BLEB_IRQn );
    PFIC_EnableIRQ( BLEL_IRQn );
}

/******************************** endfile @rf ******************************/
