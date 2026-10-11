#pragma once

#include "audio/AudioMusicTypes.h"

/**
 * @file TetrisMusic.h
 * @brief Musica de fondo original para la demo de Tetris (CC0 1.0).
 *
 * Diseno (no es la melodia NES literal):
 * - La melodia original de NES (Korobeiniki, cancion popular rusa del s. XIX,
 *   arreglo de Hirokazu Tanaka a ~144 BPM con lead pulse agudo y saltos de
 *   octava) es muy pegadiza y compite con la concentracion: ritmo rapido,
 *   registro alto, ataque brillante y percusion marcada.
 * - Esta pieza conserva solo el gesto evocador (contorno descendente
 *   E-D-C-B-A en La menor y progresion Am-F-C-G) pero ralentizado a 96 BPM,
 *   con notas largas (blancas/redondas), registro medio (A4-E5), timbre
 *   suave (triangle pad, sin vibrato agresivo) y SIN percusion: nada de
 *   kick/snare que marque el pulso de forma invasiva.
 * - Estructura: 8 compases de 4/4 = 32 beats en loop sin cortes (termina en
 *   tonica + silencio de 1 beat). Tres voces en slots 0-2 del secuenciador;
 *   los SFX del juego (move/rotate/lock/clear) usan los slots 4-7, asi que
 *   la musica nunca los interrumpe ni es interrumpida por ellos.
 * - Volumenes por nota deliberadamente bajos (lead 0.20, bajo 0.26,
 *   armonia 0.12) + master 0.6 desde la escena: fondo, no protagonista.
 */
