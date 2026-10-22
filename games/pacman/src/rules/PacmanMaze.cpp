#include "PacmanMaze.h"

#include <cstring>

namespace pacman {

namespace {

/* Layout legend. Kept as named constants so that the validator and the
 * renderer agree on what a character means without repeating the literals. */
constexpr char kCharWall = '#';
constexpr char kCharDot = '.';
constexpr char kCharPowerPellet = 'o';
constexpr char kCharEmptyPath = ' ';
constexpr char kCharTunnel = '-';
constexpr char kCharHouseWall = 'H';
constexpr char kCharHouseDoor = '=';
constexpr char kCharHouse = '_';

/**
 * @brief The built-in level layout, one string per row.
 *
 * This is the arcade's own maze: 28 columns by 31 rows, the one the 1980
 * Namco cabinet played, with the score rows above it and the lives rows
 * below it left out. The level holds exactly the arcade's 240 dots and 4
 * energizers.
 *
 * The maze is drawn as the outline of its wall regions, so a wall block
 * reads as the corridor the arcade draws it as.
 */
const char* const kLevelLayout[kMazeRows] = {
    "############################",  // 0
    "#............##............#",  // 1
    "#.####.#####.##.#####.####.#",  // 2
    "#o####.#####.##.#####.####o#",  // 3
    "#.####.#####.##.#####.####.#",  // 4
    "#..........................#",  // 5
    "#.####.##.########.##.####.#",  // 6
    "#.####.##.########.##.####.#",  // 7
    "#......##....##....##......#",  // 8
    "######.##### ## #####.######",  // 9
    "######.##### ## #####.######",  // 10
    "######.##          ##.######",  // 11
    "######.## HHH==HHH ##.######",  // 12
    "######.## H______H ##.######",  // 13
    "-     .   H______H   .     -",  // 14
    "######.## H______H ##.######",  // 15
    "######.## HHHHHHHH ##.######",  // 16
    "######.##          ##.######",  // 17
    "######.## ######## ##.######",  // 18
    "######.## ######## ##.######",  // 19
    "#............##............#",  // 20
    "#.####.#####.##.#####.####.#",  // 21
    "#.####.#####.##.#####.####.#",  // 22
    "#o..##.......  .......##..o#",  // 23
    "###.##.##.########.##.##.###",  // 24
    "###.##.##.########.##.##.###",  // 25
    "#......##....##....##......#",  // 26
    "#.##########.##.##########.#",  // 27
    "#.##########.##.##########.#",  // 28
    "#..........................#",  // 29
    "############################",  // 30
};

/**
 * @brief Maps a layout character onto its terrain type.
 *
 * @param ch The character read from the layout.
 * @return The terrain type it stands for.
 */
constexpr CellType cellTypeForChar(char ch) {
    switch (ch) {
        case kCharWall:
            return CellType::Wall;
        case kCharHouseWall:
            return CellType::HouseWall;
        case kCharTunnel:
            return CellType::Tunnel;
        case kCharHouseDoor:
            return CellType::HouseDoor;
        case kCharHouse:
            return CellType::House;
        default:
            return CellType::Path;
    }
}

}  // namespace

const char* const* PacmanMaze::levelLayout() {
    return kLevelLayout;
}

const PacmanLevelConfig& PacmanMaze::defaultLevel() {
    static const PacmanLevelConfig kDefault = {
        kLevelLayout,
        kMazeRows,
        kMazeCols,
        kPlayerStartCol,
        kPlayerStartRow,
        kEnemyCount,
        {kEnemyStartCols[0], kEnemyStartCols[1], kEnemyStartCols[2], kEnemyStartCols[3]},
        {kEnemyStartRows[0], kEnemyStartRows[1], kEnemyStartRows[2], kEnemyStartRows[3]},
    };
    return kDefault;
}

PacmanMaze::PacmanMaze() : PacmanMaze(defaultLevel()) {}

PacmanMaze::PacmanMaze(const PacmanLevelConfig& config) : config_(&config) {
    // The ghost house has a wall of its own and the renderer draws it as a
    // closed rectangle, so the maze measures where it is.
    house_.leftCol = config.colCount;
    house_.rightCol = -1;
    house_.topRow = config.rowCount;
    house_.bottomRow = -1;
    for (int row = 0; row < config.rowCount; ++row) {
        for (int col = 0; col < config.colCount; ++col) {
            if (cellType(col, row) != CellType::HouseWall) {
                continue;
            }
            if (col < house_.leftCol) {
                house_.leftCol = col;
            }
            if (col > house_.rightCol) {
                house_.rightCol = col;
            }
            if (row < house_.topRow) {
                house_.topRow = row;
            }
            if (row > house_.bottomRow) {
                house_.bottomRow = row;
            }
        }
    }
}

bool PacmanMaze::isInside(int col, int row) const {
    return col >= 0 && col < config_->colCount && row >= 0 && row < config_->rowCount;
}

CellType PacmanMaze::cellType(int col, int row) const {
    if (!isInside(col, row)) {
        return CellType::Wall;
    }
    return cellTypeForChar(config_->rows[row][col]);
}

bool PacmanMaze::isWall(int col, int row) const {
    return cellType(col, row) == CellType::Wall;
}

bool PacmanMaze::isWalkableForPlayer(int col, int row) const {
    const CellType type = cellType(col, row);
    return type == CellType::Path || type == CellType::Tunnel;
}

bool PacmanMaze::isWalkableForEnemy(int col, int row) const {
    const CellType type = cellType(col, row);
    return type == CellType::Path || type == CellType::Tunnel ||
           type == CellType::HouseDoor || type == CellType::House;
}

bool PacmanMaze::isTunnel(int col, int row) const {
    return cellType(col, row) == CellType::Tunnel;
}

Collectible PacmanMaze::collectibleAt(int col, int row) const {
    if (!isInside(col, row)) {
        return Collectible::None;
    }
    const char ch = config_->rows[row][col];
    if (ch == kCharDot) {
        return Collectible::Dot;
    }
    if (ch == kCharPowerPellet) {
        return Collectible::PowerPellet;
    }
    return Collectible::None;
}

int PacmanMaze::collectibleCount() const {
    int count = 0;
    for (int row = 0; row < config_->rowCount; ++row) {
        for (int col = 0; col < config_->colCount; ++col) {
            const char ch = config_->rows[row][col];
            if (ch == kCharDot || ch == kCharPowerPellet) {
                ++count;
            }
        }
    }
    return count;
}

int PacmanMaze::playerStartCol() const {
    return config_->playerStartCol;
}

int PacmanMaze::playerStartRow() const {
    return config_->playerStartRow;
}

int PacmanMaze::enemyStartCol(int index) const {
    if (index < 0 || index >= config_->enemyCount) {
        return 0;
    }
    return config_->enemyStartCols[index];
}

int PacmanMaze::enemyStartRow(int index) const {
    if (index < 0 || index >= config_->enemyCount) {
        return 0;
    }
    return config_->enemyStartRows[index];
}

int PacmanMaze::enemyCount() const {
    return config_->enemyCount;
}

int PacmanMaze::colCount() const {
    return config_->colCount;
}

int PacmanMaze::rowCount() const {
    return config_->rowCount;
}

bool PacmanMaze::wrapCell(int col, int row, int& outCol, int& outRow) const {
    if (row < 0 || row >= config_->rowCount) {
        return false;
    }
    if (col >= 0 && col < config_->colCount) {
        return false;
    }

    // Only a row whose edges are tunnels may wrap. Checking the layout rather
    // than a hard-coded row keeps the rule honest: a level whose tunnels were
    // moved simply stops wrapping.
    const char leftEdge = config_->rows[row][0];
    const char rightEdge = config_->rows[row][config_->colCount - 1];
    if (leftEdge != kCharTunnel && rightEdge != kCharTunnel) {
        return false;
    }

    outCol = (col < 0) ? (config_->colCount - 1) : 0;
    outRow = row;
    return true;
}

PacmanMaze::ValidationError PacmanMaze::validate() const {
    // Every row must be exactly as wide as the level claims to be. A short
    // row would silently read past the end of the string.
    for (int row = 0; row < config_->rowCount; ++row) {
        if (std::strlen(config_->rows[row]) != static_cast<std::size_t>(config_->colCount)) {
            return ValidationError::RowLength;
        }
    }

    // The outer border must be closed. Tunnel cells are allowed to sit on the
    // border: they are how an actor leaves and re-enters the map.
    for (int col = 0; col < config_->colCount; ++col) {
        const CellType top = cellType(col, 0);
        const CellType bottom = cellType(col, config_->rowCount - 1);
        if (top != CellType::Wall && top != CellType::Tunnel) {
            return ValidationError::Border;
        }
        if (bottom != CellType::Wall && bottom != CellType::Tunnel) {
            return ValidationError::Border;
        }
    }
    for (int row = 0; row < config_->rowCount; ++row) {
        const CellType left = cellType(0, row);
        const CellType right = cellType(config_->colCount - 1, row);
        if (left != CellType::Wall && left != CellType::Tunnel) {
            return ValidationError::Border;
        }
        if (right != CellType::Wall && right != CellType::Tunnel) {
            return ValidationError::Border;
        }
    }

    // Start positions must exist and must not be inside a wall.
    if (!isInside(config_->playerStartCol, config_->playerStartRow)) {
        return ValidationError::MissingStart;
    }
    if (!isWalkableForPlayer(config_->playerStartCol, config_->playerStartRow)) {
        return ValidationError::StartInWall;
    }
    for (int index = 0; index < config_->enemyCount; ++index) {
        const int col = config_->enemyStartCols[index];
        const int row = config_->enemyStartRows[index];
        if (!isInside(col, row)) {
            return ValidationError::MissingStart;
        }
        if (!isWalkableForEnemy(col, row)) {
            return ValidationError::StartInWall;
        }
    }

    // A level with nothing to collect would end the instant it started.
    if (collectibleCount() == 0) {
        return ValidationError::NoCollectibles;
    }

    // Every cell the player can stand on must be reachable from the player's
    // start, otherwise a dot could exist that no route ever reaches.
    bool visited[kCells] = {};
    int queue[kCells];
    int head = 0;
    int tail = 0;

    const int startIndex = config_->playerStartRow * config_->colCount + config_->playerStartCol;
    visited[startIndex] = true;
    queue[tail++] = startIndex;

    while (head < tail) {
        const int index = queue[head++];
        const int row = index / config_->colCount;
        const int col = index % config_->colCount;

        const int deltas[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto& delta : deltas) {
            const int nextCol = col + delta[1];
            const int nextRow = row + delta[0];
            if (!isWalkableForPlayer(nextCol, nextRow)) {
                continue;
            }
            // A tunnel edge wraps to the opposite side of the map.
            int wrappedCol = nextCol;
            int wrappedRow = nextRow;
            if (!isInside(nextCol, nextRow)) {
                if (!wrapCell(nextCol, nextRow, wrappedCol, wrappedRow)) {
                    continue;
                }
            }
            const int nextIndex = wrappedRow * config_->colCount + wrappedCol;
            if (visited[nextIndex]) {
                continue;
            }
            visited[nextIndex] = true;
            queue[tail++] = nextIndex;
        }
    }

    for (int row = 0; row < config_->rowCount; ++row) {
        for (int col = 0; col < config_->colCount; ++col) {
            if (!isWalkableForPlayer(col, row)) {
                continue;
            }
            if (!visited[row * config_->colCount + col]) {
                return ValidationError::UnreachableCell;
            }
        }
    }

    return ValidationError::None;
}

const char* PacmanMaze::describe(ValidationError error) {
    switch (error) {
        case ValidationError::None:
            return "level is valid";
        case ValidationError::RowLength:
            return "a row does not match the declared column count";
        case ValidationError::Border:
            return "the outer border of the map is open";
        case ValidationError::MissingStart:
            return "a start position lies outside the map";
        case ValidationError::StartInWall:
            return "a start position lies inside a wall";
        case ValidationError::NoCollectibles:
            return "the level holds nothing to collect";
        case ValidationError::UnreachableCell:
            return "a walkable cell cannot be reached from the player start";
    }
    return "unknown validation error";
}

}  // namespace pacman
