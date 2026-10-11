#include "CONFIG.h"
#include "ch32v20x_usbfs_device.h"
#include "usb_device.h"

#include <string.h>

#define USB_PACKET_SIZE       64U
#define USB_MS_OS_VENDOR_CODE 0x20U
#define USB_MS_OS_DESCRIPTOR  0x0007U

static const uint8_t device_descriptor[] = {
    0x12, USB_DESCR_TYP_DEVICE, 0x10, 0x02, 0x00, 0x00, 0x00,
    USB_PACKET_SIZE, 0x86, 0x1a, 0x2f, 0x57, 0x00, 0x01,
    0x01, 0x02, 0x03, 0x01
};

static const uint8_t configuration_descriptor[] = {
    0x09, USB_DESCR_TYP_CONFIG, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, USB_DESCR_TYP_INTERF, 0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x04,
    0x07, USB_DESCR_TYP_ENDP, 0x07, 0x02, USB_PACKET_SIZE, 0x00, 0x00,
    0x07, USB_DESCR_TYP_ENDP, 0x87, 0x02, USB_PACKET_SIZE, 0x00, 0x00
};

static const uint8_t bos_descriptor[] = {
    0x05, USB_DESCR_TYP_BOS, 0x21, 0x00, 0x01,
    0x1c, 0x10, 0x05, 0x00,
    0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c,
    0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,
    0x00, 0x00, 0x03, 0x06, 0xa2, 0x00,
    USB_MS_OS_VENDOR_CODE, 0x00
};

static const uint8_t ms_os_20_descriptor[] = {
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0xa2, 0x00,
    0x14, 0x00, 0x03, 0x00,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x04, 0x00, 0x07, 0x00, 0x2a, 0x00,
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    0x50, 0x00,
    '{', 0, 'A', 0, '1', 0, 'D', 0, '4', 0, '9', 0, '3', 0, '5', 0, 'B', 0,
    '-', 0, '4', 0, 'D', 0, '7', 0, '6', 0,
    '-', 0, '4', 0, 'E', 0, '6', 0, 'F', 0,
    '-', 0, 'B', 0, '6', 0, '2', 0, '3', 0,
    '-', 0, '6', 0, 'C', 0, '8', 0, '5', 0, '6', 0, 'A', 0, '6', 0, '7', 0,
    'A', 0, '8', 0, 'E', 0, '9', 0, '}', 0, 0, 0, 0, 0
};

static const uint8_t language_descriptor[] = {
    0x04, USB_DESCR_TYP_STRING, 0x09, 0x04
};
static const uint8_t manufacturer_descriptor[] = {
    0x20, USB_DESCR_TYP_STRING,
    'W', 0, 'C', 0, 'H', 0, ' ', 0,
    'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0, ' ', 0,
    'T', 0, 'o', 0, 'o', 0, 'l', 0
};
static const uint8_t product_descriptor[] = {
    0x2a, USB_DESCR_TYP_STRING,
    'W', 0, 'C', 0, 'H', 0, ' ', 0,
    'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0, ' ', 0,
    'T', 0, 'o', 0, 'o', 0, 'l', 0, ' ', 0,
    'C', 0, 'H', 0, '3', 0, '2', 0
};
static uint8_t serial_descriptor[36] = {
    36, USB_DESCR_TYP_STRING,
    'C', 0, 'H', 0, '2', 0, '0', 0, '8', 0
};
static const uint8_t vendor_descriptor[] = {
    0x20, USB_DESCR_TYP_STRING,
    'W', 0, 'C', 0, 'H', 0, ' ', 0,
    'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0, ' ', 0,
    'T', 0, 'o', 0, 'o', 0, 'l', 0
};

typedef char device_descriptor_size_check[
    sizeof(device_descriptor) == 0x12 ? 1 : -1];
typedef char configuration_descriptor_size_check[
    sizeof(configuration_descriptor) == 0x20 ? 1 : -1];
typedef char bos_descriptor_size_check[
    sizeof(bos_descriptor) == 0x21 ? 1 : -1];
typedef char ms_os_descriptor_size_check[
    sizeof(ms_os_20_descriptor) == 0xa2 ? 1 : -1];
typedef char product_descriptor_size_check[
    sizeof(product_descriptor) == 0x2a ? 1 : -1];

__attribute__((aligned(4))) static uint8_t endpoint0_buffer[USB_PACKET_SIZE];
__attribute__((aligned(4))) static uint8_t endpoint7_buffer[USB_PACKET_SIZE * 2U];

