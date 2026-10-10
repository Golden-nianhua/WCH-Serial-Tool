# USB裁剪验证记录

## 基线

来源为已验证的`software/ch570q_firmware`，USB布局为两路CDC加一路Vendor：

- CDC0：接口0/1，EP1
- CDC1：接口2/3，EP2
- Vendor：接口4，EP7
- PID：`0x572D`
- `bcdDevice`：`1.02`

实机结果：可以正常枚举和识别。

## 单Vendor尝试

曾同时删除两路CDC、EP1/EP2、CDC类请求和数据路径，只保留接口0上的EP7 Vendor。

实机结果：设备管理器无反应，没有出现设备。由于一次修改的变量过多，该结果不能确定是描述符、端点初始化还是枚举超时造成的，已回退。

## 第一阶段：一路CDC加Vendor

当前测试版本只移除CDC1和EP2，保留CDC0和EP1：

- CDC0：接口0/1，EP1
- Vendor：接口2，EP7
- Configuration `wTotalLength`：`0x005B`
- `bNumInterfaces`：3
- MS OS 2.0 Function Subset目标接口：2
- PID：`0x572D`
- `bcdDevice`：`1.03`

RF模块仍被编译，但面向EP2的USB接口暂时返回无数据。这样本阶段只验证删除第二路CDC后能否正常枚举和自动绑定WinUSB。

实机结果：可以正常枚举和识别。

## 第二阶段：仅保留Vendor描述符

从USB Configuration Descriptor中删除最后一路CDC，主机只看到接口0上的EP7 Vendor：

- Vendor：接口0，EP7
- Configuration `wTotalLength`：`0x0020`
- `bNumInterfaces`：1
- MS OS 2.0 Function Subset目标接口：0
- PID：`0x572D`
- `bcdDevice`：`1.04`

本阶段暂不删除EP1缓冲、DMA、端点初始化、CDC类请求和物理UART转发代码。它们仍在固件中，但不会出现在USB描述符中，主机无法访问。此步骤仅验证Vendor-only描述符能否保持正常枚举。

实机结果：失败，设备管理器没有出现设备。

## 镜像实验：删除EP1、保留EP2

从完整双CDC基线重新开始，只删除物理CDC和EP1，保留无线CDC和EP2：

- RF CDC：接口0/1，EP2
- Vendor：接口2，EP7
- Configuration `wTotalLength`：`0x005B`
- `bNumInterfaces`：3
- MS OS 2.0 Function Subset目标接口：2
- PID：`0x572D`
- `bcdDevice`：`1.05`

EP2仍映射到固件内部逻辑端口1，保留原有RF CDC数据和Line Coding处理。物理CDC的EP1缓冲、DMA、端点中断和UART/USB转发均已移除。

实机结果：可以正常枚举和识别。这说明EP1不是枚举所必需，任意一路合法CDC均可维持原复合设备布局。

## 第三阶段：合法的单Vendor设备类

上一轮失败的Vendor-only描述符仍保留设备类`EF/02/01`。该类组合表示使用IAD的复合设备，但删除CDC后Configuration中已经没有IAD，设备类与配置结构不匹配。

本阶段从已验证的“EP2 CDC + Vendor”版本出发：

- 删除CDC和IAD描述符，仅暴露接口0上的EP7 Vendor
- Device Descriptor类从`EF/02/01`改为`00/00/00`
- Configuration `wTotalLength`：`0x0020`
- `bNumInterfaces`：1
- MS OS 2.0 Function Subset目标接口：0
- PID：`0x572D`
- `bcdDevice`：`1.06`

EP2缓冲、DMA、端点中断、类请求和RF运行时代码暂时全部保留。此步骤只验证修正设备类后的Vendor-only描述符。

实机现象：插入时设备管理器短暂刷新，随后设备消失；拔出时没有反应。这与主循环在500 ms内未配置时主动关闭USB并进入RF从机的行为一致，不能据此判断最终的Windows枚举错误。

## 第四阶段：取消USB枚举超时

描述符和`bcdDevice 1.06`保持第三阶段不变。只取消500 ms后执行`UsbDevice_Disable()`和切换RF从机的分支，使固件无限等待USB配置。本阶段用于让设备持续留在Windows中，以获得明确的设备管理器状态。

实机结果：设备持续出现，状态为代码28。这证明基础USB枚举正常，失败位置在WinUSB自动绑定。

## 第五阶段：修正MS OS 2.0配置值

实际USB Configuration Descriptor的`bConfigurationValue`为1，而继承自CH570基线的MS OS 2.0 Configuration Subset Header仍写0。本阶段只修改：

- MS OS 2.0 `bConfigurationValue`：`0 -> 1`
- `bcdDevice`：`1.06 -> 1.07`，用于刷新Windows描述符缓存

Vendor接口仍为接口0，EP2运行时代码仍保留，USB枚举超时仍关闭。其余描述符和运行逻辑不变。

实机结果：仍为代码28。Windows没有生成`USB\\MS_COMP_WINUSB` Compatible ID，注册表也没有`MsOs20Flags`。

## 第六阶段：设备级MS OS 2.0 Feature Descriptor

对照TinyUSB的已验证MS OS 2.0实现后确认：Configuration Subset字段使用零基配置索引；USB配置值为1时该字段仍应为0。因此第五阶段将其改为1不是正确修复，CH570复合基线中的0是合理值。

复合设备把Feature Descriptor放在Configuration Subset和Function Subset下，以便绑定到`MI_xx`子设备。当前单Vendor设备没有复合父设备和子接口，WINUSB应直接应用到设备本身。本阶段调整为：

- 删除8字节Configuration Subset Header
- 删除8字节Function Subset Header
- Compatible ID和Registry Property直接跟在10字节Set Header后
- Descriptor Set总长度：`0x00B2 -> 0x00A2`
- BOS中声明的Descriptor Set长度同步改为`0x00A2`
- `bcdDevice`：`1.07 -> 1.08`

USB接口、EP2运行时代码和无限等待USB配置的诊断逻辑不变。
