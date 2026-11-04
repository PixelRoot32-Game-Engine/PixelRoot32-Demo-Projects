<#
.SYNOPSIS
    Builds the shipped DOOM1.whd container, and optionally flashes it.

.DESCRIPTION
    The single reproducible command for the container. The container carries the whole
    shareware episode: all nine levels' map lumps raw and mappable, every sprite
    frame the episode animates with, and one union bake of its graphics (one
    payload per distinct texture, flat and sprite lump -- not one bake per level).

    What the command does, and why each part is here:

      --raw-level E1M1..E1M9   every level's ten map lumps stay verbatim and
                               4-byte aligned, so FrameDriver maps them.
      --raw COLORMAP,TEXTURE1,PNAMES,PLAYPAL
                               the renderer's read-only tables are mapped in
                               place. The C bake referee reads TEXTURE1 and
                               PNAMES from the source WAD, but the device
                               wants these mapped. PLAYPAL is raw so a flash
                               page is read as 768 B at page*768 in the mapped
                               lump: decoding all 10,752 B needs a transient
                               internal-RAM block the heap does not have once
                               the save buffer is allocated. Raw costs about
                               564 B of flash and no decode time.
      --drop-lumps "DS*"       the 55 PCM sound effects, which an APU-only
                               port can never play (389 KB).
      --drop-lumps "HELP1,HELP2,CREDIT"
                               three 68,168 B full-screen images -- the help
                               screens and the credits. The reference ports cut
                               exactly these, so the shipped container does not
                               carry them.
      --bake E1M1..E1M9        the union bake: 100 textures + the sky, 50
                               flats and 216 sprite lumps, 1,726,532 B (the
                               216 include the imp's fireball and the ten
                               MT_TFOG teleport-fog frames).
      --drop-baked-sources     after the bake, remove the 156 wall-composition
                               patch lumps (155 names; SW18_7 appears twice).
                               Their bytes live on only as the dense width*128
                               payloads, so keeping the sources was duplication.
                               It never drops a flat, a sprite patch,
                               TEXTURE1/TEXTURE2/PNAMES/COLORMAP, a level lump
                               or any manifest target -- the flats in particular
                               ARE their manifest payloads and stay.
      --drop-unused-sprites    remove the S_START..S_END lumps no list can
                               name. The fixture names 216 for the bake, the
                               weapon raw list names 28 (PISG/PISF, SHTG/SHTF,
                               CHGG/CHGF, MISG/MISF, PUNG, SAWG) and the tool
                               never drops a name any raw list requires; the
                               other 239 frames are unreachable and go. They are
                               264,819 B stored (402,464 B uncompressed) and
                               271,532 B of container once their directory and
                               name entries are counted. The renderer's sprite
                               table is built from the nine levels' THINGS and
                               the union bake, so no removed frame was ever
                               addressable. The freed flash goes to the app
                               slot.
      --stats                  total, spare against the partitions_wad.csv `wad`
                               row, the dropped-by-kind trade, the sprite count
                               and what each level would cost baked alone.

    Then it proves the result four ways: `--check` on the WAD rebuilds every
    source lump in memory and compares it byte for byte, `--check` on the
    container parses the finished bytes and reports its stats, `--check-levels`
    opens all nine maps through lib/doom/doom_map.c and compares every lump size
    and record count with the WAD, and `bake_graphics.py --check-container
    --wad` recomposes every baked texture through lib/doom/r_data.c from the
    SOURCE WAD's patches -- the container does not carry them, and a
    container agreeing with itself would prove nothing.

    The container is flashed to 0x0C0000, the `wad` partition of
    partitions_wad.csv, after a 720,896 B app row; the trimmed container needs
    about 3,168,508 B. The `wad` row is 3,375,104 B and ends where the `saves`
    partition starts, so the budget below is that row's size. Flash the app
    image too, because the table is part of it. The uploader writes boot_app0.bin at 0xE000 on every upload (the last page of
    `nvs`, where the menu's settings live), so a reflash can lose that record;
    the `saves` row at 0x3F8000 is not touched.

    The menu's art is 17 M_* patches and the HU font STCFN033..095, kept raw
    by name through --raw-list tools/menu_lumps.txt (generated and drift-checked by
    gen_menu_lumps.py, proved by menu_check.py --container). The menu's
    settings live in `nvs` at 0x009000..0x00F000, so a table change that only
    moves `factory`/`wad`/`saves` leaves that record where it is.

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
    [string]$Wad,
    [string]$Out,
    [string]$Port = "COM4",
    [switch]$Flash
)

$ErrorActionPreference = "Stop"

# Defaults live in the body, not in the param block: PowerShell 5.1 leaves
# $PSScriptRoot empty while it binds a [CmdletBinding()] script's parameters, so
# `.\tools\make_container.ps1` would fail with "Join-Path: Path is an empty
# string" before doing anything.
if (-not $Wad) { $Wad = Join-Path $PSScriptRoot "DOOM1.WAD" }
if (-not $Out) { $Out = Join-Path $PSScriptRoot "DOOM1.whd" }

