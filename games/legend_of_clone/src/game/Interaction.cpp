#include "game/Interaction.h"

#include "GameConstants.h"

namespace legend_of_clone {

GridCell playerCellAt(int pixelX, int pixelY) {
    return GridCell{(pixelX + kPlayerSize / 2) / kTileSize,
                    (pixelY + kPlayerSize / 2) / kTileSize};
}

GridCell facingCell(GridCell standing, Facing facing) {
    switch (facing) {
        case Facing::Up:    --standing.row; break;
        case Facing::Down:  ++standing.row; break;
        case Facing::Left:  --standing.col; break;
        case Facing::Right: ++standing.col; break;
    }
    return standing;
}

} // namespace legend_of_clone
