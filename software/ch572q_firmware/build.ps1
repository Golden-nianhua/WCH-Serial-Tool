$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolchain = 'C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC12\bin'
$gcc = Join-Path $toolchain 'riscv-wch-elf-gcc.exe'
$objcopy = Join-Path $toolchain 'riscv-wch-elf-objcopy.exe'
$size = Join-Path $toolchain 'riscv-wch-elf-size.exe'
$out = Join-Path $root 'obj'

New-Item -ItemType Directory -Force $out | Out-Null

$commonFlags = @(
    '-march=rv32imc_zba_zbb_zbc_zbs',
    '-mabi=ilp32',
    '-mcmodel=medany',
    '-msmall-data-limit=8',
    '-mno-save-restore',
    '-Os',
    '-ffunction-sections',
    '-fdata-sections',
    '--param=highcode-gen-section-name=1',
    '-fno-common',
    '-fsigned-char',
    '-Wall',
    '-Wextra',
    '-std=gnu99',
    '-DNDEBUG=1',
    '-DBLE_SNV=FALSE',
    '-DHAL_SLEEP=FALSE',
    "-I$root\src",
    "-I$root\BLE",
    "-I$root\HAL\include",
    "-I$root\LIB",
    "-I$root\StdPeriphDriver\inc",
    "-I$root\RVMSIS"
)

$driverSources = @(
    "$root\StdPeriphDriver\CH57x_clk.c",
    "$root\StdPeriphDriver\CH57x_gpio.c",
    "$root\StdPeriphDriver\CH57x_sys.c",
    "$root\StdPeriphDriver\CH57x_uart.c"
)

function Compile-App($name, $sources, $assemblySources, $extraFlags)
{
    $objectDirectory = Join-Path $out "manual\$name"
    New-Item -ItemType Directory -Force $objectDirectory | Out-Null
    $flags = $commonFlags + $extraFlags
    $objects = @()

    foreach($source in $sources)
    {
        $objectName = ($source.Substring($root.Length + 1) -replace '[\\/]', '_')
        $object = Join-Path $objectDirectory ([IO.Path]::ChangeExtension($objectName, '.o'))
        & $gcc @flags -MMD -MP -c $source -o $object
        if($LASTEXITCODE) { throw "Compile failed: $source" }
        $objects += $object
    }

    foreach($source in $assemblySources)
    {
        $object = Join-Path $objectDirectory ((Split-Path $source -LeafBase) + '.o')
        & $gcc @flags -x assembler-with-cpp -c $source -o $object
        if($LASTEXITCODE) { throw "Compile failed: $source" }
        $objects += $object
    }

    return ,$objects
}

$usbSources = @(
    "$root\src\Main.c",
    "$root\src\usb_device.c",
    "$root\src\uart_bridge.c",
    "$root\src\config_protocol.c",
    "$root\src\config_service.c",
    "$root\src\device_config.c"
) + $driverSources

$bleSources = @(
    "$root\src\ble_main.c",
    "$root\src\uart_bridge.c",
    "$root\src\config_protocol.c",
    "$root\src\device_config.c",
    "$root\BLE\ble_device_info.c",
    "$root\BLE\ble_uart_service.c",
    "$root\BLE\ble_uart_app.c",
    "$root\HAL\MCU.c",
    "$root\HAL\RTC.c",
    "$root\HAL\SLEEP.c"
) + $driverSources

$usbObjects = Compile-App 'usb' $usbSources `
    @("$root\Startup\boot_stub.S", "$root\Startup\startup_CH572.S") `
    @('-DMODE_MEMORY_SIZE=512U')
$bleObjects = Compile-App 'ble' $bleSources `
    @("$root\Startup\startup_CH572.S") `
    @('-DBLE_MAC=TRUE', '-DBLE_MEMHEAP_SIZE=5120',
      '-DBLE_BUFF_MAX_LEN=496', '-DMODE_MEMORY_SIZE=5120U')

$usbElf = Join-Path $out 'ch572q_usb.elf'
$usbMap = Join-Path $out 'ch572q_usb.map'
& $gcc @commonFlags -T "$root\Ld\Link.ld" -nostartfiles `
    --specs=nano.specs --specs=nosys.specs `
    -Xlinker --gc-sections -Xlinker --print-memory-usage `
    '-Wl,--defsym=APP_ORIGIN=0x00002000' `
    '-Wl,--defsym=APP_LENGTH=0x00010000' `
    "-L$root\StdPeriphDriver" "-L$root\LIB" "-Wl,-Map,$usbMap" `
    -o $usbElf $usbObjects -lISP572 -lm
if($LASTEXITCODE) { throw 'USB application link failed' }

$bleElf = Join-Path $out 'ch572q_ble.elf'
$bleMap = Join-Path $out 'ch572q_ble.map'
& $gcc @commonFlags -T "$root\Ld\Link.ld" -nostartfiles `
    --specs=nano.specs --specs=nosys.specs `
    -Xlinker --gc-sections -Xlinker --print-memory-usage `
    '-Wl,--defsym=APP_ORIGIN=0x00012000' `
    '-Wl,--defsym=APP_LENGTH=0x00028000' `
    '-Wl,--defsym=MacAddr=0x20002FF4' '-Wl,-e,_start' `
    "-L$root\StdPeriphDriver" "-L$root\LIB" "-Wl,-Map,$bleMap" `
    -o $bleElf $bleObjects -lm -lCH572BLE_PERI
if($LASTEXITCODE) { throw 'BLE application link failed' }

$usbBinary = Join-Path $out 'ch572q_usb_padded.bin'
$bleBinary = Join-Path $out 'ch572q_ble.bin'
$firmwareBinary = Join-Path $out 'ch572q_firmware.bin'
$firmwareHex = Join-Path $out 'ch572q_firmware.hex'

& $objcopy -O binary --gap-fill 0xff --pad-to 0x12000 $usbElf $usbBinary
if($LASTEXITCODE) { throw 'USB binary generation failed' }
& $objcopy -O binary $bleElf $bleBinary
if($LASTEXITCODE) { throw 'BLE binary generation failed' }

$outputStream = [IO.File]::Create($firmwareBinary)
try
{
    foreach($path in @($usbBinary, $bleBinary))
    {
        $bytes = [IO.File]::ReadAllBytes($path)
        $outputStream.Write($bytes, 0, $bytes.Length)
    }
}
finally
{
    $outputStream.Dispose()
}

& $objcopy -I binary -O ihex $firmwareBinary $firmwareHex
if($LASTEXITCODE) { throw 'Combined HEX generation failed' }

Write-Host 'USB selector:'
& $size $usbElf
Write-Host 'BLE application:'
& $size $bleElf
Write-Host "Firmware image: $firmwareHex"
