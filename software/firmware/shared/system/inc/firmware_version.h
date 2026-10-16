/**
 * @file firmware_version.h
 * @brief Version of the DualSlide firmware.
 *
 * Firmware versions follow semantic versioning:
 *
 *   MAJOR  changes that end support for existing games (a new game API major
 *          version, game_api.h)
 *   MINOR  new features, including additions to the game API
 *   PATCH  fixes only
 *
 * The firmware version is what players see and what game-compatibility
 * messages quote. Raise it with every release.
 */

#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_VERSION_MAJOR              (1U)
#define FIRMWARE_VERSION_MINOR              (0U)
#define FIRMWARE_VERSION_PATCH              (0U)

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_VERSION_H */
