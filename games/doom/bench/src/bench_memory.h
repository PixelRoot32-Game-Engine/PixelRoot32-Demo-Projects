// D0.3 (fill throughput) and D0.4 (DRAM census).
#pragma once

namespace bench {

/// Times memset, a plain byte-store loop and a byte-store loop through a
/// 256-entry LUT over 320x200 and 240x180 buffers. 1,000 iterations each.
void runFillProbe();

/// Reports internal DRAM before drawer init, after drawer init, and after a
/// second 320x200 8bpp surface, then checks PLAN §5's 300 KB gate.
void runDramCensus();

} // namespace bench
