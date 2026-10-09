/**
 * @file app_manager.c
 * @brief Controls the DualSlide launcher and active application lifecycle.
 */

#include "app_manager.h"

#include "controls.h"
#include "launcher.h"
#include "mixer.h"
#include "music.h"
#include "pong.h"
#include "settings_app.h"
#include "sound.h"
#include "tanks.h"
#include "template_game.h"
#include "window_washer.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Private types                                                              */
/* -------------------------------------------------------------------------- */

typedef enum
{
    APP_MANAGER_STATE_LAUNCHER = 0,
    APP_MANAGER_STATE_APPLICATION
} AppManager_StateTypeDef;

typedef struct
{
    bool (*Init)(void);
    void (*Update)(uint32_t DeltaTimeMilliseconds);
    void (*Render)(Render_TargetTypeDef *Target);
    void (*Pause)(void);
    void (*Resume)(void);
    void (*Shutdown)(void);
} AppManager_RuntimeInterfaceTypeDef;

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

static const AppManager_RuntimeInterfaceTypeDef AppManager_LauncherInterface =
{
    .Init = Launcher_Init,
    .Update = Launcher_Update,
    .Render = Launcher_Render,
    .Pause = Launcher_Pause,
    .Resume = Launcher_Resume,
    .Shutdown = Launcher_Shutdown
};

/* Every app the launcher offers, in channel order. To add a game, add it here. */
static const AppManager_AppTypeDef *const AppManager_Applications[] =
{
    &WindowWasher_App,
    &TemplateGame_App,
    &Pong_App,
    &Tanks_App,
    &SettingsApp_App
};

/* The standard palette, for apps without colours of their own; see the RENDER_ colour names. */
static const Display_ColourTypeDef AppManager_StandardPalette[RENDER_STANDARD_COLOUR_COUNT] =
{
    0x00000000U, /* RENDER_BLACK      */
    0x00FFFFFFU, /* RENDER_WHITE      */
    0x00808080U, /* RENDER_GREY       */
    0x00404040U, /* RENDER_DARK_GREY  */
    0x00C0C0C0U, /* RENDER_LIGHT_GREY */
    0x00E53935U, /* RENDER_RED        */
    0x008E1B1BU, /* RENDER_DARK_RED   */
    0x00FB8C00U, /* RENDER_ORANGE     */
    0x00FDD835U, /* RENDER_YELLOW     */
    0x0043A047U, /* RENDER_GREEN      */
    0x001B5E20U, /* RENDER_DARK_GREEN */
    0x0000BCD4U, /* RENDER_CYAN       */
    0x001E88E5U, /* RENDER_BLUE       */
    0x000D2A6BU, /* RENDER_DARK_BLUE  */
    0x008E24AAU, /* RENDER_PURPLE     */
    0x00F06292U, /* RENDER_PINK       */
    0x00795548U, /* RENDER_BROWN      */
    0x0087CEEBU  /* RENDER_SKY        */
};

static AppManager_StateTypeDef AppManager_State;
static uint16_t AppManager_ActiveApplicationIndex;
static bool AppManager_Initialized;
static bool AppManager_Paused;

/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static bool AppManager_IsApplicationIndexValid(uint16_t ApplicationIndex)
{
    return ApplicationIndex < AppManager_GetAppCount();
}

static const AppManager_AppTypeDef *AppManager_GetApplication(uint16_t ApplicationIndex)
{
    if(!AppManager_IsApplicationIndexValid(ApplicationIndex))
    {
        return NULL;
    }

    return AppManager_Applications[ApplicationIndex];
}

/* An app's colours, or the standard palette, filling the whole application range. */
static void AppManager_FillPalette(const AppManager_AppTypeDef *Application, Display_ColourTypeDef *Palette)
{
    const Display_ColourTypeDef *Colours = (Application->Palette != NULL) ? Application->Palette : AppManager_StandardPalette;
    uint16_t Count = (Application->Palette != NULL) ? Application->PaletteCount : (uint16_t)RENDER_STANDARD_COLOUR_COUNT;

    Count = (Count > APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT) ? (uint16_t)APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT : Count;

    for(uint16_t Index = 0U; Index < APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT; Index++)
    {
        Palette[Index] = (Index < Count) ? Colours[Index] : 0U;
    }
}

