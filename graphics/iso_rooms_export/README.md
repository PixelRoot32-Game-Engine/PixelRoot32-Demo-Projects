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

`src/assets/tilemap/` is empty in the repository: the export is the step this
example exists to check. The tilesets are in `editor/assets/tilesets/`
(32x16 floors, 32x40 walls).

1. In the Tool Suite, create a project with an **isometric** scene, cell 32x16.
2. Import `dungeon_floors.png` (tile 32x16) and `dungeon_walls.png` (tile 32x40).
3. Use **one** tile layer; floors and walls share it, like `iso_dungeon`.
4. Paint two rooms side by side and mark them as rooms (for example two 7x7
   rooms at columns 0 and 7), connected to each other.
5. Save the project as `editor/iso_rooms.pr32scene.bin`.
6. Export, 4bpp, with:
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

The engine is the sibling checkout `../../../PixelRoot32-Game-Engine` (see
`platformio.ini`): the projected room API is not in a published release yet.
