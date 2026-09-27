/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_basic.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2024/08/15
 * Description        : ���ߴ���-���Ͷˣ�ע��RF����ʧ�� RESEND_COUNT �����ᶪ�����ݰ�
 *
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/******************************************************************************/
/* ͷ�ļ����� */
#include "rf.h"
#include "rf_uart_rx.h"
#include "rf_uart_tx.h"
#include "uart.h"

#define gTxBuf        rf_role_tx_buf
#define gRfRxCount    hostRfRxCount
#define gInterval     hostInterval
#define gTimeoutMax   hostTimeoutMax
#define gTimeout      hostTimeout
#define gServerData   hostServerData
#define gTxDataSeq    hostTxDataSeq
#define gRfStatus     hostRfStatus
#define gBoundStatus  gBoundStatus
#define getDataProbe  hostGetDataProbe
#define gRfRxFlag     hostRfRxFlag
#define pPkt_t        hostPkt
#define rfCBs         hostRfCBs
#define pRfBuf        hostRfBuf
#define rf_buf        rf_role_buf
#define rf_buffer     hostRfBuffer
#define RF_bound_Flag hostBoundFlag
#define ledcount      hostLedCount
#define rf_remote_message rf_role_control.remote_message

/*********************************************************************
 * GLOBAL TYPEDEFS
 */

extern rfTxBuf_t rf_role_tx_buf;
uint32_t gRfRxCount;
uint16_t gInterval;
uint16_t gTimeoutMax;
uint16_t gTimeout;
uint16_t gServerData;

uint8_t gTxDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t getDataProbe;
uint32_t  gRfRxFlag;
rfPackage_t *pPkt_t;
static volatile uint8_t remoteRequestPending;
static volatile uint8_t remoteResponseReady;
static volatile uint8_t remoteResponseSent;
static volatile uint8_t remoteResetAfterAck;
static volatile uint8_t remoteResetReady;
static uint8_t remoteTransaction;

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessTimeout( void );

static __attribute__((noinline)) void remote_ack_response(void)
{
    if(!remoteResponseSent)
        return;

    remoteResponseSent = 0;
    remoteResponseReady = 0;
    if(remoteResetAfterAck)
    {
        remoteResetAfterAck = 0;
        remoteResetReady = 1;
    }
}

static __attribute__((noinline)) void remote_receive_request(
    rfPackage_t *packet, rfRsp_t *response)
{
    uint8_t length = packet->length - PKT_DATA_OFFSET - 1U;
    rfRemoteMessage_t *message =
        (rfRemoteMessage_t *)response->other.rspData;

    if(length < 4U || message->length > RF_REMOTE_PAYLOAD_SIZE ||
       length < 4U + message->length)
        return;

    if(message->transaction != remoteTransaction)
    {
        memcpy(&rf_remote_message, message, 4U + message->length);
        remoteTransaction = message->transaction;
        remoteRequestPending = 1;
        remoteResponseReady = 0;
    }
    else if(remoteResponseReady)
    {
        getDataProbe = 6;
    }
}

static __attribute__((noinline)) uint8_t remote_prepare_response(
    rfPackage_t *packet)
{
    if(!remoteResponseReady || !gBoundStatus)
        return 0;

    gRfStatus = RF_STATUS_GETS;
    packet->type = PKT_CMD_GET_STATUS;
    packet->length = PKT_DATA_OFFSET + 4U + rf_remote_message.length;
    memcpy(packet + 1, &rf_remote_message, 4U + rf_remote_message.length);
    gTxBuf.status = STA_BUSY;
    packet->seq = gTxDataSeq;
    packet->resv = 0;
    remoteResponseSent = 1;
    return 1;
}

// tf status callbacks
rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessTimeout,
    rfProcessTimeout,
};

#define  RF_BUF_LEN    128
extern uint8_t rf_role_buf[RF_BUF_LEN];
static struct simple_buf rf_buffer;
struct simple_buf *pRfBuf = NULL;

