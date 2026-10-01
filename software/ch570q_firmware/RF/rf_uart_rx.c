/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_basic.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2024/08/15
 * Description        : ���ߴ���-���ն�
 *
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/******************************************************************************/
/* ͷ�ļ����� */
#include "rf.h"
#include "rf_uart_rx.h"
#include "usb_uart.h"

#define gTxBuf         rf_role_tx_buf
#define gSysClock      slaveSysClock
#define gRfTxCount     slaveRfTxCount
#define gIntervalTimer slaveIntervalTimer
#define gServerData    slaveServerData
#define gTimeoutMax    slaveTimeoutMax
#define gTimeout       slaveTimeout
#define gDataSeq       slaveDataSeq
#define gRfStatus      slaveRfStatus
#define gBoundStatus   slaveBoundStatus
#define gRxDataStatus  slaveRxDataStatus
#define pRfBuf         slaveRfBuf
#define rf_buf         rf_role_buf
#define rf_buffer      slaveRfBuffer
#define rfCBs          slaveRfCBs
#define gBaudRate      slaveBaudRate
#define scanDevices    rf_role_control.scan_devices
#define rf_remote_message rf_role_control.remote_message

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
extern uint8_t rf_role_buf[RF_BUF_LEN];
static struct simple_buf *pRfBuf = NULL;
static struct simple_buf rf_buffer;
uint32_t gBaudRate;

extern rfTxBuf_t rf_role_tx_buf;
uint32_t gSysClock;
uint32_t gRfTxCount;
uint32_t gIntervalTimer;
uint16_t gServerData; // ��Ҫ���籣�棬�豣����flash
uint16_t gTimeoutMax;
uint16_t gTimeout;

uint8_t gDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t gRxDataStatus;

static uint8_t scanActive;
static uint8_t scanCount;
static uint8_t scanTargetValid;
static uint8_t scanTarget[6];
static volatile uint8_t remoteState;
static uint8_t remoteTransaction;
static uint32_t remoteDeadline;
static int8_t lastRssi;
static uint8_t rssiValid;

#define REMOTE_STATE_IDLE     0
#define REMOTE_STATE_PENDING  1
#define REMOTE_STATE_COMPLETE 2

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessCrcError( void );
static  void rfProcessTimeout( void );

static __attribute__((noinline)) void remote_receive_response(rfPackage_t *packet)
{
    uint8_t length = packet->length - PKT_DATA_OFFSET;
    rfRemoteMessage_t *message = (rfRemoteMessage_t *)(packet + 1);

    if(remoteState == REMOTE_STATE_PENDING && length >= 4U &&
       message->transaction == rf_remote_message.transaction &&
       message->command == rf_remote_message.command &&
       message->length <= RF_REMOTE_PAYLOAD_SIZE &&
       length >= 4U + message->length)
    {
        memcpy(&rf_remote_message, message, 4U + message->length);
        remoteState = REMOTE_STATE_COMPLETE;
    }
}

static __attribute__((noinline)) uint8_t remote_prepare_request(
    rfPackage_t *packet, rfRsp_t *response)
{
    if(remoteState != REMOTE_STATE_PENDING)
        return 0;

    packet->length = PKT_DATA_OFFSET + 1U + 4U +
                     rf_remote_message.length;
    response->opcode = OPCODE_REMOTE;
    memcpy(response->other.rspData, &rf_remote_message,
           4U + rf_remote_message.length);
    return 1;
}

static void scan_record(const bound_req_t *request, int8_t rssi)
{
    uint8_t index;

    for(index = 0; index < scanCount; index++)
    {
        if(memcmp(scanDevices[index].device_id, request->deviceId, 6) == 0)
            break;
    }
    if(index == scanCount)
    {
        if(scanCount >= RF_SCAN_MAX_DEVICES)
            return;
        scanCount++;
        memcpy(scanDevices[index].device_id, request->deviceId, 6);
        scanDevices[index].reserved = 0;
    }
    scanDevices[index].rssi = rssi;
    scanDevices[index].server_data = request->severData;
}

// GAP Service Callbacks
rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessCrcError,
    rfProcessTimeout,
};

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
 * @fn      rf_rand
 *
 * @brief   ��������ɺ���
 *
 * @return  None.
 */

__HIGH_CODE
uint32_t rf_rand16( uint32_t seed )
{
    static uint32_t holdrand;
    uint64_t tmp;

    holdrand += seed;
    holdrand = holdrand * 1664525 + 1013904223;
    tmp = holdrand;
    tmp = (tmp*0xFFFF)>>32;
    return tmp;
}

/*******************************************************************************
 * @fn      rf_rand
 *
 * @brief   ��������ɺ���
 *
 * @return  None.
 */

