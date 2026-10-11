$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolchain = 'C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin'
$gcc = Join-Path $toolchain 'riscv-none-embed-gcc.exe'
$objcopy = Join-Path $toolchain 'riscv-none-embed-objcopy.exe'
$size = Join-Path $toolchain 'riscv-none-embed-size.exe'
$out = Join-Path $root 'obj'
$obj = Join-Path $out 'manual'

New-Item -ItemType Directory -Force $obj | Out-Null

$flags = @(
    '-march=rv32imacxw',
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
    '-DCH32V20x_D8W',
    '-DCLK_OSC32K=2',
    '-DDEBUG=0',
    "-I$root\SRC\Core",
    "-I$root\SRC\Startup",
    "-I$root\SRC\Debug",
    "-I$root\SRC\Peripheral\inc",
    "-I$root\APP\include",
    "-I$root\HAL\include",
    "-I$root\LIB"
)

$sources = @(
    "$root\APP\ble_config_service.c",
    "$root\APP\ble_device_info.c",
    "$root\APP\ble_uart_app.c",
    "$root\APP\ble_uart_service.c",
    "$root\APP\board.c",
    "$root\APP\ch32v20x_it.c",
    "$root\APP\config_protocol.c",
    "$root\APP\config_service.c",
    "$root\APP\peripheral_main.c",
    "$root\APP\system_ch32v20x.c",
    "$root\APP\uart_bridge.c",
    "$root\APP\usb_device.c",
    "$root\HAL\MCU.c",
    "$root\HAL\RTC.c",
    "$root\HAL\SLEEP.c",
    "$root\SRC\Core\core_riscv.c",
    "$root\SRC\Debug\debug.c"
)
$sources += Get-ChildItem "$root\SRC\Peripheral\src\*.c" | ForEach-Object FullName

$objects = @()
foreach($source in $sources)
{
    $relative = [IO.Path]::GetRelativePath($root, $source)
    $name = ($relative -replace '[\\/]', '_') -replace '\.c$', '.o'
    $object = Join-Path $obj $name
    & $gcc @flags -MMD -MP -c $source -o $object
    if($LASTEXITCODE) { throw "Compile failed: $source" }
    $objects += $object
}

foreach($source in @("$root\SRC\Startup\startup_ch32v20x_D8W.S",
                     "$root\LIB\ble_task_scheduler.S"))
{
    $relative = [IO.Path]::GetRelativePath($root, $source)
    $name = ($relative -replace '[\\/]', '_') -replace '\.S$', '.o'
    $object = Join-Path $obj $name
    & $gcc @flags -x assembler-with-cpp -c $source -o $object
    if($LASTEXITCODE) { throw "Compile failed: $source" }
    $objects += $object
}

$elf = Join-Path $out 'ch32v208w_firmware.elf'
$hex = Join-Path $out 'ch32v208w_firmware.hex'
$map = Join-Path $out 'ch32v208w_firmware.map'

& $gcc @flags -T "$root\HAL\Link.ld" -nostartfiles `
    --specs=nano.specs --specs=nosys.specs `
    -Xlinker --gc-sections -Xlinker --print-memory-usage `
    "-L$root\LIB" "-Wl,-Map,$map" `
    -o $elf $objects -lwchble -lm
if($LASTEXITCODE) { throw 'Link failed' }

& $objcopy -O ihex $elf $hex
if($LASTEXITCODE) { throw 'HEX generation failed' }

& $size $elf
Write-Host "Firmware image: $hex"
