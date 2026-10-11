# WinUSB MS OS 2.0配置

本文只说明Windows 10/11通过Microsoft OS 2.0描述符自动绑定系统`WinUSB`驱动所需的正确配置。

## 通用要求

USB Device Descriptor的`bcdUSB`应不低于`0x0210`，使主机读取BOS描述符。BOS中加入Microsoft OS 2.0 Platform Capability：

```text
Platform Capability UUID: DF60DDD8-8945-4CC7-9CD2-659D9E648A9F
Windows version:          0x06030000
Vendor Code:              0x20
Alternate enumeration:   0x00
```

BOS中的Descriptor Set长度必须等于实际Microsoft OS 2.0 Descriptor Set长度。主机随后通过EP0发送Vendor IN请求：

```text
bRequest = 0x20
wIndex   = 0x0007
```

固件应返回完整的Microsoft OS 2.0 Descriptor Set，返回长度不超过Setup包中的`wLength`。

WinUSB Feature Descriptor需要包含：

- Compatible ID：ASCII `WINUSB`，其余字节补零。
- Registry Property：属性名`DeviceInterfaceGUIDs`，类型`REG_MULTI_SZ`。
- 项目GUID：`{A1D4935B-4D76-4E6F-B623-6C856A67A8E9}`。

## 复合设备中的Vendor接口

复合设备包含CDC、HID等接口，同时只让其中一个Vendor接口绑定WinUSB时，Microsoft OS 2.0 Descriptor Set采用以下层级：

```text
Set Header
└─ Configuration Subset Header
   └─ Function Subset Header
      ├─ Compatible ID Feature Descriptor
      └─ Registry Property Feature Descriptor
```

本项目的Feature Descriptor长度固定，因此各层长度为：

| 描述符 | 长度或子集总长度 |
| --- | ---: |
| Set Header | 10字节 |
| Configuration Subset Header | 8字节，`wTotalLength = 0x00A8` |
| Function Subset Header | 8字节，`wSubsetLength = 0x00A0` |
| Compatible ID Feature | 20字节 |
| Registry Property Feature | 132字节 |
| 整个Descriptor Set | `0x00B2` |

Configuration Subset Header中的`bConfigurationValue`实际表示从零开始的配置索引。只有一个USB Configuration时填写`0`，不要复制USB Configuration Descriptor中通常为`1`的`bConfigurationValue`。

Function Subset Header中的`bFirstInterface`必须等于Vendor接口的`bInterfaceNumber`：

- CH570双CDC + Vendor：Vendor为接口4，填写`4`。
- CH572单CDC + Vendor：Vendor为接口2，填写`2`。

使用IAD组织CDC等复合功能时，Device Descriptor通常设置为：

```text
bDeviceClass    = 0xEF
bDeviceSubClass = 0x02
bDeviceProtocol = 0x01
```

## 单Vendor设备

设备只公开一个Vendor接口时，不需要Configuration Subset和Function Subset。Feature Descriptor直接放在Set Header后：

```text
Set Header
├─ Compatible ID Feature Descriptor
└─ Registry Property Feature Descriptor
```

对应长度为：

| 描述符 | 长度 |
| --- | ---: |
| Set Header | 10字节 |
| Compatible ID Feature | 20字节 |
| Registry Property Feature | 132字节 |
| 整个Descriptor Set | `0x00A2` |

BOS中的Descriptor Set长度也必须改为`0x00A2`。单接口设备的Device Descriptor使用接口定义类别：

```text
bDeviceClass    = 0x00
bDeviceSubClass = 0x00
bDeviceProtocol = 0x00
```

Configuration中包含一个`bInterfaceClass = 0xFF`的Vendor接口，以及实际使用的Bulk IN和Bulk OUT端点。若各端点最大包长为64字节，则一个最小配置通常由9字节Configuration、9字节Interface和两个7字节Endpoint描述符组成，`wTotalLength = 0x0020`、`bNumInterfaces = 1`。

单Vendor设备不应继续使用表示IAD复合设备的`EF/02/01`，也不应保留只适用于复合接口定位的Subset Header。

本项目的CH32V208固件采用这一结构：VID/PID为`1A86:572F`，Vendor为接口0，使用EP7 OUT `0x07`和EP7 IN `0x87`。

## 实现检查

1. BOS声明的Descriptor Set长度与实际数组长度完全一致。
2. EP0处理的Vendor Code与BOS中声明的值一致。
3. `wIndex = 0x0007`时返回Microsoft OS 2.0 Descriptor Set。
4. 复合设备使用零基Configuration索引和正确的Vendor接口号。
5. 单Vendor设备使用设备级Feature Descriptor结构，不包含Subset Header。
6. Compatible ID为`WINUSB`，`DeviceInterfaceGUIDs`与上位机枚举使用的GUID一致。
7. 修改USB或OS描述符后递增`bcdDevice`，避免Windows继续使用旧缓存。

自动绑定配置正确时不需要自定义INF、驱动签名或Zadig，Windows会加载系统自带的`winusb.sys`。
