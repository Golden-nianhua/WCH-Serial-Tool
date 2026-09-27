#include "CH57x_common.h"

#include "uart_bridge.h"
#include "usb_device.h"

#include <string.h>

#define USB_PACKET_SIZE       64U
#define UART_USB_FLUSH_MS      1U
#define USB_CDC_PORTS          2U
#define USB_DESCRIPTOR_BOS     0x0f
#define USB_MS_OS_VENDOR_CODE  0x20
#define USB_MS_OS_DESCRIPTOR   0x0007

#define CDC_SET_LINE_CODING          0x20
#define CDC_GET_LINE_CODING          0x21
#define CDC_SET_CONTROL_LINE_STATE   0x22
#define CDC_SEND_BREAK               0x23

#define CDC0_CONTROL_INTERFACE 0
#define CDC1_CONTROL_INTERFACE 2

typedef struct __attribute__((packed))
{
    uint32_t baud_rate;
    uint8_t stop_bits;
    uint8_t parity;
    uint8_t data_bits;
} cdc_line_coding_t;

typedef struct
{
    volatile uint8_t length;
    volatile uint8_t index;
} usb_out_transfer_t;

static const uint8_t device_descriptor[] = {
    0x12, USB_DESCR_TYP_DEVICE,
    0x10, 0x02,
    0xef, 0x02, 0x01,
    USB_PACKET_SIZE,
    0x86, 0x1a,
    0x2d, 0x57,
    0x02, 0x01,
    0x01, 0x02, 0x03,
    0x01
};

/* CDC0 uses EP1, CDC1 uses EP2, and WinUSB configuration uses EP7. */
static const uint8_t configuration_descriptor[] = {
    0x09, USB_DESCR_TYP_CONFIG, 0x96, 0x00, 0x05, 0x01, 0x00, 0x80, 0x32,

    0x08, 0x0b, 0x00, 0x02, 0x02, 0x02, 0x01, 0x04,
    0x09, USB_DESCR_TYP_INTERF, 0x00, 0x00, 0x00, 0x02, 0x02, 0x01, 0x04,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,
    0x09, USB_DESCR_TYP_INTERF, 0x01, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x04,
    0x07, USB_DESCR_TYP_ENDP, 0x01, 0x02, USB_PACKET_SIZE, 0x00, 0x00,
    0x07, USB_DESCR_TYP_ENDP, 0x81, 0x02, USB_PACKET_SIZE, 0x00, 0x00,

    0x08, 0x0b, 0x02, 0x02, 0x02, 0x02, 0x01, 0x05,
    0x09, USB_DESCR_TYP_INTERF, 0x02, 0x00, 0x00, 0x02, 0x02, 0x01, 0x05,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x03,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x02, 0x03,
    0x09, USB_DESCR_TYP_INTERF, 0x03, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x05,
    0x07, USB_DESCR_TYP_ENDP, 0x02, 0x02, USB_PACKET_SIZE, 0x00, 0x00,
    0x07, USB_DESCR_TYP_ENDP, 0x82, 0x02, USB_PACKET_SIZE, 0x00, 0x00,

    0x09, USB_DESCR_TYP_INTERF, 0x04, 0x00, 0x02, 0xff, 0x00, 0x00, 0x06,
    0x07, USB_DESCR_TYP_ENDP, 0x07, 0x02, USB_PACKET_SIZE, 0x00, 0x00,
    0x07, USB_DESCR_TYP_ENDP, 0x87, 0x02, USB_PACKET_SIZE, 0x00, 0x00
};

static const uint8_t bos_descriptor[] = {
    0x05, USB_DESCRIPTOR_BOS, 0x21, 0x00, 0x01,
    0x1c, 0x10, 0x05, 0x00,
    0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c,
    0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,
    0x00, 0x00, 0x03, 0x06,
    0xb2, 0x00,
    USB_MS_OS_VENDOR_CODE, 0x00
};

