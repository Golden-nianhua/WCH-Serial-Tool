"""Run the real RF state machines with mocked I/O under a RISC-V emulator.

uv run --with unicorn --with pyelftools tests/rf_reconnect_test.py
"""

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_SP, UC_RISCV_REG_GP, UC_RISCV_REG_PC


MOCK_HEADER = r"""
#ifndef MOCK_CH57X_COMMON_H
#define MOCK_CH57X_COMMON_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define __HIGH_CODE
#define __PACKED __attribute__((packed))
#define PRINT(...)
#define FREQ_SYS 24000000U
#define __MCPY(dst, src, end) memcpy(dst, src, (uint8_t *)(end) - (uint8_t *)(src))
typedef uint8_t BOOL;
typedef struct { uint32_t CNTL; } mock_tick_t;
extern mock_tick_t mock_tick;
#define SysTick (&mock_tick)
#define R8_UART_TFC 0
#define R8_CHIP_ID 0x72
#define R8_GLOB_CFG_INFO 0
#define RB_CFG_BOOT_EN 8
#define bUDM 1
#define bUDP 2
#define bTXD_0 8
#define bRXD_0 4
#define GPIO_ModeOut_PP_5mA 0
#define BLEL_IRQn 21
#define UART_IRQn 27
#define DISABLE 0
#define TMR_IT_CYC_END 1
#define PFIC_DisableIRQ(x) ((void)0)
#define PFIC_EnableIRQ(x) ((void)0)
#define PFIC_DisableAllIRQ() ((void)0)
#define PFIC_EnableAllIRQ() ((void)0)
#define PFIC_SetPendingIRQ(x) ((void)0)
#define GPIOA_SetBits(x) ((void)0)
#define GPIOA_ResetBits(x) ((void)0)
#define GPIOA_ModeCfg(x,y) ((void)0)
#define TMR_ITCfg(x,y) ((void)0)
#define GetSysClock() FREQ_SYS
#define SYS_GetSysTickCnt() mock_tick.CNTL
#define SYS_ResetExecute() ((void)0)
uint8_t FLASH_ROM_ERASE(uint32_t addr, uint32_t len);
uint8_t FLASH_ROM_WRITE(uint32_t addr, const void *data, uint32_t len);
void GetMACAddress(uint8_t *mac);
#endif
"""

