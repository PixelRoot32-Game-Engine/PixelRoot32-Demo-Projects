<#
.SYNOPSIS
    Builds the shipped DOOM1.whd container, and optionally flashes it.

.DESCRIPTION
    D3.11's single reproducible command. The container carries the whole
    shareware episode: all nine levels' map lumps raw and mappable, every sprite
    frame the episode animates with, and one union bake of its graphics (one
    payload per distinct texture, flat and sprite lump -- not one bake per level).

    What the command does, and why each part is here:

      --raw-level E1M1..E1M9   every level's ten map lumps stay verbatim and
                               4-byte aligned, so FrameDriver maps them.
      --raw COLORMAP,TEXTURE1,PNAMES
                               the renderer's read-only tables are mapped in
                               place. The C bake referee now reads TEXTURE1 and
                               PNAMES from the source WAD, but the device still
                               wants these three mapped.
      --drop-lumps "DS*"       the 55 PCM sound effects, which an APU-only
                               port can never play (389 KB).
      --drop-lumps "HELP1,HELP2,CREDIT"
                               three 68,168 B full-screen images -- the help
                               screens and the credits. The reference ports cut
                               exactly these, and Phase 5 can redraw or omit
                               them, so the shipped container does not carry
                               them.
      --bake E1M1..E1M9        the union bake: 100 textures + the sky, 50
                               flats and 36 sprite lumps, 1,484,568 B.
      --drop-baked-sources     after the bake, remove the 156 wall-composition
                               patch lumps (155 names; SW18_7 appears twice).
                               Their bytes live on only as the dense width*128
                               payloads, so keeping the sources was duplication.
                               It never drops a flat, a sprite patch,
                               TEXTURE1/TEXTURE2/PNAMES/COLORMAP, a level lump
                               or any manifest target -- the flats in particular
                               ARE their manifest payloads and stay.
      (no --drop-unused-sprites)
                               every S_START..S_END lump stays in the container:
                               483 sprite lumps, 533,310 B stored. The frame-0
                               fixture names 36 of them for the bake; the other
                               447 are compressed frames Phase 4 animation can
                               address later, which is the point of D3.11.
      --stats                  total, spare against the partitions_wad.csv `wad`
                               row, the dropped-by-kind trade, the sprite count
                               and what each level would cost baked alone.

    Then it proves the result four ways: `--check` on the WAD rebuilds every
    source lump in memory and compares it byte for byte, `--check` on the
    container parses the finished bytes and reports its stats, `--check-levels`
    opens all nine maps through lib/doom/doom_map.c and compares every lump size
    and record count with the WAD, and `bake_graphics.py --check-container
    --wad` recomposes every baked texture through lib/doom/r_data.c from the
    SOURCE WAD's patches -- the container no longer carries them, and a
    container agreeing with itself would prove nothing.

    The container is flashed to 0x0D0000, the `wad` partition of
    partitions_wad.csv (D3.11). That offset moved from 0x100000 when the app slot
    shrank to 768 KB; flashing a new partition table erases NVS, which this
    project does not use. Flash the app image too, because the table is part of
    it.

    The WAD is copyrighted shareware data and is never committed. tools/ is
    git-ignored for both DOOM1.WAD and DOOM1.whd.

.PARAMETER Wad
    The source IWAD. Defaults to tools/DOOM1.WAD beside this script.

.PARAMETER Out
    The container to write. Defaults to tools/DOOM1.whd beside this script.

.PARAMETER Port
    Serial port for -Flash. Defaults to COM4.

.PARAMETER Flash
    Flash the container after building it. The app image (and the partition
    table) is flashed separately with `pio run -e <env> -t upload`.

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

# The container is written at this offset in the `wad` partition (D3.11).
$ContainerOffset = "0x0D0000"
$Budget = 3342336
$Levels = "E1M1,E1M2,E1M3,E1M4,E1M5,E1M6,E1M7,E1M8,E1M9"
$Root = Split-Path -Parent $PSScriptRoot

Push-Location $Root
try {
    if (-not (Test-Path $Wad)) {
        throw "$Wad is missing: the shareware WAD is never committed, point -Wad at it."
    }

    # One flag list, used by the encode and by the in-memory rebuild, so a proof
    # can never describe a different container than the one that was written.
    $EncodeArgs = @(
        "--raw-level", $Levels,
        "--raw", "COLORMAP,TEXTURE1,PNAMES",
        "--drop-lumps", "DS*",
        "--drop-lumps", "HELP1,HELP2,CREDIT",
        "--bake", $Levels,
        "--drop-baked-sources",
        "--sprite-list", "tools/e1m1_sprites.inc",
        "--budget", $Budget
    )

    Write-Host "== encode: nine levels, every sprite, union bake, 3,342,336 B budget =="
    & python tools/whd_encode.py $Wad -o $Out --force @EncodeArgs --stats
    if ($LASTEXITCODE -ne 0) {
        throw "whd_encode.py exited $LASTEXITCODE"
    }

    Write-Host "== prove: every source lump rebuilds byte for byte =="
    & python tools/whd_encode.py $Wad --check @EncodeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "whd_encode.py --check (WAD rebuild) exited $LASTEXITCODE"
    }

    Write-Host "== report: the finished container =="
    & python tools/whd_encode.py $Out --check --stats --budget $Budget
    if ($LASTEXITCODE -ne 0) {
        throw "whd_encode.py --check (container) exited $LASTEXITCODE"
    }

    Write-Host "== prove: nine maps open through doom_map_open() =="
    & python tools/whd_encode.py $Out --check-levels
    if ($LASTEXITCODE -ne 0) {
        throw "whd_encode.py --check-levels exited $LASTEXITCODE"
    }

    Write-Host "== prove: every baked texture recomposes from the WAD through r_data.c =="
    & python tools/bake_graphics.py --check-container $Out --wad $Wad
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
        Write-Host "Flash the app (and the new partition table) with:"
        Write-Host "  & `"$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe`" run -e esp32dev_ILI9341_frame -t upload"
        Write-Host "Flash the container with:"
        Write-Host "  & `"$env:USERPROFILE\.platformio\penv\Scripts\python.exe`" `"$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py`" --chip esp32 --port $Port --baud 921600 write_flash $ContainerOffset $Out"
    }
}
finally {
    Pop-Location
}
