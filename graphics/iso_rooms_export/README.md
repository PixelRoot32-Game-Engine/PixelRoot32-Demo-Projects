# Isometric Rooms Export

An isometric scene with rooms, straight out of the PixelRoot32 Tilemap Editor:
one map per room on a shared tileset, a room graph built from the exported room
table, and an arrow key that moves to the room connected on that side.

> [`iso_tilemap_export`](../iso_tilemap_export) consumes one isometric map.
> [`iso_dungeon`](../iso_dungeon) is the gameplay demo (hero, depth sorting,
> doors). This one is the smallest consumer of the **rooms** part of the
> export, and nothing else.

## What the export gives the game

| Symbol | Use |
| --- | --- |
| `ISO_PROJECTION` | The scene's cell-to-screen basis. |
| `ISO_ROOMS_MAP_ROOMS[]` | Each room's origin cell, size and connections. |
| `ISO_ROOMS_MAP_ROOM_LAYER` | That table with the cell stride (32x16). |
| `ROOM_LAYERS[]`, `NUM_ROOM_LAYERS` | One `TileMap4bpp` per room, local cells from (0, 0). |
| `init()` | Fills the maps and, with **Install palette**, installs the palette. |

`IsoRoomsScene` builds the graph with the projected
`gameplay::buildRoomGraph(layer, graph, ISO_PROJECTION, perRoom)`, widening
each room by what its own map draws (`expandProjectedMapBounds`), and draws the
current room through `gameplay::roomProjection(ISO_PROJECTION, room)`.

## Producing the export

`src/assets/tilemap/` is the editor's export of `editor/iso_rooms.pr32scene.bin`:
an isometric 14x7 scene with two 7x7 rooms (Right goes 0 -> 1, Left comes
back) on one tile layer, using the tilesets in `editor/assets/tilesets/`
(32x16 floors, 32x40 walls). To re-export it:

1. Open `editor/iso_rooms.pr32scene.bin` in the Tool Suite.
2. Edit the rooms or the tiles. Keep **one** tile layer: this example draws the
   first layer of each room.
3. Export (the bit depth follows the colours in use; these tilesets give 4bpp)
   with:
   - namespace `iso_rooms::map` (this names the files `iso_rooms_map.h/.cpp`
     and the room symbols `ISO_ROOMS_MAP_*`);
   - **Install palette**: `Background`;
   - output folder `src/assets/tilemap/`.

## Build and run

```
pio run -e native -t exec
```

The arrow keys move between connected rooms. Each room should look exactly as
it does in the editor: same colours, walls standing on their cells, nothing
cut at the top of the tall tiles.

The engine is pulled from its `feat/iso-rooms` branch (see `platformio.ini`):
the projected room API is not in a published release yet.