namespace tetris::music {

namespace a = pixelroot32::audio;

// Duraciones en beats (negra = 1.0 segun ApuCore).
constexpr float Q = 1.0f;  // negra
constexpr float H = 2.0f;  // blanca
constexpr float W = 4.0f;  // redonda
constexpr float E = 0.5f;  // corchea

constexpr float LEAD_VOL = 0.20f;
constexpr float BASS_VOL = 0.26f;
constexpr float HARMONY_VOL = 0.12f;

#define TETRIS_LEAD(n, o, d) \
    a::MusicNote { a::Note::n, o, d, LEAD_VOL, &a::INSTR_TRIANGLE_PAD }
#define TETRIS_BASS(n, o, d) \
    a::MusicNote { a::Note::n, o, d, BASS_VOL, &a::INSTR_TRIANGLE_BASS }
#define TETRIS_HARMONY(n, o, d) \
    a::MusicNote { a::Note::n, o, d, HARMONY_VOL, &a::INSTR_PULSE_HARMONY }

// Voz principal: contorno descendente E-D-C-B-A (eco de Korobeiniki) en
// frases de 4 beats con silencios de respiracion. Sin semicorcheas ni
// saltos de octava: todo se mueve por grados conjuntos o terceras.
static const a::MusicNote LEAD_NOTES[] = {
    // Compas 1: E5 - D5 - C5 - B4
    TETRIS_LEAD(E, 5, 1.5f),
    TETRIS_LEAD(D, 5, 0.5f),
    TETRIS_LEAD(C, 5, 1.0f),
    TETRIS_LEAD(B, 4, 1.0f),
    // Compas 2: resolucion parcial a A4 + respiracion
    TETRIS_LEAD(A, 4, 2.0f),
    a::makeRest(1.0f),
    TETRIS_LEAD(C, 5, 1.0f),
    // Compas 3: repite el gesto, cae a A4
    TETRIS_LEAD(E, 5, 1.5f),
    TETRIS_LEAD(D, 5, 0.5f),
    TETRIS_LEAD(C, 5, 1.0f),
    TETRIS_LEAD(A, 4, 1.0f),
    // Compas 4: G4 - A4, semicadencia
    TETRIS_LEAD(G, 4, 2.0f),
    TETRIS_LEAD(A, 4, 1.0f),
    a::makeRest(1.0f),
    // Compas 5: como el 1 (variante de la segunda mitad)
    TETRIS_LEAD(E, 5, 1.5f),
    TETRIS_LEAD(D, 5, 0.5f),
    TETRIS_LEAD(C, 5, 1.0f),
    TETRIS_LEAD(B, 4, 1.0f),
    // Compas 6: A4 largo + E4 como apoyo grave
    TETRIS_LEAD(A, 4, 2.0f),
    TETRIS_LEAD(E, 4, 1.0f),
    a::makeRest(1.0f),
    // Compas 7: D5 - C5 - B4 (tension final suave)
    TETRIS_LEAD(D, 5, 2.0f),
    TETRIS_LEAD(C, 5, 1.0f),
    TETRIS_LEAD(B, 4, 1.0f),
    // Compas 8: tonica larga + silencio para un loop sin cortes
    TETRIS_LEAD(A, 4, 3.0f),
    a::makeRest(1.0f),
};

// Bajo: fundamentales en redondas (Am - F - C - G | Am - F - G - Am).
// Sin movimiento de corcheas: solo sostiene la armonia.
static const a::MusicNote BASS_NOTES[] = {
    TETRIS_BASS(A, 2, W),
    TETRIS_BASS(F, 2, W),
    TETRIS_BASS(C, 3, W),
    TETRIS_BASS(G, 2, W),
    TETRIS_BASS(A, 2, W),
    TETRIS_BASS(F, 2, W),
    TETRIS_BASS(G, 2, W),
    TETRIS_BASS(A, 2, W),
};

// Armonia: una tercera/quinta por compas en el tercer beat, el resto
// silencio. Rellena sin distraer; duty 12.5% a volumen minimo.
static const a::MusicNote HARMONY_NOTES[] = {
    a::makeRest(H), TETRIS_HARMONY(C, 5, Q), a::makeRest(Q),
    a::makeRest(H), TETRIS_HARMONY(A, 4, Q), a::makeRest(Q),
    a::makeRest(H), TETRIS_HARMONY(G, 4, Q), a::makeRest(Q),
    a::makeRest(H), TETRIS_HARMONY(B, 4, Q), a::makeRest(Q),
    a::makeRest(H), TETRIS_HARMONY(C, 5, Q), a::makeRest(Q),
    a::makeRest(H), TETRIS_HARMONY(A, 4, Q), a::makeRest(Q),
    a::makeRest(H), TETRIS_HARMONY(G, 4, Q), a::makeRest(Q),
    a::makeRest(H), TETRIS_HARMONY(A, 4, Q), a::makeRest(Q),
};

#undef TETRIS_LEAD
#undef TETRIS_BASS
#undef TETRIS_HARMONY

static const a::MusicTrack BASS_TRACK = {
    BASS_NOTES,
    sizeof(BASS_NOTES) / sizeof(a::MusicNote),
    true,
    a::WaveType::TRIANGLE,
    0.5f,
    nullptr,
    nullptr,
    nullptr,
};

static const a::MusicTrack HARMONY_TRACK = {
    HARMONY_NOTES,
    sizeof(HARMONY_NOTES) / sizeof(a::MusicNote),
    true,
    a::WaveType::PULSE,
    0.125f,
    nullptr,
    nullptr,
    nullptr,
};

// Pista principal: lead + bajo + armonia, sin percusion (nullptr).
// BPM y volumen master los fija la escena (96 BPM, master 0.6).
static const a::MusicTrack BGM_TRACK = {
    LEAD_NOTES,
    sizeof(LEAD_NOTES) / sizeof(a::MusicNote),
    true,
    a::WaveType::TRIANGLE,
    0.5f,
    &BASS_TRACK,
    &HARMONY_TRACK,
    nullptr,  // sin percusion a proposito: el pulso marcado distrae
};

constexpr float BGM_BPM = 96.0f;
constexpr float BGM_MASTER_VOLUME = 0.6f;

}  // namespace tetris::music
