#include "CH57x_common.h"

#include "usb_device.h"

#include <string.h>

#define USB_PACKET_SIZE       64U
#define USB_DESCRIPTOR_BOS    0x0f
#define USB_MS_OS_VENDOR_CODE 0x20
#define USB_MS_OS_DESCRIPTOR  0x0007

static const uint8_t device_descriptor[] = {
    0x12, USB_DESCR_TYP_DEVICE,
    0x10, 0x02,
    0x00, 0x00, 0x00,
    USB_PACKET_SIZE,
    0x86, 0x1a,
    0x2e, 0x57,
    0x06, 0x01,
    0x01, 0x02, 0x03,
    0x01
};

static const uint8_t configuration_descriptor[] = {
    0x09, USB_DESCR_TYP_CONFIG, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, USB_DESCR_TYP_INTERF, 0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x04,
    0x07, USB_DESCR_TYP_ENDP, 0x07, 0x02, USB_PACKET_SIZE, 0x00, 0x00,
    0x07, USB_DESCR_TYP_ENDP, 0x87, 0x02, USB_PACKET_SIZE, 0x00, 0x00
};

static const uint8_t bos_descriptor[] = {
    0x05, USB_DESCRIPTOR_BOS, 0x21, 0x00, 0x01,
    0x1c, 0x10, 0x05, 0x00,
    0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c,
    0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,
    0x00, 0x00, 0x03, 0x06,
    0xa2, 0x00,
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
    0x14, USB_DESCR_TYP_STRING,
    'U', 0, 'A', 0, 'R', 0, 'T', 0, ' ', 0, 'T', 0, 'o', 0, 'o', 0, 'l', 0
};
static const uint8_t product_descriptor[] = {
    0x20, USB_DESCR_TYP_STRING,
    'W', 0, 'C', 0, 'H', 0, ' ', 0,
    'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0, ' ', 0,
    'T', 0, 'o', 0, 'o', 0, 'l', 0
};
static uint8_t serial_descriptor[38] = {
    38, USB_DESCR_TYP_STRING,
    'C', 0, 'H', 0, '5', 0, '7', 0, '2', 0, 'Q', 0
};
static const uint8_t vendor_descriptor[] = {
    0x20, USB_DESCR_TYP_STRING,
    'W', 0, 'C', 0, 'H', 0, ' ', 0,
    'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0, ' ', 0,
    'T', 0, 'o', 0, 'o', 0, 'l', 0
};

__attribute__((aligned(4))) static uint8_t endpoint0_buffer[USB_PACKET_SIZE];
__attribute__((aligned(4))) static uint8_t endpoint7_buffer[USB_PACKET_SIZE * 2U];

static volatile uint8_t usb_configuration;
static volatile uint8_t pending_address;
static volatile uint8_t vendor_out_length;
static const uint8_t *control_data;
static uint16_t control_remaining;
static uint8_t control_status_out;
static uint32_t activity_count;

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

static void control_stall(void)
{
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                   UEP_R_RES_STALL | UEP_T_RES_STALL;
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
    control_status_out = 0;

    switch(setup->bRequestType & USB_REQ_TYP_MASK)
    {
        case USB_REQ_TYP_STANDARD:
            handle_standard_request(setup);
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
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                   UEP_R_RES_NAK | UEP_T_RES_NAK;
}

static void reset_endpoints(void)
{
    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP7_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_USB_DEV_AD = 0;
    usb_configuration = 0;
    pending_address = 0;
    control_status_out = 0;
    control_remaining = 0;
    vendor_out_length = 0;
}

static void hardware_init(void)
{
    R8_USB_CTRL = 0;
    R8_UEP4_1_MOD = 0;
    R8_UEP2_3_MOD = 0;
    R8_UEP567_MOD = RB_UEP7_RX_EN | RB_UEP7_TX_EN;

    R16_UEP0_DMA = (uint16_t)(uint32_t)endpoint0_buffer;
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
    build_serial_descriptor();
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

static void process_interrupts(void)
{
    uint8_t status;

    if(R8_USB_INT_FG & RB_UIF_TRANSFER)
    {
        status = R8_USB_INT_ST;
        if(status & RB_UIS_SETUP_ACT)
        {
            activity_count++;
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
        activity_count++;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }

    if(R8_USB_INT_FG & RB_UIF_SUSPEND)
        R8_USB_INT_FG = RB_UIF_SUSPEND;
}

void UsbDevice_Task(void)
{
    process_interrupts();
}