__HIGH_CODE
uint32_t rf_rand_aa( uint16_t rand )
{
    uint32_t aa = rand;

    aa = (aa<<8)|0x6E0000B6;
    return aa;
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
static void rf_disconnect( void )
{
    PRINT("disconnect.\n" );
    RFRole_Shut( );
    gBoundStatus = BOUND_STATUS_IDLE;
    rssiValid = 0;
    gRxDataStatus = DATA_STATUS_START;
    TMR_ITCfg(DISABLE, TMR_IT_CYC_END); // �ر��ж�

    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );
    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );
    gRfStatus = RF_STATUS_WAIT;
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
static void rf_bound( bound_rsp_t *rsp )
{
    gTimeout = 0;
    gBoundStatus = BOUND_STATUS_WAIT;
    rf_tx_set_sync_word( rsp->accessaddr );
    rf_tx_set_frequency( rsp->channel );
    rf_tx_set_phy_type( rsp->phy );

    rf_rx_set_sync_word( rsp->accessaddr );
    rf_rx_set_frequency( rsp->channel );
    rf_rx_set_phy_type( rsp->phy );
    scanActive = 0;
    scanTargetValid = 0;
    PRINT("bound success.%X %x\n",rsp->accessaddr,rsp->channel );
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
    rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;

    if( gBoundStatus == BOUND_STATUS_IDLE )
    {
        if( pPkt->type == PKT_CMD_BOUND_REQ )
        {
            BOOL reg=0;
            bound_req_t  *pReq_t =  ( bound_req_t  *)(pPkt+1);
            if( pPkt->length == sizeof(bound_req_t) + 2 )
            {
                int8_t rssi  = *(uint8_t* )((uint8_t* )pPkt + pPkt->length + 2 +2 );

                // �ǵ�һ�����ӣ���ַƥ�����
                if(scanActive)
                {
                    scan_record(pReq_t, rssi);
                    if(scanTargetValid &&
                       memcmp(scanTarget, pReq_t->deviceId, 6) == 0)
                        reg = 1;
                }
                else if( pReq_t->severData )
                {
                    // ��Ϣƥ�䣬����dongle�����ϵ���
                    if( !gServerData || pReq_t->severData == gServerData )
                    {
                        reg = 1;
                    }
                }
                else
                {
                    // ��һ�����ӣ��迿������
                    if( rssi > -35 )
                    {
                        reg = 1;
                    }
                }
                if( reg )
                {
                    bound_rsp_t *pRsp_t = (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                    gRfStatus = RF_STATUS_TXRSP;
                    // �����´λ����������Ϣ
                    gDataSeq = 0;
                    gServerData = rf_rand16( rssi );
                    pPkt_t->type = PKT_CMD_BOUND_RSP;
                    pPkt_t->length = PKT_DATA_OFFSET+sizeof(bound_rsp_t);
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                    pRsp_t->accessaddr = rf_rand_aa( gServerData );
                    pRsp_t->channel = gServerData&0x3F;
                    pRsp_t->phy = CONN_PHY_TYPE; // 2M
                    pRsp_t->severData = gServerData;
                    pRsp_t->interval = CONN_INTERVAL;
                    pRsp_t->timeout = CONN_TIMEOUT;
                    rf_tx_start( pPkt_t, 20 );
                    gDataSeq++;
                    gIntervalTimer = pReq_t->interval*1000;
                    gTimeoutMax = BOUND_EST_COUNT;
                    return ;
                }
                else
                {
                    PRINT(" reject..\n");
                }
                PRINT( "rssi=%d \n",rssi);
            }
        }
        gRfStatus = RF_STATUS_WAIT;
    }
    else
    {
        rfRsp_t *pRsp_t = (rfRsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
        lastRssi = *(int8_t *)((uint8_t *)pPkt + pPkt->length + 4U);
        rssiValid = 1;
        if( pPkt->type == PKT_CMD_GET_STATUS )
        {
            gRfStatus = RF_STATUS_TX;
            if( pPkt->seq == gDataSeq )
            {
                uint8_t s;
                typeBufSize len = DATA_LEN_MAX_TX;

                remote_receive_response(pPkt);

                pPkt_t->type = PKT_CMD_RSP_STATUS;
                if( remote_prepare_request(pPkt_t, pRsp_t) )
                {
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                else if( gBoundStatus < BOUND_STATUS_EST  )
                {
                    // ���ʹ��ڲ�����
                    pPkt_t->length = PKT_DATA_OFFSET+1+sizeof(RfUartPara);
                    pRsp_t->opcode = OPCODE_BSP;
                    pRsp_t->buad_t.BaudRate = RfUartPara.BaudRate;
                    pRsp_t->buad_t.StopBits = RfUartPara.StopBits;
                    pRsp_t->buad_t.ParityType = RfUartPara.ParityType;
                    pRsp_t->buad_t.DataBits = RfUartPara.DataBits;
                    pRsp_t->buad_t.ioStaus = RfUartPara.ioStaus;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                else
                {
                    s = USB_RfRxQuery( pRsp_t->other.rspData, &len );
                    if( s == 0 )
                    {
                        pPkt_t->length = PKT_DATA_OFFSET+1+len;
                        pRsp_t->opcode = OPCODE_DATA;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                        gRfTxCount += len;
                    }
                    else if( s == 0x80 )
                    {
                        // ���ʹ��ڲ�����
                        pPkt_t->length = PKT_DATA_OFFSET+1+sizeof(RfUartPara);
                        pRsp_t->opcode = OPCODE_BSP;
                        pRsp_t->buad_t.BaudRate = RfUartPara.BaudRate;
                        pRsp_t->buad_t.StopBits = RfUartPara.StopBits;
                        pRsp_t->buad_t.ParityType = RfUartPara.ParityType;
                        pRsp_t->buad_t.DataBits = RfUartPara.DataBits;
                        pRsp_t->buad_t.ioStaus = RfUartPara.ioStaus;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                    }
                    else
                    {
                        pPkt_t->length = PKT_DATA_OFFSET;
                        pRsp_t->opcode = OPCODE_ACK;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                    }
                }
                gDataSeq++;
            }
            rf_tx_start( pPkt_t, 20 );
            if( pRsp_t->opcode == OPCODE_BSP )
            {
                PRINT("uart param.\n");
            }
        }
        else if( pPkt->type == PKT_DATA_FLAG )
        {
            gRfStatus = RF_STATUS_TX;
            if( pPkt->seq == gDataSeq )
            {
                typeBufSize len = pPkt->length;
                len -= PKT_DATA_OFFSET;
                write_buf( pRfBuf, (pPkt+1), &len );
                gRxDataStatus = DATA_STATUS_RCV;
                // ������ջ���������ᶪ����
                len = DATA_LEN_MAX_TX;
                pPkt_t->type = PKT_DATA_RSP_ACK;
                if( gBoundStatus == BOUND_STATUS_EST && !USB_RfRxQuery( pRsp_t->other.rspData, &len ) )
                {
                    pPkt_t->length = PKT_DATA_OFFSET+1+len;
                    pRsp_t->opcode = OPCODE_DATA;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                    gRfTxCount += len;
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                gDataSeq++;
            }
            else
            {
                // �ش�
            }
            rf_tx_start( pPkt_t, 20 );
        }
        else
        {
            PRINT("error data.\n");
            if( ++gTimeout > gTimeoutMax )
            {
                gRxDataStatus = DATA_STATUS_TIMEOUT;
                PRINT("connect timeout.\n");
            }
            else
            {
                gRfStatus = RF_STATUS_WAIT;
            }
            return ;
        }
        if( gBoundStatus == BOUND_STATUS_WAIT )
        {
            // ����ȷ�ϣ���������ͨ�ų�ʱ
            gBoundStatus = BOUND_STATUS_EST;
            gIntervalTimer = CONN_INTERVAL*1000;
            gTimeoutMax = (CONN_TIMEOUT*10/CONN_INTERVAL);
       }
       gTimeout = 0;
    }
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
static void rfProcessTx( void )
{
    if( gRfStatus == RF_STATUS_TXRSP )
    {
         rf_bound( (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN] );
    }
    gRfStatus = RF_STATUS_RX;
    rf_rx_start( gIntervalTimer );
}

/*******************************************************************************
 * @fn      RF_ProcessRxError
 *
 * @brief
 *
 * @param   None.
 *
 * @return  None.
 */

__HIGH_CODE
static void rfProcessCrcError( void )
{
    gRfStatus = RF_STATUS_WAIT;
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
static void rfProcessTimeout( void )
{
    // PRINT("r -timeout %x\n",gRfStatus);
    if( gBoundStatus && ++gTimeout > gTimeoutMax )
    {
        gRxDataStatus = DATA_STATUS_TIMEOUT;
        PRINT("connect timeout.\n");
    }
    else
    {
        gRfStatus = RF_STATUS_WAIT;
    }
}

/*******************************************************************************
 * @fn      RF_RxQuery
 *
 * @brief
 *
 * @return  None.
 */

__HIGH_CODE
uint8_t RF_RxQuery( void *buf, typeBufSize *len )
{
    uint8_t *p;

    if( gRxDataStatus == DATA_STATUS_RCV )
    {
        if( read_buf( pRfBuf, buf, len ) == 0 )
        {
            PFIC_DisableIRQ(BLEL_IRQn);
            gRxDataStatus = DATA_STATUS_START;
            PFIC_EnableIRQ(BLEL_IRQn);
        }
    }
    else if( gRxDataStatus == DATA_STATUS_TIMEOUT )
    {
        *len = 0;
        rf_disconnect( );
    }
    else
    {
        *len = 0;
        if( gRfStatus == RF_STATUS_WAIT )
        {
            gRfStatus = RF_STATUS_RX;
            rf_rx_start( gIntervalTimer );
        }
    }
    return *len;
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
void RF_UartRxInit( void )
{
    scanActive = 0;
    scanCount = 0;
    scanTargetValid = 0;
    remoteState = REMOTE_STATE_IDLE;
    rssiValid = 0;
    PRINT("----------------- rf uart rx mode -----------------\n");
    gRxDataStatus = DATA_STATUS_IDLE;
    rf_buffer_create(&pRfBuf);
    gDataSeq = 0;
    gServerData = 0;
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    gIntervalTimer = CONN_INTERVAL*1000;
    gSysClock = GetSysClock( );
    RFRole_RegisterStatusCbs( &rfCBs );
    gRfStatus = RF_STATUS_WAIT;
}

void RF_ScanStart(void)
{
    scanCount = 0;
    scanTargetValid = 0;
    scanActive = 1;
}

uint8_t RF_ScanIsActive(void)
{
    return scanActive;
}

uint8_t RF_ScanGetResults(rf_scan_device_t *devices, uint8_t capacity)
{
    uint8_t count;

    PFIC_DisableIRQ(BLEL_IRQn);
    count = scanCount;
    if(count > capacity)
        count = capacity;
    memcpy(devices, scanDevices, count * sizeof(rf_scan_device_t));
    PFIC_EnableIRQ(BLEL_IRQn);
    return count;
}

uint8_t RF_ScanSelect(const uint8_t device_id[6])
{
    uint8_t index;

    for(index = 0; index < scanCount; index++)
    {
        if(memcmp(scanDevices[index].device_id, device_id, 6) == 0)
        {
            memcpy(scanTarget, device_id, 6);
            scanTargetValid = 1;
            return 0;
        }
    }
    return 1;
}

uint8_t RF_RxGetRssi(int8_t *rssi)
{
    if(gBoundStatus != BOUND_STATUS_EST || !rssiValid)
        return 1;
    *rssi = lastRssi;
    return 0;
}

uint8_t RF_RemoteExchange(uint8_t command, const uint8_t *request,
                          uint8_t request_length, uint8_t *status,
                          uint8_t *response, uint8_t *response_length)
{
    uint8_t result;

    if(request_length > RF_REMOTE_PAYLOAD_SIZE)
        return RF_REMOTE_EXCHANGE_BUSY;

    PFIC_DisableIRQ(BLEL_IRQn);
    if(remoteState == REMOTE_STATE_IDLE)
    {
        remoteTransaction++;
        if(remoteTransaction == 0U)
            remoteTransaction = 1U;
        rf_remote_message.transaction = remoteTransaction;
        rf_remote_message.command = command;
        rf_remote_message.status = 0;
        rf_remote_message.length = request_length;
        if(request_length)
            memcpy(rf_remote_message.payload, request, request_length);
        remoteDeadline = SYS_GetSysTickCnt() + GetSysClock() * 2U;
        remoteState = REMOTE_STATE_PENDING;
        result = RF_REMOTE_EXCHANGE_PENDING;
    }
    else if(remoteState == REMOTE_STATE_PENDING)
    {
        if((int32_t)(SYS_GetSysTickCnt() - remoteDeadline) >= 0)
        {
            remoteState = REMOTE_STATE_IDLE;
            result = RF_REMOTE_EXCHANGE_TIMEOUT;
        }
        else if(command != rf_remote_message.command ||
                request_length != rf_remote_message.length ||
                (request_length &&
                 memcmp(request, rf_remote_message.payload, request_length) != 0))
        {
            result = RF_REMOTE_EXCHANGE_BUSY;
        }
        else
        {
            result = RF_REMOTE_EXCHANGE_PENDING;
        }
    }
    else if(command != rf_remote_message.command)
    {
        result = RF_REMOTE_EXCHANGE_BUSY;
    }
    else
    {
        *status = rf_remote_message.status;
        *response_length = rf_remote_message.length;
        if(rf_remote_message.length)
            memcpy(response, rf_remote_message.payload,
                   rf_remote_message.length);
        remoteState = REMOTE_STATE_IDLE;
        result = RF_REMOTE_EXCHANGE_COMPLETE;
    }
    PFIC_EnableIRQ(BLEL_IRQn);
    return result;
}
/******************************** endfile @rf ******************************/
