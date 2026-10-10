$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolchain = 'C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC12\bin'
$gcc = Join-Path $toolchain 'riscv-wch-elf-gcc.exe'
$objcopy = Join-Path $toolchain 'riscv-wch-elf-objcopy.exe'
$size = Join-Path $toolchain 'riscv-wch-elf-size.exe'
$out = Join-Path $root 'obj'
$obj = Join-Path $out 'manual'

New-Item -ItemType Directory -Force $obj | Out-Null

$flags = @(
    '-march=rv32imc_zba_zbb_zbc_zbs',
    '-mabi=ilp32',
    '-mcmodel=medany',
    '-msmall-data-limit=8',
    '-mno-save-restore',
    '-Os',
    '-ffunction-sections',
    '-fdata-sections',
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

$sources = @(
    "$root\src\Main.c",
    "$root\src\usb_device.c",
    "$root\src\uart_bridge.c",
    "$root\src\config_protocol.c",
    "$root\src\config_service.c",
    "$root\src\device_config.c",
    "$root\BLE\ble_config_service.c",
    "$root\BLE\ble_device_info.c",
    "$root\BLE\ble_uart_service.c",
    "$root\BLE\ble_uart_app.c",
    "$root\HAL\MCU.c",
    "$root\HAL\RTC.c",
    "$root\HAL\SLEEP.c",
    "$root\StdPeriphDriver\CH57x_clk.c",
    "$root\StdPeriphDriver\CH57x_gpio.c",
    "$root\StdPeriphDriver\CH57x_sys.c",
    "$root\StdPeriphDriver\CH57x_uart.c"
)

$objects = @()
foreach($source in $sources)
{
    $name = ($source.Substring($root.Length + 1) -replace '[\\/]', '_')
    $object = Join-Path $obj ([IO.Path]::ChangeExtension($name, '.o'))
    & $gcc @flags -MMD -MP -c $source -o $object
    if($LASTEXITCODE) { throw "Compile failed: $source" }
    $objects += $object
}

foreach($source in @("$root\Startup\boot_stub.S", "$root\Startup\startup_CH572.S"))
{
    $object = Join-Path $obj ((Split-Path $source -LeafBase) + '.o')
    & $gcc @flags -x assembler-with-cpp -c $source -o $object
    if($LASTEXITCODE) { throw "Compile failed: $source" }
    $objects += $object
}

$elf = Join-Path $out 'ch572q_firmware.elf'
$hex = Join-Path $out 'ch572q_firmware.hex'
$map = Join-Path $out 'ch572q_firmware.map'

& $gcc @flags -T "$root\Ld\Link.ld" -nostartfiles `
    --specs=nano.specs --specs=nosys.specs `
    -Xlinker --gc-sections -Xlinker --print-memory-usage `
    "-L$root\StdPeriphDriver" "-L$root\LIB" "-Wl,-Map,$map" `
    -o $elf $objects -lISP572 -lm -lCH572BLE_PERI
if($LASTEXITCODE) { throw 'Link failed' }

& $objcopy -O ihex $elf $hex
if($LASTEXITCODE) { throw 'HEX generation failed' }

& $size $elf
Write-Host "Firmware image: $hex"
