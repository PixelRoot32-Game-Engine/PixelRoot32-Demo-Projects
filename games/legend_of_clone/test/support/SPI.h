#pragma once

/**
 * @brief Empty stand-in for <SPI.h>, for the host_test environment only.
 *
 * graphics/DisplayConfig.h includes it on every non-native target, and it is
 * reached only because Font5x7.h and FontManager.cpp include Renderer.h for
 * the Sprite definition. Nothing under test touches SPI.
 */
