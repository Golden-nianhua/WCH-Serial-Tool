# CH572 USB配置

## 已验证布局

CH572 USB/BLE UART固件在USB模式下是CDC与Vendor组成的复合设备；未枚举USB时才进入BLE UART模式。固件不包含2.4 GHz RF UART。

| 功能 | USB接口 | 端点 |
| --- | --- | --- |
| 物理CDC串口 | 控制接口0、数据接口1 | EP1 OUT `0x01`、EP1 IN `0x81` |
| Vendor配置 | 接口2 | EP7 OUT `0x07`、EP7 IN `0x87` |

VID/PID为`1A86:572E`，设备类为`EF/02/01`。配置描述符总长度为`0x005B`，共3个接口。Product字符串和Vendor接口字符串均为`WCH Serial Tool`，当前`bcdDevice`为`1.07`。

## WinUSB自动绑定

Vendor接口位于复合设备的接口2，因此Microsoft OS 2.0描述符集使用完整的Subset层级：

1. Set Header，描述符集总长度`0x00B2`。
2. Configuration Subset Header，配置索引为`0`，子集长度`0x00A8`。
3. Function Subset Header，首接口号为`2`，子集长度`0x00A0`。
4. Compatible ID Feature Descriptor，Compatible ID为`WINUSB`。
5. Registry Property Feature Descriptor，写入`DeviceInterfaceGUIDs`。

固定参数：

```text
Vendor Code: 0x20
wIndex: 0x0007
DeviceInterfaceGUIDs: {A1D4935B-4D76-4E6F-B623-6C856A67A8E9}
MS OS 2.0 descriptor length: 0x00B2
```

Configuration Subset中的字段是从零开始的配置索引，不是USB配置描述符的`bConfigurationValue`。当前只有一个配置，因此必须使用`0`。BOS Platform Capability和Set Header声明的总长度都必须是`0x00B2`。

旧的单Vendor固件采用`0x00A2`的设备级Feature Descriptor结构；该结构不能直接用于当前复合设备。

## 启动模式

- 上电后最多等待USB枚举500 ms；检测到枚举活动后会继续等待完整枚举。
- USB完成枚举后启用CDC物理串口和Vendor配置，BLE保持关闭。
- 未发现USB主机时关闭USB，进入BLE UART模式。
- PA2/RXD、PA3/TXD在USB CDC与BLE UART模式下复用。
- BLE UART模式下PA0/UDM和PA1/UDP用作RTS#、DTR#。

MS OS 2.0描述符发生变化时应递增`bcdDevice`，并在设备管理器中卸载旧设备实例后重新插入，避免Windows缓存干扰验证。