HARNESS = r"""
#include "rf.h"
#include "rf_uart_rx.h"
#include "rf_uart_tx.h"
#include "rf_transport.h"
#include "rf_legacy_port.h"
#include "uart.h"
#include "usb_uart.h"

mock_tick_t mock_tick;
rfipTx_t gTxParam;
extern rfTxBuf_t rf_role_tx_buf;
extern uint8_t slaveBoundStatus, slaveDataSeq;
extern uint8_t slaveRxDataStatus, slaveRfStatus;
extern uint16_t slaveServerData, slaveTimeoutMax;
extern struct simple_buf rf_role_stream_buffer;
static const rfStatusCBs_t *callbacks;
static uint32_t tx_count, rx_count, uart_reads;
static uint32_t mock_tx_aa, mock_rx_aa;
static uint8_t usb_ready;
volatile uint32_t test_result;

uint8_t FLASH_ROM_ERASE(uint32_t a, uint32_t n)
{ memset((void *)a, 0xff, n); return 0; }
uint8_t FLASH_ROM_WRITE(uint32_t a, const void *p, uint32_t n)
{ memcpy((void *)a, p, n); return 0; }
void GetMACAddress(uint8_t *p) { memset(p, 1, 8); }
void RFRole_Init(void) {}
void RFRole_RegisterStatusCbs(const rfStatusCBs_t *p) { callbacks = p; }
bStatus_t RFRole_Stop(void) { return 0; }
bStatus_t RFRole_Shut(void) { return 0; }
void rf_tx_set_sync_word(uint32_t n) { mock_tx_aa = n; }
void rf_rx_set_sync_word(uint32_t n) { mock_rx_aa = n; }
void rf_tx_set_frequency(uint32_t n) {}
void rf_rx_set_frequency(uint32_t n) {}
void rf_tx_set_phy_type(uint8_t n) {}
void rf_rx_set_phy_type(uint8_t n) {}
void rf_tx_start(void *p, uint16_t n) { tx_count++; }
void rf_rx_start(uint32_t n) { rx_count++; }
uint16_t UartBridge_Available(void) { return 512; }
uint32_t UartBridge_GetRxEventCount(void) { return 512; }
uint32_t UartBridge_GetRxDropCount(void) { return 0; }
uint32_t UartBridge_GetLineErrorCount(void) { return 0; }
uint32_t UartBridge_GetBaud(void) { return 115200; }
uint8_t UartBridge_ReadByte(uint8_t *p) { uart_reads++; *p=0x55; return 1; }
uint8_t UartBridge_WriteByte(uint8_t p) { return 1; }
void UartBridge_SetLineCoding(uint32_t b, uint8_t s, uint8_t p, uint8_t d) {}
uint8_t UsbDevice_IsConfigured(void) { return 1; }
uint8_t UsbDevice_RfWrite(const uint8_t *p, uint8_t n) { return usb_ready; }
uint8_t UsbDevice_RfRead(uint8_t *p, uint8_t n) { return 0; }
uint8_t UsbDevice_RfLineCoding(uint32_t *b, uint8_t *s, uint8_t *p,
                             uint8_t *d, uint8_t *i) { return 0; }

#define CHECK(condition, code) do { if(!(condition)) { test_result=code; return; } } while(0)

static void request_pair(uint16_t id)
{
    uint8_t packet[32] __attribute__((aligned(4))) = {0};
    rfPackage_t *head = (rfPackage_t *)packet;
    bound_req_t *request = (bound_req_t *)(head + 1);
    head->type = PKT_CMD_BOUND_REQ;
    head->length = PKT_DATA_OFFSET + sizeof(*request);
    request->severData = id;
    request->interval = ADV_INTERVAL;
    /* Weak RSSI is valid for an already paired node. */
    packet[head->length + 4] = (uint8_t)-70;
    callbacks->pfnRxCB(head);
}

static void receiver_lost_response(void)
{
    RF_UartRxInit();
    slaveServerData = 0x382a;
    uint32_t before = tx_count;
    request_pair(0x382a);
    CHECK(tx_count == before + 1, 101);
    CHECK(slaveServerData == 0x382a, 102);
    callbacks->pfnTxCB();
    CHECK(slaveBoundStatus == BOUND_STATUS_WAIT, 103);
    /* Drop the response. The node is still on AA with its previous ID. */
    for(uint32_t i=0; i<=BOUND_EST_COUNT; i++)
    {
        callbacks->pfnTimeoutCB();
        RF_UartRxPoll();
    }
    CHECK(slaveBoundStatus == BOUND_STATUS_IDLE, 104);
    CHECK(mock_rx_aa == AA, 105);
    before = tx_count;
    request_pair(0x382a);
    CHECK(tx_count == before + 1, 106);
    CHECK(slaveServerData == 0x382a, 107);
    callbacks->pfnTxCB();
    uint8_t packet[12] __attribute__((aligned(4))) = {0};
    rfPackage_t *status = (rfPackage_t *)packet;
    status->type = PKT_CMD_GET_STATUS;
    status->length = PKT_DATA_OFFSET;
    status->seq = slaveDataSeq;
    callbacks->pfnRxCB(status);
    CHECK(slaveBoundStatus == BOUND_STATUS_EST, 108);
    before = tx_count;
    slaveBoundStatus = BOUND_STATUS_IDLE;
    request_pair(0x7777);
    CHECK(tx_count == before, 109);
}

static void blocked_usb_recovery(void)
{
    RfTransport_Init(RF_TRANSPORT_USB_RECEIVER, 1);
    slaveBoundStatus = BOUND_STATUS_EST;
    slaveTimeoutMax = 100;
    slaveRxDataStatus = DATA_STATUS_RCV;
    uint8_t payload[64] = {0x55};
    typeBufSize length = sizeof(payload);
    write_buf(&rf_role_stream_buffer, payload, &length);
    usb_ready = 0;
    RfTransport_Task();
    CHECK(RfTransport_GetPendingLength() == 64, 201);
    /* USB remains blocked across the RF supervision timeout. */
    uint32_t before = rx_count;
    for(uint32_t i=0; i<=100; i++)
    {
        callbacks->pfnTimeoutCB();
        RfTransport_Task();
    }
    CHECK(slaveBoundStatus == BOUND_STATUS_IDLE, 202);
    CHECK(mock_rx_aa == AA, 203);
    CHECK(rx_count > before, 204);
    CHECK(RfTransport_GetPendingLength() == 64, 205);
    usb_ready = 1;
    RfTransport_Task();
    CHECK(RfTransport_GetPendingLength() == 0, 206);
    /* A timeout must also not strand already acknowledged RF bytes. */
    length = sizeof(payload);
    write_buf(&rf_role_stream_buffer, payload, &length);
    slaveRxDataStatus = DATA_STATUS_TIMEOUT;
    RF_UartRxPoll();
    length = sizeof(payload);
    CHECK(RF_RxQuery(payload, &length) == 64, 207);
    CHECK(RF_RxGetQueueDepth() == 0, 208);
}

static void node_uart_reconnect(void)
{
    RfTransport_Init(RF_TRANSPORT_UART_NODE, 0);
    uint32_t before = tx_count;
    mock_tick.CNTL += FREQ_SYS / 1000 * 20;
    RF_StatusQuery();
    rfPackage_t *head = (rfPackage_t *)rf_role_tx_buf.TxBuf;
    CHECK(tx_count == before + 1, 301);
    CHECK(head->type == PKT_CMD_BOUND_REQ, 302);
    CHECK(uart_reads == 0, 303);
    /* A bound response moves to WAIT; UART must not starve confirmation. */
    uint8_t packet[32] __attribute__((aligned(4))) = {0};
    rfPackage_t *response = (rfPackage_t *)packet;
    bound_rsp_t *bound = (bound_rsp_t *)(response + 1);
    response->type = PKT_CMD_BOUND_RSP;
    response->length = PKT_DATA_OFFSET + sizeof(*bound);
    response->seq = head->seq;
    bound->interval = CONN_INTERVAL;
    bound->timeout = CONN_TIMEOUT;
    bound->severData = 0x382a;
    bound->accessaddr = 0x12345678;
    callbacks->pfnRxCB(response);
    CHECK(gBoundStatus == BOUND_STATUS_WAIT, 304);
    mock_tick.CNTL += FREQ_SYS / 1000 * 20;
    RF_StatusQuery();
    CHECK(head->type == PKT_CMD_GET_STATUS, 305);
    CHECK(uart_reads == 0, 306);
    /* Reach EST, then lose every response until the node falls back to AA. */
    response->type = PKT_CMD_RSP_STATUS;
    response->length = PKT_DATA_OFFSET;
    response->seq = head->seq;
    callbacks->pfnRxCB(response);
    CHECK(gBoundStatus == BOUND_STATUS_EST, 307);
    RF_StatusQuery();
    CHECK(head->type == PKT_DATA_FLAG, 308);
    for(uint32_t i=0; i<RESEND_COUNT+BOUND_EST_COUNT+110; i++)
    {
        callbacks->pfnTxCB();
        callbacks->pfnTimeoutCB();
        RF_StatusQuery();
        if(gBoundStatus == BOUND_STATUS_IDLE) break;
    }
    CHECK(gBoundStatus == BOUND_STATUS_IDLE, 309);
    CHECK(mock_tx_aa == AA, 310);
    uint32_t reads_before = uart_reads;
    mock_tick.CNTL += FREQ_SYS / 1000 * ADV_INTERVAL;
    RF_StatusQuery();
    CHECK(head->type == PKT_CMD_BOUND_REQ, 311);
    CHECK(uart_reads == reads_before, 312);
}

void _start(void)
{
    receiver_lost_response();
    if(!test_result) blocked_usb_recovery();
    if(!test_result) node_uart_reconnect();
    if(!test_result) test_result = 1;
    __asm__ volatile("ebreak");
    while(1);
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    toolchain = args.toolchain
    if toolchain is None:
        for version in ("3", "2"):
            candidate = Path(f"C:/MounRiver/MounRiver_Studio{version}/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC12/bin")
            if (candidate / "riscv-wch-elf-gcc.exe").exists():
                toolchain = candidate
                break
    if toolchain is None:
        parser.error("pass --toolchain with the WCH GCC12 bin directory")

    sources = ["RF/rf_uart_rx.c", "RF/rf_uart_tx.c", "RF/buf.c",
               "RF/role_mem.c", "src/rf_legacy_port.c", "src/rf_transport.c"]
    with tempfile.TemporaryDirectory(prefix="rf-reconnect-") as temp:
        temp = Path(temp)
        (temp / "CH57x_common.h").write_text(MOCK_HEADER)
        (temp / "harness.c").write_text(HARNESS)
        elf_path = temp / "test.elf"
        command = [str(toolchain / "riscv-wch-elf-gcc.exe"), "-march=rv32imc",
                   "-mabi=ilp32", "-msmall-data-limit=0", "-Os",
                   "-ffunction-sections", "-fdata-sections", "-nostartfiles",
                   "--specs=nano.specs", "--specs=nosys.specs",
                   "-Wl,--gc-sections,-Ttext=0x100000,-e,_start",
                   f"-I{temp}", f"-I{root / 'RF/include'}", f"-I{root / 'src'}",
                   str(temp / "harness.c"), *[str(root / s) for s in sources],
                   "-o", str(elf_path)]
        subprocess.run(command, check=True)
        machine = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        machine.mem_map(0x30000, 0x10000)  # Pairing/config flash pages.
        machine.mem_write(0x30000, b"\xff" * 0x10000)
        machine.mem_map(0x0f0000, 0x50000)
        with elf_path.open("rb") as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_LOAD":
                    machine.mem_write(segment["p_vaddr"], segment.data())
            symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
            machine.reg_write(UC_RISCV_REG_SP, 0x13fff0)
            machine.reg_write(UC_RISCV_REG_GP, symbols["__global_pointer$"])
            try:
                machine.emu_start(symbols["_start"], 0x140000, count=5_000_000)
            except UcError:
                # EBREAK terminates the harness after it publishes a result.
                pc = machine.reg_read(UC_RISCV_REG_PC)
                opcode = bytes(machine.mem_read(pc, 4))
                if opcode != b"\x73\x00\x10\x00" and opcode[:2] != b"\x02\x90":
                    raise RuntimeError(f"emulation fault at 0x{pc:x}: {opcode.hex()}")
                result = struct.unpack("<I", machine.mem_read(symbols["test_result"], 4))[0]
                if result != 1:
                    raise AssertionError(f"RF reconnect regression failed: {result}")
            else:
                raise AssertionError("RF reconnect harness did not reach EBREAK")
        print("PASS: lost reconnect response, weak-RSSI retry, peer-ID filtering")
        print("PASS: RF timeout/rearm while USB IN is blocked; queued bytes preserved")
        print("PASS: reconnect and confirmation with continuous UART input")


if __name__ == "__main__":
    main()
