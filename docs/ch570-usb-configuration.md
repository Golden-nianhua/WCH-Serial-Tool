# CH570 USB配置

## 已验证布局

CH570固件是一个复合USB设备，VID/PID为`1A86:572D`，设备类为`EF/02/01`。

| 功能 | USB接口 | 端点 |
| --- | --- | --- |
| 物理CDC串口 | 控制接口0、数据接口1 | 通知EP3 IN `0x83`，数据EP1 OUT `0x01`、EP1 IN `0x81` |
| 第二CDC串口 | 控制接口2、数据接口3 | 通知EP5 IN `0x85`，数据EP2 OUT `0x02`、EP2 IN `0x82` |
| Vendor配置 | 接口4 | EP7 OUT `0x07`、EP7 IN `0x87` |

配置描述符总长度为`0x00A4`，共5个接口。Product字符串和Vendor接口字符串均为`WCH Serial Tool`。

## WinUSB自动绑定

CH570的Vendor接口位于复合设备的接口4，因此Microsoft OS 2.0描述符集使用完整的Subset层级：

1. Set Header，描述符集总长度`0x00B2`。
2. Configuration Subset Header，配置索引为`0`，子集长度`0x00A8`。
3. Function Subset Header，首接口号为`4`，子集长度`0x00A0`。
4. Compatible ID Feature Descriptor，Compatible ID为`WINUSB`。
5. Registry Property Feature Descriptor，写入`DeviceInterfaceGUIDs`。

Configuration Subset中的配置字段是从零开始的配置索引，不是USB配置描述符的`bConfigurationValue`。本设备只有一个配置，所以这里必须是`0`。

固定参数：

```text
Vendor Code: 0x20
wIndex: 0x0007
DeviceInterfaceGUIDs: {A1D4935B-4D76-4E6F-B623-6C856A67A8E9}
```

BOS Platform Capability中的MS OS 2.0描述符集长度也必须是`0x00B2`。Windows通过EP0发出`bRequest=0x20, wIndex=0x0007`后，固件返回完整的`0xB2`字节。

## 修改约束

- CDC接口数量或顺序改变时，必须同步修改Vendor接口号和Function Subset的首接口号。
- MS OS 2.0描述符内容变化时递增`bcdDevice`，避免Windows继续使用旧缓存。
- 不要把CH572单Vendor设备的`0xA2`设备级结构用于本复合设备。