static volatile uint8_t usb_configuration;
static volatile uint8_t vendor_out_length;
static volatile uint8_t vendor_in_busy;
static uint8_t pending_address;
static const uint8_t *control_data;
static uint16_t control_remaining;
static uint8_t control_status_out;
static uint32_t activity_count;

static void build_serial_descriptor(void)
{
    static const uint8_t hex[] = "0123456789ABCDEF";
    uint8_t mac[6];
    uint8_t index;

    FLASH_GetMACAddress(mac);
    for(index = 0; index < 6U; index++)
    {
        serial_descriptor[12U + index * 4U] = hex[mac[index] >> 4];
        serial_descriptor[14U + index * 4U] = hex[mac[index] & 0x0fU];
    }
}

static void load_control_packet(void)
{
    uint8_t length = control_remaining > USB_PACKET_SIZE
                   ? USB_PACKET_SIZE : (uint8_t)control_remaining;
    memcpy(endpoint0_buffer, control_data, length);
    control_data += length;
    control_remaining -= length;
    USBFSD->UEP0_TX_LEN = length;
}

static void control_in(const uint8_t *data, uint16_t length,
                       uint16_t requested)
{
    control_status_out = 1;
    control_data = data;
    control_remaining = length < requested ? length : requested;
    load_control_packet();
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
}

static void control_status(void)
{
    control_status_out = 0;
    control_remaining = 0;
    USBFSD->UEP0_TX_LEN = 0;
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
}

static void control_stall(void)
{
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL;
}

static void handle_standard_request(PUSB_SETUP_REQ setup)
{
    static const uint8_t zero_status[2] = {0, 0};
    static const uint8_t zero_interface = 0;
    const uint8_t *descriptor = NULL;
    uint16_t descriptor_length = 0;

    switch(setup->bRequest)
    {
        case USB_GET_DESCRIPTOR:
            switch(setup->wValue >> 8)
            {
                case USB_DESCR_TYP_DEVICE: descriptor = device_descriptor; descriptor_length = sizeof(device_descriptor); break;
                case USB_DESCR_TYP_CONFIG: descriptor = configuration_descriptor; descriptor_length = sizeof(configuration_descriptor); break;
                case USB_DESCR_TYP_BOS: descriptor = bos_descriptor; descriptor_length = sizeof(bos_descriptor); break;
                case USB_DESCR_TYP_STRING:
                    switch(setup->wValue & 0xffU)
                    {
                        case 0: descriptor = language_descriptor; descriptor_length = sizeof(language_descriptor); break;
                        case 1: descriptor = manufacturer_descriptor; descriptor_length = sizeof(manufacturer_descriptor); break;
                        case 2: descriptor = product_descriptor; descriptor_length = sizeof(product_descriptor); break;
                        case 3: descriptor = serial_descriptor; descriptor_length = sizeof(serial_descriptor); break;
                        case 4: descriptor = vendor_descriptor; descriptor_length = sizeof(vendor_descriptor); break;
                    }
                    break;
            }
            if(descriptor)
                control_in(descriptor, descriptor_length, setup->wLength);
            else
                control_stall();
            break;
        case USB_SET_ADDRESS:
            pending_address = setup->wValue & 0x7fU;
            control_status();
            break;
        case USB_SET_CONFIGURATION:
            usb_configuration = setup->wValue & 0xffU;
            control_status();
            break;
        case USB_GET_CONFIGURATION:
            control_in((const uint8_t *)&usb_configuration, 1U, setup->wLength);
            break;
        case USB_GET_INTERFACE:
            control_in(&zero_interface, 1U, setup->wLength);
            break;
        case USB_GET_STATUS:
            control_in(zero_status, sizeof(zero_status), setup->wLength);
            break;
        case USB_CLEAR_FEATURE:
        case USB_SET_INTERFACE:
            control_status();
            break;
        default:
            control_stall();
            break;
    }
}