# The container is written at this offset: the `wad` row starts there in
# partitions_wad.csv.
$ContainerOffset = "0x0C0000"
# The `wad` row's size in partitions_wad.csv, which ends where the `saves`
# partition starts. This number is the budget whd_encode.py checks: a larger value
# would let a future rebuild write past 0x3F8000 and over the save slots, which
# the container's own header would not notice.
$Budget = 3375104
$Levels = "E1M1,E1M2,E1M3,E1M4,E1M5,E1M6,E1M7,E1M8,E1M9"
$Root = Split-Path -Parent $PSScriptRoot

# The firmware holds a fixed number of manifest records -- `kBakedCapacity` in the
# adapter's header -- and a bake past it fits the flash partition and is still
# refused whole at boot with `reason=baked,code=too_many`, leaving the panel on
# the shell. The constant is read from the header, so
# there is one source of truth, and whd_encode.py fails the encode: the mismatch
# stops here instead of on the device. A header this cannot parse is a build
# failure, never a silently skipped check.
$AdapterHeader = Join-Path $Root "src\doomc\BakedGraphics.h"
$CapacityLine = Select-String -Path $AdapterHeader -Pattern "kBakedCapacity\s*=\s*(\d+)" |
    Select-Object -First 1
if ($null -eq $CapacityLine) {
    throw "kBakedCapacity not found in ${AdapterHeader}: the bake capacity check cannot run"
}
$BakedCapacity = [int]$CapacityLine.Matches[0].Groups[1].Value

Push-Location $Root
try {
    if (-not (Test-Path $Wad)) {
        throw "$Wad is missing: the shareware WAD is never committed, point -Wad at it."
    }

    # One flag list, used by the encode and by the in-memory rebuild, so a proof
    # can never describe a different container than the one that was written.
    $EncodeArgs = @(
        "--raw-level", $Levels,
        "--raw", "COLORMAP,TEXTURE1,PNAMES,PLAYPAL",
        "--drop-lumps", "DS*",
        "--drop-lumps", "HELP1,HELP2,CREDIT",
        "--bake", $Levels,
        "--drop-baked-sources",
        "--sprite-list", "tools/e1m1_sprites.inc",
        "--raw-list", "tools/hud_lumps.txt",
        "--raw-list", "tools/weapon_lumps.txt",
        "--raw-list", "tools/menu_lumps.txt",
        # The 239 sprite frames no list can name. The tool protects every name
        # the sprite list and the three raw lists carry, so the weapon patches
        # survive.
        "--drop-unused-sprites",
        "--budget", $Budget,
        "--baked-capacity", $BakedCapacity
    )

    Write-Host "== encode: nine levels, the named sprite frames, the HUD, weapon and menu art, the mapped PLAYPAL, union bake, 3,375,104 B budget, $BakedCapacity manifest entries =="
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

    Write-Host "== prove: the 86 status-bar patches are raw, aligned and equal to the WAD's =="
    & python tools/gen_hud_lumps.py --wad $Wad --check
    if ($LASTEXITCODE -ne 0) {
        throw "gen_hud_lumps.py --check exited $LASTEXITCODE"
    }
    & python tools/hud_check.py --container $Out --wad $Wad
    if ($LASTEXITCODE -ne 0) {
        throw "hud_check.py exited $LASTEXITCODE"
    }

    Write-Host "== prove: the weapon patches are raw, aligned and equal to the WAD's =="
    & python tools/gen_weapon_lumps.py --wad $Wad --check
    if ($LASTEXITCODE -ne 0) {
        throw "gen_weapon_lumps.py --check exited $LASTEXITCODE"
    }
    & python tools/weapon_check.py --container $Out --wad $Wad
    if ($LASTEXITCODE -ne 0) {
        throw "weapon_check.py exited $LASTEXITCODE"
    }

    Write-Host "== prove: the menu patches and the HU font are raw, aligned and equal to the WAD's =="
    & python tools/gen_menu_lumps.py --wad $Wad --check
    if ($LASTEXITCODE -ne 0) {
        throw "gen_menu_lumps.py --check exited $LASTEXITCODE"
    }
    & python tools/menu_check.py --container $Out --wad $Wad
    if ($LASTEXITCODE -ne 0) {
        throw "menu_check.py --container exited $LASTEXITCODE"
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
        Write-Host "  & `"$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe`" run -e esp32dev_ILI9341_show -t upload"
        Write-Host "Flash the container with:"
        Write-Host "  & `"$env:USERPROFILE\.platformio\penv\Scripts\python.exe`" `"$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py`" --chip esp32 --port $Port --baud 921600 write_flash $ContainerOffset $Out"
    }
}
finally {
    Pop-Location
}
