/**
 * @file controls.h
 * @brief The two buttons and two sliders, ready for games to read.
 *
 * QUICK START
 *
 *   Read the controls in your app's Update function:
 *
 *       if(Controls_WasPressed(CONTROLS_PRIMARY))      // went down this frame
 *       {
 *           Jump();
 *       }
 *
 *       PaddleY = Controls_SliderBetween(CONTROLS_RIGHT_SLIDER, 400, 0);  // bottom of travel 400, top 0
 *
 *   Buttons:
 *       Controls_IsDown(Button)            held right now
 *       Controls_WasPressed(Button)        pressed this frame
 *       Controls_WasReleased(Button)       let go this frame
 *       Controls_HeldMilliseconds(Button)  how long it has been held (or was, as it is let go)
 *
 *   Sliders:
 *       Controls_Slider(Slider)                     0.0 at the bottom to 1.0 at the top
 *       Controls_SliderBetween(Slider, Bottom, Top) the same, scaled to any range
 *       Controls_SliderMoved(Slider)                has it been moved yet?
 *
 * THE SYSTEM GESTURE
 *
 *   Holding both buttons together returns to the menu, so a press that
 *   becomes part of it is taken away from the game: the buttons stop reading
 *   as down and their release is not reported. Likewise a button still held
 *   from launching the game does nothing until it is let go.
 *
 * Values change once per frame, before the app's Update.
 */

#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The buttons. */
typedef enum
{
    CONTROLS_PRIMARY = 0,
    CONTROLS_SECONDARY,
    CONTROLS_BUTTON_COUNT
} Controls_ButtonTypeDef;

/** The sliders. */
typedef enum
{
    CONTROLS_LEFT_SLIDER = 0,
    CONTROLS_RIGHT_SLIDER,
    CONTROLS_SLIDER_COUNT
} Controls_SliderTypeDef;

/** Highest raw slider reading, from Controls_SliderRaw(), at the top of its travel. */
#define CONTROLS_SLIDER_RAW_MAXIMUM         (65535)

/* -------------------------------------------------------------------------- */
/* Buttons                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Return whether a button is held down.
 */
bool Controls_IsDown(Controls_ButtonTypeDef Button);

/**
 * @brief Return whether a button went down this frame.
 */
bool Controls_WasPressed(Controls_ButtonTypeDef Button);

/**
 * @brief Return whether a button was let go this frame.
 */
bool Controls_WasReleased(Controls_ButtonTypeDef Button);

/**
 * @brief Return how long a button has been held, or 0 when it is up.
 *
 * On the frame it is let go, this is how long the press lasted, so a tap
 * and a hold can be told apart: Controls_WasReleased() with a short time.
 */
uint32_t Controls_HeldMilliseconds(Controls_ButtonTypeDef Button);

/**
 * @brief Make the buttons that are down now do nothing until they are let go.
 *
 * The system does this when an app starts, so apps rarely need it.
 */
void Controls_IgnoreHeldButtons(void);

/* -------------------------------------------------------------------------- */
/* Sliders                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief A slider's position: 0.0 at the bottom of its travel, 1.0 at the top.
 */
float Controls_Slider(Controls_SliderTypeDef Slider);

/**
 * @brief A slider's position scaled from AtBottom to AtTop, rounded.
 *
 * Either end can be the larger: Controls_SliderBetween(Slider, 400, 0) gives
 * a screen Y that is 0 when the slider is at the top.
 */
int32_t Controls_SliderBetween(Controls_SliderTypeDef Slider, int32_t AtBottom, int32_t AtTop);

/**
 * @brief A slider's raw reading, 0 at the bottom to CONTROLS_SLIDER_RAW_MAXIMUM at the top.
 */
int32_t Controls_SliderRaw(Controls_SliderTypeDef Slider);

/**
 * @brief Return whether a slider has moved since the app started (or since
 *        Controls_ResetSliderMoved()).
 *
 * Use it to leave a setting alone until the player actually moves the
 * slider, rather than jumping to wherever it was left.
 */
bool Controls_SliderMoved(Controls_SliderTypeDef Slider);

/**
 * @brief Start watching a slider for movement again from where it is now.
 */
void Controls_ResetSliderMoved(Controls_SliderTypeDef Slider);

/* -------------------------------------------------------------------------- */
/* System                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read the controls for the first time; buttons already down are ignored.
 */
void Controls_Init(void);

/**
 * @brief Read the controls for a new frame. Called by the system loop.
 */
void Controls_Update(uint32_t DeltaTimeMilliseconds);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLS_H */
