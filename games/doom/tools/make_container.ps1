<#
.SYNOPSIS
    Builds the shipped DOOM1.whd container, and optionally flashes it.

.DESCRIPTION
    D3.10's single reproducible command. The container carries the whole
    shareware episode: all nine levels' map lumps raw and mappable, and one
    union bake of their graphics (one payload per distinct texture, flat and
    sprite lump -- not one bake per level).

    What the command does, and why each part is here:

      --raw-level E1M1..E1M9   every level's ten map lumps stay verbatim and
                               4-byte aligned, so FrameDriver maps them.
      --raw COLORMAP,TEXTURE1,PNAMES
                               the renderer's read-only tables are mapped in
                               place; the C bake referee reads TEXTURE1/PNAMES
                               out of this same container.
      --drop-lumps "DS*"       the 55 PCM sound effects, which an APU-only
                               port can never play (389 KB).
      --drop-unused-sprites    the 447 sprite lumps the bake's frame-0 sprite
                               list does not name (493 KB). Without this the
                               episode does not fit the 3 MB partition.
      --bake E1M1..E1M9        the union bake: 100 textures + the sky, 50
                               flats and 36 sprite lumps, 1,484,568 B.
      --stats                  total, spare against the 3 MB wad partition and
                               what each level would cost baked alone.

    Then it proves the result twice: `--check-levels` opens all nine maps
    through lib/doom/doom_map.c and compares every lump size and record count
    with the WAD, and `bake_graphics.py --check-container` recomposes every
    baked texture through lib/doom/r_data.c.

    The container is flashed to 0x100000, the `wad` partition of
    partitions_wad.csv. That offset moved from 0x200000 in D3.10; flashing a new
    partition table erases NVS, which this project does not use.

    The WAD is copyrighted shareware data and is never committed. tools/ is
    git-ignored for both DOOM1.WAD and DOOM1.whd.

.PARAMETER Wad
    The source IWAD. Defaults to tools/DOOM1.WAD beside this script.

.PARAMETER Out
    The container to write. Defaults to tools/DOOM1.whd beside this script.

.PARAMETER Port
    Serial port for -Flash. Defaults to COM4.

.PARAMETER Flash
    Flash the container after building it. The app image is flashed separately
    with `pio run -e <env> -t upload`.

.EXAMPLE
    .\tools\make_container.ps1
    .\tools\make_container.ps1 -Flash
#>
[CmdletBinding()]
param(
    [string]$Wad = (Join-Path $PSScriptRoot "DOOM1.WAD"),
    [string]$Out = (Join-Path $PSScriptRoot "DOOM1.whd"),
    [string]$Port = "COM4",
    [switch]$Flash
)

$ErrorActionPreference = "Stop"

# The container is written at this offset in the `wad` partition (D3.10).
$ContainerOffset = "0x100000"
$Levels = "E1M1,E1M2,E1M3,E1M4,E1M5,E1M6,E1M7,E1M8,E1M9"
$Root = Split-Path -Parent $PSScriptRoot

Push-Location $Root
try {
    if (-not (Test-Path $Wad)) {
        throw "$Wad is missing: the shareware WAD is never committed, point -Wad at it."
    }

    Write-Host "== encode: nine levels, union bake, 3 MB budget =="
    & python tools/whd_encode.py $Wad -o $Out --force `
        --raw-level $Levels `
        --raw COLORMAP,TEXTURE1,PNAMES `
        --drop-lumps "DS*" `
        --drop-unused-sprites `
        --bake $Levels `
        --sprite-list tools/e1m1_sprites.inc `
        --stats
    if ($LASTEXITCODE -ne 0) {
        throw "whd_encode.py exited $LASTEXITCODE"
    }

    Write-Host "== prove: nine maps open through doom_map_open() =="
    & python tools/whd_encode.py $Out --check-levels
    if ($LASTEXITCODE -ne 0) {
        throw "whd_encode.py --check-levels exited $LASTEXITCODE"
    }

    Write-Host "== prove: every baked texture recomposes through r_data.c =="
    & python tools/bake_graphics.py --check-container $Out
    if ($LASTEXITCODE -ne 0) {
        throw "bake_graphics.py --check-container exited $LASTEXITCODE"
    }

    if ($Flash) {
        $esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
        $python = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
        if (-not (Test-Path $esptool) -or -not (Test-Path $python)) {
            throw "PlatformIO's esptool is not installed; run 'pio run' once first."
        }
        Write-Host "== flash: $Out at $ContainerOffset on $Port =="
        & $python $esptool --chip esp32 --port $Port --baud 921600 `
            write_flash $ContainerOffset $Out
        if ($LASTEXITCODE -ne 0) {
            throw "esptool exited $LASTEXITCODE"
        }
    }
    else {
        Write-Host ""
        Write-Host "Flash the app with: $env:USERPROFILE\.platformio\penv\Scripts\platformio.exe run -e esp32dev_ILI9341_frame -t upload"
        Write-Host "Flash the container with:"
        Write-Host "  & `"$env:USERPROFILE\.platformio\penv\Scripts\python.exe`" `"$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py`" --chip esp32 --port $Port --baud 921600 write_flash $ContainerOffset $Out"
    }
}
finally {
    Pop-Location
}
