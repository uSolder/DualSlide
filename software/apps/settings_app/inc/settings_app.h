/**
 * @file settings_app.h
 * @brief Settings application: screen brightness, volume, and erasing scores.
 *
 * Registered with the application manager like a game, so it appears as a
 * channel in the launcher and has both sliders and both buttons to itself.
 */

#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include "app_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The app, as registered with the application manager. */
extern const AppManager_AppTypeDef SettingsApp_App;

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_APP_H */
