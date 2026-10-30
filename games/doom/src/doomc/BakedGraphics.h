#pragma once

/*
 * The baked-graphics adapter (D3.8): a packed name becomes an r_walltex_t, an
 * r_flat_t or an r_sprite_patch_t that points straight into the container's
 * flash mapping.
 *
 * WHY THIS LAYER EXISTS
 *
 * D3.5 measured the cost of composing a texture per frame: 13,044 us of
 * composition plus 114,845 us of inflate on a 166,261 us frame. A composite is
 * written once at build time by tools/bake_graphics.py and stored raw, so at
 * run time a texture resolve is a manifest lookup and a pointer. Nothing here
 * decodes, copies or allocates.
 *
 * THE MANIFEST
 *
 * One raw container lump, "~PWBAKE~" (eight characters, and the '~' is a byte
 * no IWAD lump name uses, so it cannot collide), holds a 16-byte header and
 * 20-byte records:
 *
 *   header   magic "PWBK", version 1, record size 20, count, reserved
 *   record   name[8], width (2), rows (2), flags (1), kind (1),
 *            reserved (2), lumpIndex (4)
 *
 * All integers little-endian. `rows` is the texture's *declared* height for a
 * wall or sky, 64 for a flat and 0 for a sprite; the stored payload is always
 * 128 rows for a wall/sky because R_DrawColumn() wraps with a hard-coded
 * `& 127`.
 *
 * Bit 0 of `flags` means a mask plane follows the pixels. The mask is stored
 * only when the composite leaves at least one texel unwritten, and its rows
 * above the declared height are 1: those rows are the opaque zero fill, not a
 * hole, and a 0 there would change a masked mid texture's pixels.
 *
 * Kind values are 1 wall, 2 sky, 3 flat, 4 sprite. The sky is a wall texture
 * to the renderer (r_plane.c resolves it through r_tex_source_t::resolve), so
 * resolveWall() serves both kinds. Flats and sprites are their own namespaces;
 * a name may therefore repeat across kinds, but not inside one.
 *
 * WHAT IT REFUSES
 *
 * A manifest that is truncated, has the wrong magic/version/record size, a
 * duplicate name inside a kind, an unknown kind, a shape that disagrees with
 * its kind, or a lump index outside the container is rejected by open(). A
 * payload that is not raw, or whose size is not what the record implies, is
 * rejected too: the port must never silently fall back to decoding.
 */

#include <cstddef>
#include <cstdint>

#include "r_data.h"
#include "r_defs.h"
#include "r_plane.h"
#include "r_segs.h"
#include "r_things.h"
#include "whd.h"

namespace doom {
namespace doomc {

constexpr std::size_t kBakedNameSize = 8u;
constexpr const char* kBakedManifestName = "~PWBAKE~";
constexpr std::uint16_t kBakedManifestVersion = 1u;
constexpr std::size_t kBakedManifestHeaderSize = 16u;
constexpr std::size_t kBakedManifestRecordSize = 20u;

/// The manifest's capacity in this port: the union of E1M1..E1M9 needs 187
/// entries (100 textures + the sky, 50 flats and 36 sprite lumps), so 256 leaves
/// room for the rest of the episode without a heap allocation. D3.8's 96 was
/// E1M1's 90 plus headroom, and the whole-episode bake outgrew it.
constexpr int kBakedCapacity = 256;

constexpr std::uint8_t kBakedFlagMask = 0x01u;

enum BakedKind : std::uint8_t {
    kBakedKindWall = 1,
    kBakedKindSky = 2,
    kBakedKindFlat = 3,
    kBakedKindSprite = 4
};

enum class BakedError : std::uint8_t {
    Ok = 0,
    Null,
    TooSmall,
    BadMagic,
    BadVersion,
    BadRecordSize,
    BadReserved,
    TooMany,
    Truncated,
    DuplicateName,
    BadKind,
    BadShape,
    IndexOutOfRange,
    NotRaw,
    SizeMismatch,
    ManifestMissing
};

const char* bakedErrorName(BakedError error);

/// One parsed manifest record, copied out of the raw lump.
struct BakedEntry {
    char name[kBakedNameSize + 1u];
    std::uint16_t width;
    std::uint16_t rows;
    std::uint8_t flags;
    std::uint8_t kind;
    std::uint32_t lumpIndex;
};

struct BakedManifest {
    BakedEntry entries[kBakedCapacity];
    int count;
};

/**
 * @brief Parses and validates one manifest payload.
 *
 * @param data      Manifest bytes (the raw lump).
 * @param size      Bytes available at @p data.
 * @param lumpCount How many lumps the container holds, so every record's index
 *                  can be proved to exist.
 * @param out       Caller-owned result; written only on BakedError::Ok.
 */
BakedError bakedManifestParse(const std::uint8_t* data, std::size_t size,
                              std::uint32_t lumpCount, BakedManifest* out);

/**
 * @brief Resolves baked names to renderer records over one open container.
 *
 * The object holds the manifest and one prebuilt record per entry; open() does
 * all the `whd_lump_view()` work once, so a resolve is a binary search over at
 * most 96 packed names. It owns no memory and allocates nothing.
 */
class BakedGraphics {
public:
    /// Finds the manifest lump and prebuilds every record. Idempotent.
    BakedError open(const whd_state_t* container);

    /// Wall and sky textures; the sky is served here because r_plane.c asks
    /// r_tex_source_t::resolve for it.
    const r_walltex_t* resolveWall(r_name_t name) const;

    /// 64 x 64 flats.
    const r_flat_t* resolveFlat(r_name_t name) const;

    /// Raw sprite patch lumps (no compose).
    const r_sprite_patch_t* resolveSprite(r_name_t name) const;

    /// How many manifest records this adapter holds, by kind or in total.
    int count(BakedKind kind) const;
    int entryCount() const { return count_; }

    /// Payload bytes the manifest's records point at, and the manifest size.
    std::uint32_t payloadBytes() const { return payloadBytes_; }
    std::uint32_t manifestBytes() const { return manifestBytes_; }

private:
    struct SortedView {
        int indices[kBakedCapacity];
        int count;
    };

    union Value {
        r_walltex_t wall;
        r_flat_t flat;
        r_sprite_patch_t sprite;
    };

    BakedEntry entries_[kBakedCapacity];
    Value values_[kBakedCapacity];
    SortedView walls_;
    SortedView flats_;
    SortedView sprites_;
    int count_ = 0;
    std::uint32_t payloadBytes_ = 0u;
    std::uint32_t manifestBytes_ = 0u;

    static int find(const SortedView& view, const BakedEntry* entries, r_name_t name);
};

}  // namespace doomc
}  // namespace doom