uint8_t volatile RF_bound_Flag;
uint32_t ledcount = 0;
static uint8_t deviceId[6];
/*********************************************************************
 * @fn      uart_buffer_create
 *
 * @brief   Create a file called uart_buffer simple buffer of the buffer
 *          and assign its address to the variable pointed to by the buffer pointer.
 *
 * @param   buf    -   a parameter buf pointing to a pointer.
 *
 * @return  none
 */
static void rf_buffer_create(struct simple_buf **buf)
{
    *buf = simple_buf_create(&rf_buffer, rf_role_buf, sizeof(rf_role_buf) );
}

/*******************************************************************************
 * @fn      rf_disconnect
 *
 * @brief   �Ͽ�����
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
static void rf_disconnect( void )
{
    gBoundStatus = BOUND_STATUS_IDLE;
    UART_SetTimer( ADV_INTERVAL );
    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );

    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );

    RF_bound_Flag = 0;
    PRINT("disconnect.\n" );
}

/*******************************************************************************
 * @fn      rf_bound
 *
 * @brief   �Ͽ�����
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
static void rf_bound( bound_rsp_t *rsp )
{
    rfBoundInfo_t info;

    gTimeout = 0;
    gBoundStatus = BOUND_STATUS_WAIT;
    gInterval = rsp->interval;
    gTimeoutMax = (rsp->timeout*10/rsp->interval);
    gServerData = rsp->severData;
    rf_tx_set_sync_word( rsp->accessaddr );
    rf_tx_set_frequency( rsp->channel );
    rf_tx_set_phy_type( rsp->phy );
    rf_rx_set_sync_word( rsp->accessaddr );
    rf_rx_set_frequency( rsp->channel );
    rf_rx_set_phy_type( rsp->phy );
    info.head = 0x55aa;
    info.serverData = gServerData;
    FLASH_ROM_ERASE( BOUND_INFO_FLASH_ADDR, 4096 );
    FLASH_ROM_WRITE( BOUND_INFO_FLASH_ADDR,&info,4 );
    
    RF_bound_Flag = 1;
    PRINT("bound success.%x %x\n",rsp->accessaddr,rsp->channel );
}

/*******************************************************************************
 * @fn      RF_ProcessRx
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
static void rfProcessRx( rfPackage_t *pPkt )
{
    if( gTxDataSeq == pPkt->seq )
    {
        if( pPkt->type == PKT_DATA_RSP_ACK )
        {
            // ���ݷ��ͳɹ�
            if( pPkt->length > PKT_DATA_OFFSET+1 )
            {
                typeBufSize len;

                pPkt_t = pPkt;
                {
                    rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
                    len = pPkt_t->length-PKT_DATA_OFFSET-1;
                    gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                    if( !len )
                    {
                        UART_Send(pRsp_t->other.rspData,pPkt_t->length-PKT_DATA_OFFSET-1);
                    }
                    if( !R8_UART_TFC )
                    {
                        PFIC_SetPendingIRQ( UART_IRQn );
                    }
                }
            }
            getDataProbe = 6;
        }
        else if( pPkt->type == PKT_CMD_BOUND_RSP )
        {
            rf_bound( (bound_rsp_t *)(pPkt+1) );
        }
        else if( pPkt->type == PKT_CMD_RSP_STATUS )
        {
            rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt+1);

            remote_ack_response();

            if( gBoundStatus == BOUND_STATUS_WAIT )
            {
                gBoundStatus = BOUND_STATUS_EST;
                UART_SetTimer( gInterval );
            }

            if(  pPkt->length == PKT_DATA_OFFSET )
            {

            }
            // ״̬Ӧ��Ϊ�Զ��豸������
            else if( pRsp_t->opcode == OPCODE_BSP )
            {
                UART_SetConfig(pRsp_t->buad_t.BaudRate,
                               pRsp_t->buad_t.StopBits,
                               pRsp_t->buad_t.ParityType,
                               pRsp_t->buad_t.DataBits);

#if 1
                // DTR ��ƽ״̬
                if( pRsp_t->buad_t.ioStaus&0x20 )
                {
                    GPIOA_SetBits( DTR );
                }
                else
                {
                    GPIOA_ResetBits( DTR );
                }
                // RTS ��ƽ״̬
                if( pRsp_t->buad_t.ioStaus&0x40 )
                {
                    GPIOA_SetBits( RTS );
                }
                else
                {
                    GPIOA_ResetBits( RTS );
                }
#endif
            }
            else if( pRsp_t->opcode == OPCODE_DATA )
            {
                typeBufSize len;

                pPkt_t = pPkt;
                getDataProbe = 6;
                {
                    rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
                    len = pPkt_t->length-PKT_DATA_OFFSET-1;
                    gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                    if( !len )
                    {
                        UART_Send(pRsp_t->other.rspData,pPkt_t->length-PKT_DATA_OFFSET-1);
                    }
                    if( !R8_UART_TFC )
                    {
                        PFIC_SetPendingIRQ( UART_IRQn );
                    }
                }
            }
            else if( pRsp_t->opcode == OPCODE_REMOTE )
            {
                remote_receive_request(pPkt, pRsp_t);
            }
        }
        else
        {

        }
        gRfStatus = RF_STATUS_IDLE;
        gTxBuf.status = STA_IDLE;
        gTxDataSeq ++;
        gTimeout = 0;
    }
}

/*******************************************************************************
 * @fn      RF_ProcessTx
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
static void rfProcessTx( void )
{
    if( gRfStatus == RF_STATUS_RETX )
    {
        gRfStatus = RF_STATUS_REWAIT;
    }
    else
    {
        gRfStatus = RF_STATUS_WAITRSP;
    }
    rf_rx_start( 150 );
}

/*******************************************************************************
 * @fn      RF_ProcessTimeout
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
static  void rfProcessTimeout( void )
{
    gTxBuf.status = STA_RESEND;
    if( gRfStatus == RF_STATUS_WAITRSP )
    {
        gTxBuf.resendCount = RESEND_COUNT;
    }
    else
    {
        if( gBoundStatus == BOUND_STATUS_WAIT )
        {
            if( ++ gTimeout > BOUND_EST_COUNT )
            {
                rf_disconnect( );
            }
            gTxBuf.status = STA_IDLE;
        }
        else if( gBoundStatus == BOUND_STATUS_EST )
        {
            if( ++ gTimeout > gTimeoutMax )
            {
                rf_disconnect( );
                gTxBuf.status = STA_IDLE;
            }
        }
    }
}


/*******************************************************************************
 * @fn      RF_StatusQuery
 *
 * @brief   ״̬����
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
void RF_StatusQuery( void )
{
    uint8_t s;

    //��ȡbound��־
    if(RF_bound_Flag)
    {
        ledcount++;
        if(ledcount >=50000)
        {
            ledcount = 0;
        }
    }

    if( gTxBuf.status == STA_IDLE )
    {
        rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;

        if(remote_prepare_response(pPkt_t))
        {
            rf_tx_start(gTxBuf.TxBuf, 60);
            return;
        }

        gTxBuf.len  = DATA_LEN_MAX_TX;
        s = UART_RxQuery( (void *)(pPkt_t+1), &gTxBuf.len );
        // ��������
        if( s == 0 )
        {

            gRfStatus = RF_STATUS_TX;
            pPkt_t->type = PKT_DATA_FLAG;
            pPkt_t->length = gTxBuf.len + PKT_DATA_OFFSET;
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else if( s == 0x80 )
        {
            if( gBoundStatus )
            {
                // ��ȡ״̬
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
            }
            else
            {
                // �����
                bound_req_t *pReq_t = (bound_req_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                gRfStatus = RF_STATUS_REQ;
                pPkt_t->type = PKT_CMD_BOUND_REQ;
                pPkt_t->length = PKT_DATA_OFFSET + sizeof(bound_req_t);
                pReq_t->interval = ADV_INTERVAL;
                pReq_t->severData = gServerData;
                memcpy(pReq_t->deviceId, deviceId, sizeof(deviceId));
                gTxDataSeq = 0;
            }
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else
        {
            if( getDataProbe )
            {
                getDataProbe--;
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
                gTxBuf.status = STA_BUSY;
                pPkt_t->seq = gTxDataSeq;
                pPkt_t->resv = 0;
                rf_tx_start( gTxBuf.TxBuf, 60 );
            }
        }
    }
    else if( gTxBuf.status == STA_RESEND )
    {
        if( gTxBuf.resendCount )
        {
            if( gTxBuf.resendCount < RESEND_COUNT/2 )
            {
                PRINT("*%d %d\n",gTxBuf.resendCount,gTxDataSeq);
            }
            gRfStatus = RF_STATUS_RETX;
            if( gTxBuf.resendCount != 0xFF ) gTxBuf.resendCount--;
            gTxBuf.status = STA_BUSY;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else
        {
            // ����ʧ�ܶ���
            PRINT(" resend fail.%d\n",gTxBuf.TxBuf[1]);
            gTxBuf.status = STA_IDLE;
        }
    }
}

/*******************************************************************************
 * @fn      RF_UartTxInit
 *
 * @brief   RF uart����Ӧ�ó�ʼ��
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
void RF_UartTxInit( void )
{
    rfBoundInfo_t *pInfo;
    __attribute__((aligned(4))) uint8_t mac[8];
    PRINT("----------------- rf uart tx mode -----------------\n");
    gTxDataSeq = 0;
    gRfRxFlag = 0;
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    remoteRequestPending = 0;
    remoteResponseReady = 0;
    remoteResponseSent = 0;
    remoteResetAfterAck = 0;
    remoteResetReady = 0;
    remoteTransaction = 0;
    rf_buffer_create(&pRfBuf);
    GetMACAddress(mac);
    memcpy(deviceId, mac, sizeof(deviceId));

    pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);
    if( pInfo->head == BOUND_INFO_HEAD )
    {
        gServerData = pInfo->serverData;
    }
    else {
        gServerData = 0;
    }
    PRINT("gServerData = %x \n",gServerData);
    RFRole_RegisterStatusCbs( &rfCBs );
}

void RF_GetPairStatus(uint8_t *paired, uint8_t *state, uint16_t *server_data)
{
    rfBoundInfo_t *info = (rfBoundInfo_t *)BOUND_INFO_FLASH_ADDR;
    *paired = info->head == BOUND_INFO_HEAD;
    *state = gBoundStatus;
    *server_data = *paired ? info->serverData : 0;
}

uint8_t RF_ClearPairing(void)
{
    return (uint8_t)FLASH_ROM_ERASE(BOUND_INFO_FLASH_ADDR, 4096);
}

uint8_t RF_RemoteTakeRequest(uint8_t *transaction, uint8_t *command,
                             uint8_t *payload, uint8_t *length)
{
    if(!remoteRequestPending)
        return 0;

    PFIC_DisableIRQ(BLEL_IRQn);
    *transaction = rf_remote_message.transaction;
    *command = rf_remote_message.command;
    *length = rf_remote_message.length;
    if(*length)
        memcpy(payload, rf_remote_message.payload, *length);
    remoteRequestPending = 0;
    PFIC_EnableIRQ(BLEL_IRQn);
    return 1;
}

void RF_RemoteSetResponse(uint8_t transaction, uint8_t command,
                          uint8_t status, const uint8_t *payload,
                          uint8_t length, uint8_t reset_after_ack)
{
    PFIC_DisableIRQ(BLEL_IRQn);
    rf_remote_message.transaction = transaction;
    rf_remote_message.command = command;
    rf_remote_message.status = status;
    rf_remote_message.length = length;
    if(length)
        memcpy(rf_remote_message.payload, payload, length);
    remoteResetAfterAck = reset_after_ack;
    remoteResponseReady = 1;
    getDataProbe = 6;
    PFIC_EnableIRQ(BLEL_IRQn);
}

uint8_t RF_RemoteResetReady(void)
{
    uint8_t ready = remoteResetReady;
    remoteResetReady = 0;
    return ready;
}

/******************************** endfile @rf ******************************/
