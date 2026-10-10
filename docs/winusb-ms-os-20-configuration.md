# WinUSB MS OS 2.0 自动绑定配置

## 现象

Windows 设备管理器能够看到 Vendor 接口，但接口显示在“其他设备”中，设备状态为：

```text
此设备的驱动程序未被安装。（代码 28）
```

这说明 USB 设备已经完成基础枚举，并不代表 DP/DM、端点或 USB 中断完全失效。问题发生在 Windows 为 Vendor 接口选择驱动的阶段。

本项目使用 BOS Platform Capability 和 Microsoft OS 2.0 Descriptor，使 Windows 10/11 自动为 Vendor 接口绑定系统自带的 WinUSB 驱动，不需要自定义 INF 或微软签名。

## 根因

此前将代码28归因于Configuration Subset字段为0是不准确的。经过单变量实机测试和成熟实现对照，USB配置值为1时，MS OS 2.0 Configuration Subset仍使用零基配置索引0。

当前设备只有一个配置，USB 配置描述符为：

```c
// bConfigurationValue = 1
0x09, USB_DESCR_TYP_CONFIG, ..., 0x01, ...
```

复合设备只有一个配置时，Configuration Subset指定索引`0`：

```c
// wLength, wDescriptorType, bConfigurationValue, bReserved,
// wTotalLength
0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0xa8, 0x00,
```

不能把USB Configuration Descriptor的`bConfigurationValue`直接复制到这里。CH570复合设备基线使用索引0，并已实测自动绑定WinUSB。

## 接口号不能照搬

复合设备的Configuration Subset Header后面使用Function Subset Header指定要绑定WinUSB的接口号：

```c
// wLength, wDescriptorType, bFirstInterface, bReserved,
// wSubsetLength
0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0xa0, 0x00,
```

其中 `bFirstInterface` 必须与当前固件中的 Vendor 接口一致：

| 固件 | USB布局 | Vendor接口号 |
| --- | --- | ---: |
| CH570双CDC固件 | CDC0占接口0/1，CDC1占接口2/3，Vendor占接口4 | 4 |
| CH572 BLE UART固件 | 只有一个Vendor接口 | 0 |

CH570使用Configuration Subset和Function Subset，Function Subset中的接口号为4。CH572单Vendor设备经过实机验证必须采用设备级Feature Descriptor结构，Set Header后直接放Compatible ID和Registry Property，不包含这两个Subset Header。

## 正确配置检查表

1. Device Descriptor 的 `bcdUSB` 至少为 `0x0210`，主机才会读取 BOS 描述符。
2. BOS 描述符包含 Microsoft OS 2.0 Platform Capability UUID：

   ```text
   DF60DDD8-8945-4CC7-9CD2-659D9E648A9F
   ```

3. BOS 中的描述符集长度等于 `sizeof(ms_os_20_descriptor)`；CH570复合设备为`0x00B2`，CH572单Vendor设备为`0x00A2`。
4. BOS 中的 Vendor Code 与 EP0 处理代码一致；本项目为 `0x20`。
5. 主机请求 `bRequest = 0x20`、`wIndex = 0x0007` 时，EP0 返回完整的 MS OS 2.0 Descriptor Set。
6. 复合设备的Configuration Subset使用零基配置索引；只有一个配置时为`0`。
7. Function Subset 的 `bFirstInterface` 等于 Vendor 接口的 `bInterfaceNumber`。
8. Compatible ID 为 ASCII `WINUSB`。
9. `DeviceInterfaceGUIDs` 与上位机枚举使用的 GUID 完全一致。

本项目使用：

```text
{A1D4935B-4D76-4E6F-B623-6C856A67A8E9}
```

## Windows 描述符缓存

Windows 会缓存 USB 和 MS OS 描述符。修正固件后，仅重新烧录或普通重新插拔不一定会立即触发重新匹配。

修改 OS 描述符时应同时递增 Device Descriptor 的 `bcdDevice`。例如：

```text
1.04 -> 1.05
```

烧录后执行：

1. 拔出设备。
2. 在设备管理器中找到代码 28 的 Vendor 接口并选择“卸载设备”。
3. 重新插入设备。
4. 确认接口出现在“通用串行总线设备”中，驱动为 `WinUSB`。
5. 再运行 CLI 的 `list` 或 `info` 命令。

如果仍然显示代码 28，可使用管理员终端按设备实例路径移除旧实例：

```powershell
pnputil /enum-devices /connected
pnputil /remove-device "<设备实例ID>"
```

然后重新插入设备。不要先手动安装第三方 WinUSB INF；自动绑定配置正确时，Windows 10/11 使用系统自带的 `winusb.sys` 即可。

## Zadig 安装后的兼容性

Zadig 也可以把设备绑定到 `WinUSB`，但它生成的 INF 会同时生成一个随机的 `DeviceInterfaceGUIDs`。例如某次安装得到：

```text
{E7BD9AE4-470A-4DC8-A1DB-2B887B2376AA}
```

这个 GUID 只属于该次驱动安装，不能写死到上位机中，也不能假定它等于固件 MS OS 2.0 描述符声明的项目 GUID。否则设备管理器显示驱动正常，CLI 仍会因为按项目 GUID 枚举而报告 `no devices`。

本项目 CLI 的枚举顺序为：

1. 优先枚举项目 GUID `{A1D4935B-4D76-4E6F-B623-6C856A67A8E9}`。
2. 再枚举 Windows 标准 USB 设备接口 GUID `{A5DCBF10-6530-11D2-901F-00C04FB951ED}`。
3. 按 VID/PID 筛选回退路径，并实际调用 `WinUsb_Initialize`；不能作为 WinUSB 接口打开的复合设备路径会被丢弃。

这样既保留固件自动绑定方式，也兼容 Zadig 安装的随机接口 GUID。Zadig 可用于临时验证，但发布和普通使用仍推荐修正固件的 MS OS 2.0 描述符，让 Windows 自动绑定系统 WinUSB 驱动。

## 进一步定位

若清除缓存后仍失败，使用 USB Device Tree Viewer 或 USBPcap 检查：

1. 主机是否成功读取 BOS 描述符。
2. 主机是否发送 `bRequest = 0x20`、`wIndex = 0x0007`。
3. 固件是否返回与BOS声明长度一致的完整描述符集。
4. CH570复合设备的Configuration/Function Subset是否与当前配置描述符一致；CH572单Vendor设备不使用这两个Subset。

这样可以直接区分 BOS 未被读取、Vendor Request 未处理、描述符长度错误和接口号错误，避免反复修改端点或驱动脚本。
