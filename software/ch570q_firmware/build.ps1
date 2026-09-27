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
    "-I$root\src",
    "-I$root\RF\include",
    "-I$root\StdPeriphDriver\inc",
    "-I$root\RVMSIS"
)

$sources = @(
    "$root\src\Main.c",
    "$root\src\usb_device.c",
    "$root\src\uart_bridge.c",
    "$root\src\config_protocol.c",
    "$root\src\config_service.c",
    "$root\src\rf_legacy_port.c",
    "$root\src\rf_transport.c",
    "$root\RF\buf.c",
    "$root\RF\role_mem.c",
    "$root\RF\rf.c",
    "$root\RF\rf_uart_rx.c",
    "$root\RF\rf_uart_tx.c",
    "$root\StdPeriphDriver\CH57x_clk.c",
    "$root\StdPeriphDriver\CH57x_gpio.c",
    "$root\StdPeriphDriver\CH57x_sys.c",
    "$root\StdPeriphDriver\CH57x_uart.c"
)

$objects = @()
foreach($source in $sources)
{
    $object = Join-Path $obj ((Split-Path $source -LeafBase) + '.o')
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

$elf = Join-Path $out 'ch570q_firmware.elf'
$hex = Join-Path $out 'ch570q_firmware.hex'
$map = Join-Path $out 'ch570q_firmware.map'

& $gcc @flags -T "$root\Ld\Link.ld" -nostartfiles `
    --specs=nano.specs --specs=nosys.specs `
    -Xlinker --gc-sections -Xlinker --print-memory-usage `
    "-L$root\StdPeriphDriver" "-L$root\RF\lib" "-Wl,-Map,$map" `
    -o $elf $objects -lISP572 -lm -lCH57xRF
if($LASTEXITCODE) { throw 'Link failed' }

& $objcopy -O ihex $elf $hex
if($LASTEXITCODE) { throw 'HEX generation failed' }

& $size $elf
Write-Host "Firmware image: $hex"