static const uint8_t ms_os_20_descriptor[] = {
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0xb2, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0xa8, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x04, 0x00, 0xa0, 0x00,
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
    0x14, USB_DESCR_TYP_STRING,
    'U', 0, 'A', 0, 'R', 0, 'T', 0, ' ', 0, 'T', 0, 'o', 0, 'o', 0, 'l', 0
};
static const uint8_t product_descriptor[] = {
    0x22, USB_DESCR_TYP_STRING,
    'C', 0, 'H', 0, '5', 0, '7', 0, '2', 0, 'Q', 0, ' ', 0,
    'U', 0, 'A', 0, 'R', 0, 'T', 0, ' ', 0, 'T', 0, 'o', 0, 'o', 0, 'l', 0
};
static uint8_t serial_descriptor[38] = {
    38, USB_DESCR_TYP_STRING,
    'C', 0, 'H', 0, '5', 0, '7', 0, '2', 0, 'Q', 0
};
static const uint8_t uart0_descriptor[] = {
    0x1c, USB_DESCR_TYP_STRING,
    'P', 0, 'h', 0, 'y', 0, 's', 0, 'i', 0, 'c', 0, 'a', 0, 'l', 0,
    ' ', 0, 'U', 0, 'A', 0, 'R', 0, 'T', 0
};
static const uint8_t uart1_descriptor[] = {
    0x14, USB_DESCR_TYP_STRING,
    '2', 0, '.', 0, '4', 0, 'G', 0, ' ', 0, 'U', 0, 'A', 0, 'R', 0, 'T', 0
};
static const uint8_t vendor_descriptor[] = {
    0x1c, USB_DESCR_TYP_STRING,
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, ' ', 0,
    'C', 0, 'o', 0, 'n', 0, 'f', 0, 'i', 0, 'g', 0
};

__attribute__((aligned(4))) static uint8_t endpoint0_buffer[USB_PACKET_SIZE];
__attribute__((aligned(4))) static uint8_t endpoint1_buffer[USB_PACKET_SIZE * 2U];
__attribute__((aligned(4))) static uint8_t endpoint2_buffer[USB_PACKET_SIZE * 2U];
__attribute__((aligned(4))) static uint8_t endpoint7_buffer[USB_PACKET_SIZE * 2U];

static uint32_t uart_rx_time;
static uint32_t uart_rx_event;
static uint8_t uart_zlp_pending;
static usb_out_transfer_t usb_out[USB_CDC_PORTS];
static cdc_line_coding_t line_coding[USB_CDC_PORTS];
static volatile uint8_t line_coding_pending[USB_CDC_PORTS];
static uint16_t control_line_state[USB_CDC_PORTS];
static volatile uint8_t usb_configuration;
static volatile uint8_t pending_address;
static volatile uint8_t vendor_out_length;
static const uint8_t *control_data;
static uint16_t control_remaining;
static uint8_t control_out_request;
static uint8_t control_port;
static uint8_t control_status_out;
static uint32_t activity_count;

static uint32_t millis(void)
{
    return SYS_GetSysTickCnt() / (GetSysClock() / 1000U);
}

static void build_serial_descriptor(void)
{
    static const uint8_t hex[] = "0123456789ABCDEF";
    __attribute__((aligned(4))) uint8_t mac[8];
    uint8_t index;

    GetMACAddress(mac);
    for(index = 0; index < 6U; index++)
    {
        serial_descriptor[14U + index * 4U] = hex[mac[index] >> 4];
        serial_descriptor[16U + index * 4U] = hex[mac[index] & 0x0fU];
    }
}

__HIGH_CODE
static uint8_t *out_buffer(uint8_t port)
{
    return port == 0U ? endpoint1_buffer : endpoint2_buffer;
}

__HIGH_CODE
static volatile uint8_t *endpoint_control(uint8_t port)
{
    return port == 0U ? &R8_UEP1_CTRL : &R8_UEP2_CTRL;
}

