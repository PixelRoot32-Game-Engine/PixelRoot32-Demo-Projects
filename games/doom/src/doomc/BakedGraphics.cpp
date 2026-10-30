/*
 * The baked-graphics adapter: see BakedGraphics.h for the manifest format, the
 * mask rule and the failure contract.
 *
 * Everything here is plain C++ over the C headers. There is no allocation, no
 * global, no ESP-IDF dependency and no decode path: open() validates the
 * manifest and every payload once, and a resolve is a binary search over packed
 * names followed by a pointer to the record open() built.
 */
#include "doomc/BakedGraphics.h"

#include <cstring>

namespace doom {
namespace doomc {

namespace {

std::uint16_t readLe16(const std::uint8_t* at) {
    return static_cast<std::uint16_t>(at[0])
           | static_cast<std::uint16_t>(static_cast<std::uint16_t>(at[1]) << 8);
}

std::uint32_t readLe32(const std::uint8_t* at) {
    return static_cast<std::uint32_t>(at[0])
           | (static_cast<std::uint32_t>(at[1]) << 8)
           | (static_cast<std::uint32_t>(at[2]) << 16)
           | (static_cast<std::uint32_t>(at[3]) << 24);
}

bool isKnownKind(std::uint8_t kind) {
    return kind == kBakedKindWall || kind == kBakedKindSky
           || kind == kBakedKindFlat || kind == kBakedKindSprite;
}

/// The storage rows of every wall/sky payload, from r_segs.h's `& 127` contract.
constexpr std::uint32_t kStoredRows = 128u;

/// A wall/sky record's payload size, mask included when flagged.
std::uint32_t expectedWallBytes(const BakedEntry& entry) {
    const std::uint32_t planes = (entry.flags & kBakedFlagMask) != 0u ? 2u : 1u;
    return static_cast<std::uint32_t>(entry.width) * kStoredRows * planes;
}

}  // namespace

const char* bakedErrorName(BakedError error) {
    switch (error) {
        case BakedError::Ok:
            return "ok";
        case BakedError::Null:
            return "null";
        case BakedError::TooSmall:
            return "too_small";
        case BakedError::BadMagic:
            return "bad_magic";
        case BakedError::BadVersion:
            return "bad_version";
        case BakedError::BadRecordSize:
            return "bad_record_size";
        case BakedError::BadReserved:
            return "bad_reserved";
        case BakedError::TooMany:
            return "too_many";
        case BakedError::Truncated:
            return "truncated";
        case BakedError::DuplicateName:
            return "duplicate_name";
        case BakedError::BadKind:
            return "bad_kind";
        case BakedError::BadShape:
            return "bad_shape";
        case BakedError::IndexOutOfRange:
            return "index_out_of_range";
        case BakedError::NotRaw:
            return "not_raw";
        case BakedError::SizeMismatch:
            return "size_mismatch";
        case BakedError::ManifestMissing:
            return "manifest_missing";
    }
    return "unknown";
}

BakedError bakedManifestParse(const std::uint8_t* data, std::size_t size,
                              std::uint32_t lumpCount, BakedManifest* out) {
    if (data == nullptr || out == nullptr) {
        return BakedError::Null;
    }
    out->count = 0;
    if (size < kBakedManifestHeaderSize) {
        return BakedError::TooSmall;
    }
    if (data[0] != 'P' || data[1] != 'W' || data[2] != 'B' || data[3] != 'K') {
        return BakedError::BadMagic;
    }

    const std::uint16_t version = readLe16(data + 4);
    const std::uint16_t recordSize = readLe16(data + 6);
    const std::uint32_t count = readLe32(data + 8);
    const std::uint32_t reserved = readLe32(data + 12);

    if (version != kBakedManifestVersion) {
        return BakedError::BadVersion;
    }
    if (recordSize != kBakedManifestRecordSize) {
        return BakedError::BadRecordSize;
    }
    if (reserved != 0u) {
        return BakedError::BadReserved;
    }
    if (count > static_cast<std::uint32_t>(kBakedCapacity)) {
        return BakedError::TooMany;
    }
    if (kBakedManifestHeaderSize + static_cast<std::size_t>(count) * recordSize > size) {
        return BakedError::Truncated;
    }

    for (std::uint32_t index = 0u; index < count; ++index) {
        const std::uint8_t* at = data + kBakedManifestHeaderSize
                                 + static_cast<std::size_t>(index) * recordSize;
        BakedEntry& entry = out->entries[index];

        std::memcpy(entry.name, at, kBakedNameSize);
        entry.name[kBakedNameSize] = '\0';
        entry.width = readLe16(at + 8);
        entry.rows = readLe16(at + 10);
        entry.flags = at[12];
        entry.kind = at[13];
        const std::uint16_t entryReserved = readLe16(at + 14);
        entry.lumpIndex = readLe32(at + 16);

        if (entry.name[0] == '\0') {
            return BakedError::BadShape;
        }
        if (entryReserved != 0u) {
            return BakedError::BadReserved;
        }
        if ((entry.flags & ~kBakedFlagMask) != 0u) {
            return BakedError::BadShape;
        }
        if (!isKnownKind(entry.kind)) {
            return BakedError::BadKind;
        }
        if (entry.kind == kBakedKindWall || entry.kind == kBakedKindSky) {
            if (entry.width < 1u || entry.rows < 1u || entry.rows > kStoredRows) {
                return BakedError::BadShape;
            }
        } else if (entry.kind == kBakedKindFlat) {
            if (entry.width != static_cast<std::uint16_t>(R_FLAT_SIZE)
                || entry.rows != static_cast<std::uint16_t>(R_FLAT_SIZE)
                || entry.flags != 0u) {
                return BakedError::BadShape;
            }
        } else if (entry.width != 0u || entry.rows != 0u || entry.flags != 0u) {
            return BakedError::BadShape;
        }
        if (entry.lumpIndex >= lumpCount) {
            return BakedError::IndexOutOfRange;
        }
    }

    // Names are unique inside a kind; a wall texture and a flat may legitimately
    // share a name because the renderer looks them up in different namespaces.
    for (std::uint32_t left = 0u; left < count; ++left) {
        for (std::uint32_t right = left + 1u; right < count; ++right) {
            if (out->entries[left].kind == out->entries[right].kind
                && std::memcmp(out->entries[left].name, out->entries[right].name,
                               kBakedNameSize) == 0) {
                return BakedError::DuplicateName;
            }
        }
    }

    out->count = static_cast<int>(count);
    return BakedError::Ok;
}

int BakedGraphics::find(const SortedView& view, const BakedEntry* entries, r_name_t name) {
    int low = 0;
    int high = view.count - 1;

    while (low <= high) {
        const int middle = low + (high - low) / 2;
        const int at = view.indices[middle];
        const r_name_t key = r_pack_name(reinterpret_cast<const std::uint8_t*>(entries[at].name));
        if (key == name) {
            return at;
        }
        if (key < name) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return -1;
}

BakedError BakedGraphics::open(const whd_state_t* container) {
    count_ = 0;
    payloadBytes_ = 0u;
    manifestBytes_ = 0u;
    walls_ = SortedView{};
    flats_ = SortedView{};
    sprites_ = SortedView{};

    if (container == nullptr) {
        return BakedError::Null;
    }

    const int manifestLump = whd_lump_index_for_name(container, kBakedManifestName);
    if (manifestLump < 0) {
        return BakedError::ManifestMissing;
    }

    const std::uint8_t* manifestData = nullptr;
    std::uint32_t manifestBytes = 0u;
    if (whd_lump_view(container, manifestLump, &manifestData, &manifestBytes) != WHD_ERR_OK) {
        return BakedError::NotRaw;
    }
    manifestBytes_ = manifestBytes;

    BakedManifest manifest;
    const BakedError parsed = bakedManifestParse(
        manifestData, manifestBytes,
        static_cast<std::uint32_t>(whd_lump_count(container)), &manifest);
    if (parsed != BakedError::Ok) {
        return parsed;
    }

    for (int index = 0; index < manifest.count; ++index) {
        const BakedEntry& entry = manifest.entries[index];
        entries_[index] = entry;

        const std::uint8_t* data = nullptr;
        std::uint32_t size = 0u;
        if (whd_lump_view(container, static_cast<int>(entry.lumpIndex), &data, &size)
            != WHD_ERR_OK) {
            return BakedError::NotRaw;
        }
        payloadBytes_ += size;

        if (entry.kind == kBakedKindWall || entry.kind == kBakedKindSky) {
            if (size != expectedWallBytes(entry)) {
                return BakedError::SizeMismatch;
            }
            r_walltex_t& wall = values_[index].wall;
            wall.pixels = data;
            wall.mask = (entry.flags & kBakedFlagMask) != 0u
                            ? data + static_cast<std::size_t>(entry.width) * kStoredRows
                            : nullptr;
            wall.width = static_cast<std::int16_t>(entry.width);
            wall.height = static_cast<std::int16_t>(entry.rows);
            wall.widthMask = r_texture_width_mask(static_cast<int>(entry.width));
            wall.heightMask = static_cast<std::int16_t>(kStoredRows - 1u);

            const int16_t expectedWidthMask =
                r_texture_width_mask(static_cast<int>(entry.width));
            if (wall.widthMask != expectedWidthMask || wall.heightMask < wall.height - 1) {
                return BakedError::BadShape;
            }
        } else if (entry.kind == kBakedKindFlat) {
            if (size != static_cast<std::uint32_t>(R_FLAT_BYTES)) {
                return BakedError::SizeMismatch;
            }
            r_flat_t& flat = values_[index].flat;
            flat.pixels = data;
            flat.width = R_FLAT_SIZE;
            flat.height = R_FLAT_SIZE;
            flat.widthMask = R_FLAT_SIZE - 1;
            flat.heightMask = R_FLAT_SIZE - 1;
        } else {
            if (size == 0u) {
                return BakedError::SizeMismatch;
            }
            r_sprite_patch_t& sprite = values_[index].sprite;
            sprite.data = data;
            sprite.size = static_cast<std::size_t>(size);
        }
    }

    count_ = manifest.count;
    for (int index = 0; index < count_; ++index) {
        const BakedEntry& entry = entries_[index];
        SortedView* view = nullptr;
        if (entry.kind == kBakedKindWall || entry.kind == kBakedKindSky) {
            view = &walls_;
        } else if (entry.kind == kBakedKindFlat) {
            view = &flats_;
        } else {
            view = &sprites_;
        }

        // Insertion sort on at most 96 entries: the manifest is build-time data
        // and this runs once, so a simple stable sort is clearer than a heap.
        int position = view->count;
        const r_name_t key = r_pack_name(reinterpret_cast<const std::uint8_t*>(entry.name));
        while (position > 0) {
            const int previous = view->indices[position - 1];
            const r_name_t previousKey = r_pack_name(
                reinterpret_cast<const std::uint8_t*>(entries_[previous].name));
            if (previousKey <= key) {
                break;
            }
            view->indices[position] = previous;
            --position;
        }
        view->indices[position] = index;
        ++view->count;
    }

    return BakedError::Ok;
}

const r_walltex_t* BakedGraphics::resolveWall(r_name_t name) const {
    if (name == R_NAME_NONE) {
        return nullptr;
    }
    const int index = find(walls_, entries_, name);
    return index < 0 ? nullptr : &values_[index].wall;
}

const r_flat_t* BakedGraphics::resolveFlat(r_name_t name) const {
    if (name == R_NAME_NONE) {
        return nullptr;
    }
    const int index = find(flats_, entries_, name);
    return index < 0 ? nullptr : &values_[index].flat;
}

const r_sprite_patch_t* BakedGraphics::resolveSprite(r_name_t name) const {
    if (name == R_NAME_NONE) {
        return nullptr;
    }
    const int index = find(sprites_, entries_, name);
    return index < 0 ? nullptr : &values_[index].sprite;
}

int BakedGraphics::count(BakedKind kind) const {
    int total = 0;
    for (int index = 0; index < count_; ++index) {
        if (entries_[index].kind == static_cast<std::uint8_t>(kind)) {
            ++total;
        }
    }
    return total;
}

}  // namespace doomc
}  // namespace doom