static void handle_setup(void)
{
    PUSB_SETUP_REQ setup = (PUSB_SETUP_REQ)endpoint0_buffer;
    pending_address = 0;
    control_status_out = 0;
    if((setup->bRequestType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD)
        handle_standard_request(setup);
    else if((setup->bRequestType & USB_REQ_TYP_MASK) == USB_REQ_TYP_VENDOR &&
            (setup->bRequestType & USB_REQ_TYP_IN) &&
            setup->bRequest == USB_MS_OS_VENDOR_CODE &&
            setup->wIndex == USB_MS_OS_DESCRIPTOR)
        control_in(ms_os_20_descriptor, sizeof(ms_os_20_descriptor), setup->wLength);
    else
        control_stall();
}

static void handle_control_in(void)
{
    if(pending_address)
    {
        USBFSD->DEV_ADDR = pending_address;
        pending_address = 0;
    }
    if(control_remaining)
    {
        load_control_packet();
        USBFSD->UEP0_TX_CTRL ^= USBFS_UEP_T_TOG;
    }
    else
    {
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG |
            (control_status_out ? USBFS_UEP_R_RES_ACK : USBFS_UEP_R_RES_NAK);
        USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK;
    }
}

static void reset_endpoints(void)
{
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP7_RX_CTRL = USBFS_UEP_AUTO_TOG | USBFS_UEP_R_RES_ACK;
    USBFSD->UEP7_TX_CTRL = USBFS_UEP_AUTO_TOG | USBFS_UEP_T_RES_NAK;
    USBFSD->DEV_ADDR = 0;
    usb_configuration = 0;
    vendor_out_length = 0;
    vendor_in_busy = 0;
    pending_address = 0;
    control_remaining = 0;
}

void UsbDevice_Init(void)
{
    RCC_ClocksTypeDef clocks = {0};
    build_serial_descriptor();
    RCC_GetClocksFreq(&clocks);
    if(clocks.SYSCLK_Frequency == 96000000U)
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2);
    else if(clocks.SYSCLK_Frequency == 48000000U)
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);
    else if(clocks.SYSCLK_Frequency == 144000000U)
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
    USBFSD->BASE_CTRL = 0;
    USBFSD->UEP4_1_MOD = 0;
    USBFSD->UEP2_3_MOD = 0;
    USBFSD->UEP5_6_MOD = 0;
    USBFSD->UEP7_MOD = USBFS_UEP7_RX_EN | USBFS_UEP7_TX_EN;
    USBFSD->UEP0_DMA = (uint32_t)endpoint0_buffer;
    USBFSD->UEP7_DMA = (uint32_t)endpoint7_buffer;
    reset_endpoints();
    USBFSD->INT_FG = 0xff;
    USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
    NVIC_EnableIRQ(USBFS_IRQn);
}

void UsbDevice_Disable(void)
{
    NVIC_DisableIRQ(USBFS_IRQn);
    USBFSD->INT_EN = 0;
    USBFSD->BASE_CTRL = 0;
    USBFSD->UDEV_CTRL = 0;
    usb_configuration = 0;
}

uint8_t UsbDevice_IsConfigured(void) { return usb_configuration != 0U; }
uint32_t UsbDevice_GetActivityCount(void) { return activity_count; }

uint8_t UsbDevice_VendorRead(uint8_t *data, uint8_t capacity)
{
    uint8_t length = vendor_out_length;
    if(length == 0U)
        return 0;
    if(length > capacity)
        length = capacity;
    memcpy(data, endpoint7_buffer, length);
    vendor_out_length = 0;
    USBFSD->UEP7_RX_CTRL = (USBFSD->UEP7_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
    return length;
}

uint8_t UsbDevice_VendorWrite(const uint8_t *data, uint8_t length)
{
    if(!usb_configuration || vendor_in_busy || length > USB_PACKET_SIZE)
        return 0;
    memcpy(endpoint7_buffer + USB_PACKET_SIZE, data, length);
    USBFSD->UEP7_TX_LEN = length;
    USBFSD->UEP7_TX_CTRL = (USBFSD->UEP7_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
    vendor_in_busy = 1;
    return 1;
}

void UsbDevice_Task(void) {}

__attribute__((interrupt("WCH-Interrupt-fast")))
void USBFS_IRQHandler(void)
{
    uint8_t flags = USBFSD->INT_FG;
    if(flags & USBFS_UIF_TRANSFER)
    {
        uint8_t status = USBFSD->INT_ST;
        uint8_t endpoint = status & USBFS_UIS_ENDP_MASK;
        if((status & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_SETUP)
            handle_setup();
        else if(endpoint == 0U && (status & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_IN)
            handle_control_in();
        else if(endpoint == 0U && (status & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_OUT)
        {
            USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;
            USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK;
        }
        else if(endpoint == 7U && (status & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_OUT && (status & USBFS_UIS_TOG_OK))
        {
            vendor_out_length = USBFSD->RX_LEN;
            USBFSD->UEP7_RX_CTRL = (USBFSD->UEP7_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
            activity_count++;
        }
        else if(endpoint == 7U && (status & USBFS_UIS_TOKEN_MASK) == USBFS_UIS_TOKEN_IN)
        {
            USBFSD->UEP7_TX_CTRL = (USBFSD->UEP7_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
            vendor_in_busy = 0;
            activity_count++;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    }
    if(flags & USBFS_UIF_BUS_RST)
    {
        reset_endpoints();
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    }
    if(flags & USBFS_UIF_SUSPEND)
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
}