__HIGH_CODE
static void accept_out(uint8_t port)
{
    volatile uint8_t *control = endpoint_control(port);
    *control = (*control & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
}

static void load_control_packet(void)
{
    uint8_t length = control_remaining > USB_PACKET_SIZE
                   ? USB_PACKET_SIZE : (uint8_t)control_remaining;

    memcpy(endpoint0_buffer, control_data, length);
    control_data += length;
    control_remaining -= length;
    R8_UEP0_T_LEN = length;
}

static void control_in(const uint8_t *data, uint16_t length, uint16_t requested)
{
    control_status_out = 1;
    control_data = data;
    control_remaining = length < requested ? length : requested;
    load_control_packet();
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                   UEP_R_RES_ACK | UEP_T_RES_ACK;
}

static void control_status(void)
{
    control_status_out = 0;
    control_remaining = 0;
    R8_UEP0_T_LEN = 0;
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                   UEP_R_RES_NAK | UEP_T_RES_ACK;
}

static void control_out(void)
{
    R8_UEP0_T_LEN = 0;
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                   UEP_R_RES_ACK | UEP_T_RES_NAK;
}

static void control_stall(void)
{
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                   UEP_R_RES_STALL | UEP_T_RES_STALL;
}

static uint8_t port_from_interface(uint16_t interface)
{
    if(interface == CDC0_CONTROL_INTERFACE)
        return 0;
    if(interface == CDC1_CONTROL_INTERFACE)
        return 1;
    return 0xff;
}

static uint8_t baud_supported(uint8_t port, uint32_t baud)
{
    uint32_t actual;
    uint32_t error;

    if(baud == 0U)
        return 0;
    if(port == 1U)
        return baud <= 1500000U;
    if(baud <= 1000000U)
        return 1;
    if(baud > 12000000U)
        return 0;
    if(baud == 12000000U || baud == 6000000U)
        return 1;

    actual = UartBridge_CalculateBaud(baud);
    error = actual > baud ? actual - baud : baud - actual;
    return error * 100U <= baud * 2U;
}

static void handle_class_request(PUSB_SETUP_REQ setup)
{
    uint8_t port = port_from_interface(setup->wIndex);

    if(port >= USB_CDC_PORTS)
    {
        control_stall();
        return;
    }

    switch(setup->bRequest)
    {
        case CDC_SET_LINE_CODING:
            control_out_request = CDC_SET_LINE_CODING;
            control_port = port;
            control_out();
            break;

        case CDC_GET_LINE_CODING:
            control_in((const uint8_t *)&line_coding[port],
                       sizeof(cdc_line_coding_t), setup->wLength);
            break;

        case CDC_SET_CONTROL_LINE_STATE:
            control_line_state[port] = setup->wValue;
            if(port == 1U)
                line_coding_pending[1] = 1;
            control_status();
            break;

        case CDC_SEND_BREAK:
            if(port == 0U)
                UartBridge_SetBreak(setup->wValue != 0U);
            control_status();
            break;

        default:
            control_stall();
            break;
    }
}

static void handle_vendor_request(PUSB_SETUP_REQ setup)
{
    if((setup->bRequestType & USB_REQ_TYP_IN) &&
       setup->bRequest == USB_MS_OS_VENDOR_CODE &&
       setup->wIndex == USB_MS_OS_DESCRIPTOR)
    {
        control_in(ms_os_20_descriptor, sizeof(ms_os_20_descriptor), setup->wLength);
    }
    else
    {
        control_stall();
    }
}

static void handle_standard_request(PUSB_SETUP_REQ setup)
{
    static const uint8_t zero_status[2] = {0, 0};
    static const uint8_t zero_interface = 0;
    const uint8_t *descriptor = 0;
    uint16_t descriptor_length = 0;

    switch(setup->bRequest)
    {
        case USB_GET_DESCRIPTOR:
            switch(setup->wValue >> 8)
            {
                case USB_DESCR_TYP_DEVICE:
                    descriptor = device_descriptor;
                    descriptor_length = sizeof(device_descriptor);
                    break;
                case USB_DESCR_TYP_CONFIG:
                    descriptor = configuration_descriptor;
                    descriptor_length = sizeof(configuration_descriptor);
                    break;
                case USB_DESCRIPTOR_BOS:
                    descriptor = bos_descriptor;
                    descriptor_length = sizeof(bos_descriptor);
                    break;
                case USB_DESCR_TYP_STRING:
                    switch(setup->wValue & 0xff)
                    {
                        case 0: descriptor = language_descriptor; descriptor_length = sizeof(language_descriptor); break;
                        case 1: descriptor = manufacturer_descriptor; descriptor_length = sizeof(manufacturer_descriptor); break;
                        case 2: descriptor = product_descriptor; descriptor_length = sizeof(product_descriptor); break;
                        case 3: descriptor = serial_descriptor; descriptor_length = sizeof(serial_descriptor); break;
                        case 4: descriptor = uart0_descriptor; descriptor_length = sizeof(uart0_descriptor); break;
                        case 5: descriptor = uart1_descriptor; descriptor_length = sizeof(uart1_descriptor); break;
                        case 6: descriptor = vendor_descriptor; descriptor_length = sizeof(vendor_descriptor); break;
                    }
                    break;
            }
            if(descriptor)
                control_in(descriptor, descriptor_length, setup->wLength);
            else
                control_stall();
            break;

        case USB_SET_ADDRESS:
            pending_address = setup->wValue & 0x7f;
            control_status();
            break;

        case USB_SET_CONFIGURATION:
            usb_configuration = setup->wValue & 0xff;
            control_status();
            break;

        case USB_GET_CONFIGURATION:
            control_in((const uint8_t *)&usb_configuration, 1, setup->wLength);
            break;

        case USB_GET_INTERFACE:
            control_in(&zero_interface, 1, setup->wLength);
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
    control_out_request = 0;
    control_status_out = 0;

    switch(setup->bRequestType & USB_REQ_TYP_MASK)
    {
        case USB_REQ_TYP_STANDARD:
            handle_standard_request(setup);
            break;
        case USB_REQ_TYP_CLASS:
            handle_class_request(setup);
            break;
        case USB_REQ_TYP_VENDOR:
            handle_vendor_request(setup);
            break;
        default:
            control_stall();
            break;
    }
}

static void handle_control_in(void)
{
    if(pending_address)
    {
        R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | pending_address;
        pending_address = 0;
    }

    if(control_remaining)
    {
        load_control_packet();
        R8_UEP0_CTRL ^= RB_UEP_T_TOG;
    }
    else
    {
        R8_UEP0_T_LEN = 0;
        R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                       (control_status_out ? UEP_R_RES_ACK : UEP_R_RES_NAK) |
                       UEP_T_RES_NAK;
    }
}

static void handle_control_out(void)
{
    if(control_out_request == CDC_SET_LINE_CODING &&
       R8_USB_RX_LEN == sizeof(cdc_line_coding_t))
    {
        cdc_line_coding_t requested;

        memcpy(&requested, endpoint0_buffer, sizeof(requested));
        if(!baud_supported(control_port, requested.baud_rate))
        {
            control_out_request = 0;
            control_stall();
            return;
        }
        line_coding[control_port] = requested;
        line_coding_pending[control_port] = 1;
        control_out_request = 0;
        control_status();
    }
    else
    {
        R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                       UEP_R_RES_NAK | UEP_T_RES_NAK;
    }
}

static void reset_endpoints(void)
{
    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP7_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_USB_DEV_AD = 0;
    usb_configuration = 0;
    pending_address = 0;
    control_out_request = 0;
    control_status_out = 0;
    control_remaining = 0;
    usb_out[0].length = 0;
    usb_out[1].length = 0;
    uart_zlp_pending = 0;
    vendor_out_length = 0;
    line_coding_pending[0] = 0;
    line_coding_pending[1] = 0;
    control_line_state[0] = 0;
    control_line_state[1] = 0;
}

static void hardware_init(void)
{
    R8_USB_CTRL = 0;
    R8_UEP4_1_MOD = RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN;
    R8_UEP567_MOD = RB_UEP7_RX_EN | RB_UEP7_TX_EN;

    R16_UEP0_DMA = (uint16_t)(uint32_t)endpoint0_buffer;
    R16_UEP1_DMA = (uint16_t)(uint32_t)endpoint1_buffer;
    R16_UEP2_DMA = (uint16_t)(uint32_t)endpoint2_buffer;
    R16_UEP7_DMA = (uint16_t)(uint32_t)endpoint7_buffer;

    reset_endpoints();
    R8_UDEV_CTRL = RB_UD_PD_DIS;
    R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R16_PIN_ALTERNATE |= RB_PIN_USB_EN | RB_UDP_PU_EN;
    R8_USB_INT_FG = 0xff;
    R8_USB_INT_EN = 0;
    PFIC_DisableIRQ(USB_IRQn);
    R8_UDEV_CTRL |= RB_UD_PORT_EN;
}

void UsbDevice_Init(void)
{
    uint8_t port;

    build_serial_descriptor();
    memset(usb_out, 0, sizeof(usb_out));
    for(port = 0; port < USB_CDC_PORTS; port++)
    {
        line_coding[port].baud_rate = 115200U;
        line_coding[port].stop_bits = 0;
        line_coding[port].parity = 0;
        line_coding[port].data_bits = 8;
    }
    uart_rx_time = millis();
    uart_rx_event = UartBridge_GetRxEventCount();
    activity_count = 0;
    hardware_init();
}

void UsbDevice_Disable(void)
{
    R8_USB_INT_EN = 0;
    PFIC_DisableIRQ(USB_IRQn);
    R8_USB_CTRL = 0;
    R8_UDEV_CTRL = 0;
    R16_PIN_ALTERNATE &= ~(RB_PIN_USB_EN | RB_UDP_PU_EN);
    usb_configuration = 0;
}

uint8_t UsbDevice_IsConfigured(void)
{
    return usb_configuration != 0;
}

uint32_t UsbDevice_GetActivityCount(void)
{
    return activity_count;
}

uint8_t UsbDevice_VendorRead(uint8_t *data, uint8_t capacity)
{
    uint8_t length = vendor_out_length;

    if(length == 0U)
        return 0;
    if(length > capacity)
        length = capacity;

    memcpy(data, endpoint7_buffer, length);
    vendor_out_length = 0;
    R8_UEP7_CTRL = (R8_UEP7_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
    return length;
}

uint8_t UsbDevice_VendorWrite(const uint8_t *data, uint8_t length)
{
    if(length > USB_PACKET_SIZE ||
       (R8_UEP7_CTRL & MASK_UEP_T_RES) != UEP_T_RES_NAK)
        return 0;

    memcpy(endpoint7_buffer + USB_PACKET_SIZE, data, length);
    R8_UEP7_T_LEN = length;
    R8_UEP7_CTRL = (R8_UEP7_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    return 1;
}

__HIGH_CODE
uint8_t UsbDevice_RfRead(uint8_t *data, uint8_t capacity)
{
    uint8_t length = usb_out[1].length;
    uint8_t index;

    if(length == 0U)
        return 0;
    if(length > capacity)
        length = capacity;

    for(index = 0; index < length; index++)
        data[index] = endpoint2_buffer[index];
    usb_out[1].length = 0;
    usb_out[1].index = 0;
    accept_out(1);
    return length;
}

uint8_t UsbDevice_RfWrite(const uint8_t *data, uint8_t length)
{
    if(!usb_configuration || length > USB_PACKET_SIZE ||
       (R8_UEP2_CTRL & MASK_UEP_T_RES) != UEP_T_RES_NAK)
        return 0;

    memcpy(endpoint2_buffer + USB_PACKET_SIZE, data, length);
    R8_UEP2_T_LEN = length;
    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    return 1;
}

__HIGH_CODE
uint8_t UsbDevice_RfLineCoding(uint32_t *baud, uint8_t *stop_bits,
                               uint8_t *parity, uint8_t *data_bits,
                               uint8_t *io_status)
{
    if(!line_coding_pending[1])
        return 0;

    line_coding_pending[1] = 0;
    *baud = line_coding[1].baud_rate;
    *stop_bits = line_coding[1].stop_bits;
    *parity = line_coding[1].parity;
    *data_bits = line_coding[1].data_bits;
    *io_status = 0;
    if(control_line_state[1] & 0x0001U)
        *io_status |= 0x20U;
    if(control_line_state[1] & 0x0002U)
        *io_status |= 0x40U;
    return 1;
}

static void send_uart_to_usb(void)
{
    uint32_t event = UartBridge_GetRxEventCount();
    uint16_t available;
    uint8_t length = 0;

    if(event != uart_rx_event)
    {
        uart_rx_event = event;
        uart_rx_time = millis();
    }

    if((R8_UEP1_CTRL & MASK_UEP_T_RES) != UEP_T_RES_NAK)
        return;

    available = UartBridge_Available();
    if(available == 0U)
    {
        if(uart_zlp_pending &&
           (uint32_t)(millis() - uart_rx_time) >= UART_USB_FLUSH_MS)
        {
            R8_UEP1_T_LEN = 0;
            R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
            uart_zlp_pending = 0;
        }
        return;
    }

    if(available < USB_PACKET_SIZE &&
       (uint32_t)(millis() - uart_rx_time) < UART_USB_FLUSH_MS)
        return;

    while(length < USB_PACKET_SIZE &&
          UartBridge_ReadByte(&endpoint1_buffer[USB_PACKET_SIZE + length]))
        length++;

    if(length)
    {
        R8_UEP1_T_LEN = length;
        R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
        uart_zlp_pending = length == USB_PACKET_SIZE;
    }
}

static void write_usb_to_uart(uint8_t port)
{
    usb_out_transfer_t *transfer = &usb_out[port];
    uint8_t *buffer = out_buffer(port);

    if(transfer->length == 0U)
        return;

    if(port == 0U)
    {
        while(transfer->index < transfer->length &&
              UartBridge_WriteByte(buffer[transfer->index]))
            transfer->index++;
    }
    else
    {
        transfer->index = transfer->length;
    }

    if(transfer->index == transfer->length)
    {
        transfer->length = 0;
        transfer->index = 0;
        accept_out(port);
    }
}

static void handle_data_out(uint8_t port)
{
    if(R8_USB_RX_LEN)
    {
        usb_out[port].length = R8_USB_RX_LEN;
        usb_out[port].index = 0;
    }
    else
    {
        accept_out(port);
    }
}

static void process_interrupts(void)
{
    uint8_t status;

    if(R8_USB_INT_FG & RB_UIF_TRANSFER)
    {
        status = R8_USB_INT_ST;
        if(status & RB_UIS_SETUP_ACT)
        {
            handle_setup();
        }
        else
        {
            switch(status & (MASK_UIS_TOKEN | MASK_UIS_ENDP))
            {
                case UIS_TOKEN_IN:
                    handle_control_in();
                    break;
                case UIS_TOKEN_OUT:
                    handle_control_out();
                    break;
                case UIS_TOKEN_OUT | 1:
                    if(status & RB_UIS_TOG_OK)
                    {
                        R8_UEP1_CTRL = ((R8_UEP1_CTRL ^ RB_UEP_R_TOG) &
                                        ~MASK_UEP_R_RES) | UEP_R_RES_NAK;
                        handle_data_out(0);
                        activity_count++;
                    }
                    break;
                case UIS_TOKEN_IN | 1:
                    R8_UEP1_CTRL = ((R8_UEP1_CTRL ^ RB_UEP_T_TOG) &
                                    ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    activity_count++;
                    break;
                case UIS_TOKEN_OUT | 2:
                    if(status & RB_UIS_TOG_OK)
                    {
                        R8_UEP2_CTRL = ((R8_UEP2_CTRL ^ RB_UEP_R_TOG) &
                                        ~MASK_UEP_R_RES) | UEP_R_RES_NAK;
                        handle_data_out(1);
                        activity_count++;
                    }
                    break;
                case UIS_TOKEN_IN | 2:
                    R8_UEP2_CTRL = ((R8_UEP2_CTRL ^ RB_UEP_T_TOG) &
                                    ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    activity_count++;
                    break;
                case UIS_TOKEN_OUT | 7:
                    if((status & RB_UIS_TOG_OK) && R8_USB_RX_LEN)
                    {
                        vendor_out_length = R8_USB_RX_LEN;
                        R8_UEP7_CTRL = (R8_UEP7_CTRL & ~MASK_UEP_R_RES) |
                                       UEP_R_RES_NAK;
                        activity_count++;
                    }
                    break;
                case UIS_TOKEN_IN | 7:
                    R8_UEP7_CTRL = (R8_UEP7_CTRL & ~MASK_UEP_T_RES) |
                                   UEP_T_RES_NAK;
                    activity_count++;
                    break;
            }
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
    }

    if(R8_USB_INT_FG & RB_UIF_BUS_RST)
    {
        reset_endpoints();
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }

    if(R8_USB_INT_FG & RB_UIF_SUSPEND)
        R8_USB_INT_FG = RB_UIF_SUSPEND;
}

void UsbDevice_Task(void)
{
    uint8_t discard;

    process_interrupts();

    if(line_coding_pending[0])
    {
        line_coding_pending[0] = 0;
        if(line_coding[0].baud_rate)
            UartBridge_SetLineCoding(line_coding[0].baud_rate,
                                     line_coding[0].stop_bits,
                                     line_coding[0].parity,
                                     line_coding[0].data_bits);
    }

    write_usb_to_uart(0);

    if(usb_configuration)
        send_uart_to_usb();
    else
    {
        while(UartBridge_ReadByte(&discard))
        {
        }
        uart_rx_event = UartBridge_GetRxEventCount();
        uart_zlp_pending = 0;
    }
}