/* A newly shown app starts with fresh controls: buttons still held do nothing, sliders read as unmoved. */
static void AppManager_ResetControls(void)
{
    Controls_IgnoreHeldButtons();
    Controls_ResetSliderMoved(CONTROLS_LEFT_SLIDER);
    Controls_ResetSliderMoved(CONTROLS_RIGHT_SLIDER);
}

/* Acquire a frame, let Draw fill it, and present it. */
static void AppManager_RenderFrame(void (*Draw)(Render_TargetTypeDef *Target))
{
    Display_FrameTypeDef *Frame;
    Render_TargetTypeDef Target;

    if(Draw == NULL)
    {
        return;
    }

    Frame = Display_AcquireFrame();

    if(Frame == NULL)
    {
        return;
    }

    Target.Pixels = Frame->Pixels;
    Target.Width = Frame->Width;
    Target.Height = Frame->Height;
    Target.StridePixels = Frame->StridePixels;

    Render_ResetClipRect();
    Draw(&Target);

    (void)Display_PresentFrame(Frame);
}

static bool AppManager_StartLauncher(void)
{
    AppManager_State = APP_MANAGER_STATE_LAUNCHER;

    if((AppManager_LauncherInterface.Init == NULL) || !AppManager_LauncherInterface.Init())
    {
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

bool AppManager_StartApplication(uint16_t ApplicationIndex)
{
    const AppManager_AppTypeDef *Application = AppManager_GetApplication(ApplicationIndex);

    if((Application == NULL) || (Application->Init == NULL))
    {
        return false;
    }

    /* Each application starts with silent mixer channels and its own colours. */
    Music_Stop();
    Sound_StopAll();
    (void)Mixer_StopApplicationChannels();

    {
        Display_ColourTypeDef Palette[APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT];

        AppManager_FillPalette(Application, Palette);
        (void)Display_SetPalette(APP_MANAGER_SPLASH_PALETTE_START_INDEX, Palette, APP_MANAGER_SPLASH_PALETTE_ENTRY_COUNT);
    }

    AppManager_ResetControls();

    if(!Application->Init())
    {
        return false;
    }

    if(AppManager_LauncherInterface.Pause != NULL)
    {
        AppManager_LauncherInterface.Pause();
    }

    AppManager_ActiveApplicationIndex = ApplicationIndex;
    AppManager_State = APP_MANAGER_STATE_APPLICATION;

    return true;
}

bool AppManager_Init(void)
{
    if(AppManager_Initialized)
    {
        return true;
    }

    AppManager_State = APP_MANAGER_STATE_LAUNCHER;
    AppManager_ActiveApplicationIndex = 0U;
    AppManager_Paused = false;

    if(!AppManager_StartLauncher())
    {
        return false;
    }

    AppManager_Initialized = true;

    return true;
}

void AppManager_Update(uint32_t DeltaTimeMilliseconds)
{
    const AppManager_AppTypeDef *Application;

    if(!AppManager_Initialized || AppManager_Paused)
    {
        return;
    }

    if(AppManager_State == APP_MANAGER_STATE_LAUNCHER)
    {
        if(AppManager_LauncherInterface.Update != NULL)
        {
            AppManager_LauncherInterface.Update(DeltaTimeMilliseconds);
        }

        return;
    }

    Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

    if((Application != NULL) && (Application->Update != NULL))
    {
        Application->Update(DeltaTimeMilliseconds);
    }
}

void AppManager_Render(void)
{
    const AppManager_AppTypeDef *Application;

    if(!AppManager_Initialized || AppManager_Paused)
    {
        return;
    }

    if(AppManager_State == APP_MANAGER_STATE_LAUNCHER)
    {
        AppManager_RenderFrame(AppManager_LauncherInterface.Render);
        return;
    }

    Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

    if(Application != NULL)
    {
        AppManager_RenderFrame(Application->Render);
    }
}

bool AppManager_GetAppSplashScreenPalette(uint16_t ApplicationIndex, Display_ColourTypeDef *Palette)
{
    const AppManager_AppTypeDef *Application;

    if(Palette == NULL)
    {
        return false;
    }

    Application = AppManager_GetApplication(ApplicationIndex);

    if(Application == NULL)
    {
        return false;
    }

    AppManager_FillPalette(Application, Palette);

    return true;
}

bool AppManager_DrawAppSplashScreen(uint16_t ApplicationIndex, Render_TargetTypeDef *Target)
{
    const AppManager_AppTypeDef *Application;

    if((Target == NULL) || (Target->Pixels == NULL))
    {
        return false;
    }

    Application = AppManager_GetApplication(ApplicationIndex);

    if((Application == NULL) || (Application->DrawSplashScreen == NULL))
    {
        return false;
    }

    return Application->DrawSplashScreen(Target);
}

void AppManager_Pause(void)
{
    const AppManager_AppTypeDef *Application;

    if(!AppManager_Initialized || AppManager_Paused)
    {
        return;
    }

    if(AppManager_State == APP_MANAGER_STATE_LAUNCHER)
    {
        if(AppManager_LauncherInterface.Pause != NULL)
        {
            AppManager_LauncherInterface.Pause();
        }
    }
    else
    {
        Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

        if((Application != NULL) && (Application->Pause != NULL))
        {
            Application->Pause();
        }
    }

    AppManager_Paused = true;
}

void AppManager_Resume(void)
{
    const AppManager_AppTypeDef *Application;

    if(!AppManager_Initialized || !AppManager_Paused)
    {
        return;
    }

    AppManager_ResetControls();

    if(AppManager_State == APP_MANAGER_STATE_LAUNCHER)
    {
        if(AppManager_LauncherInterface.Resume != NULL)
        {
            AppManager_LauncherInterface.Resume();
        }
    }
    else
    {
        Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

        if((Application != NULL) && (Application->Resume != NULL))
        {
            Application->Resume();
        }
    }

    AppManager_Paused = false;
}

void AppManager_OpenLauncher(void)
{
    const AppManager_AppTypeDef *Application;

    if(!AppManager_Initialized || (AppManager_State == APP_MANAGER_STATE_LAUNCHER))
    {
        return;
    }

    Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

    if((Application != NULL) && (Application->Shutdown != NULL))
    {
        Application->Shutdown();
    }

    Music_Stop();
    Sound_StopAll();
    (void)Mixer_StopApplicationChannels();

    AppManager_State = APP_MANAGER_STATE_LAUNCHER;
    AppManager_ActiveApplicationIndex = 0U;
    AppManager_ResetControls();

    if(!AppManager_Paused && (AppManager_LauncherInterface.Resume != NULL))
    {
        AppManager_LauncherInterface.Resume();
    }
}

uint16_t AppManager_GetAppCount(void)
{
    return (uint16_t)(sizeof(AppManager_Applications) / sizeof(AppManager_Applications[0]));
}

bool AppManager_IsLauncherActive(void)
{
    return AppManager_Initialized && (AppManager_State == APP_MANAGER_STATE_LAUNCHER);
}

void AppManager_Shutdown(void)
{
    const AppManager_AppTypeDef *Application;

    if(!AppManager_Initialized)
    {
        return;
    }

    if(AppManager_State == APP_MANAGER_STATE_LAUNCHER)
    {
        if(AppManager_LauncherInterface.Shutdown != NULL)
        {
            AppManager_LauncherInterface.Shutdown();
        }
    }
    else
    {
        Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

        if((Application != NULL) && (Application->Shutdown != NULL))
        {
            Application->Shutdown();
        }
    }

    Music_Stop();
    Sound_StopAll();
    (void)Mixer_StopApplicationChannels();

    AppManager_State = APP_MANAGER_STATE_LAUNCHER;
    AppManager_ActiveApplicationIndex = 0U;
    AppManager_Paused = false;
    AppManager_Initialized = false;
}
