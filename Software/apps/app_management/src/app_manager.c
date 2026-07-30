/**
 * @file app_manager.c
 * @brief Controls the DualSlide launcher and active application lifecycle.
 */

#include "app_manager.h"

#include "launcher.h"
#include "template_game.h"
#include "window_washer.h"

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    APP_MANAGER_STATE_LAUNCHER = 0,
    APP_MANAGER_STATE_APPLICATION
} AppManager_StateTypeDef;

typedef struct
{
    bool (*Init)(void);
    void (*Update)(uint32_t DeltaTimeMilliseconds);
    void (*Render)(void);
    void (*Pause)(void);
    void (*Resume)(void);
    void (*Shutdown)(void);
} AppManager_RuntimeInterfaceTypeDef;

typedef struct
{
    bool (*Init)(void);
    void (*Update)(uint32_t DeltaTimeMilliseconds);
    void (*Render)(void);
    bool (*GetSplashScreenPalette)(Display_ColourTypeDef *Palette);
    bool (*DrawSplashScreen)(Render_TargetTypeDef *Target);
    void (*Pause)(void);
    void (*Resume)(void);
    void (*Shutdown)(void);
} AppManager_ApplicationInterfaceTypeDef;

static const AppManager_RuntimeInterfaceTypeDef AppManager_LauncherInterface =
{
    .Init = Launcher_Init,
    .Update = Launcher_Update,
    .Render = Launcher_Render,
    .Pause = Launcher_Pause,
    .Resume = Launcher_Resume,
    .Shutdown = Launcher_Shutdown
};

static const AppManager_ApplicationInterfaceTypeDef AppManager_Applications[NUM_APPS] =
{
    {
        .Init = WindowWasher_Init,
        .Update = WindowWasher_Update,
        .Render = WindowWasher_Render,
        .GetSplashScreenPalette = WindowWasher_GetSplashScreenPalette,
        .DrawSplashScreen = WindowWasher_DrawSplashScreen,
        .Pause = WindowWasher_Pause,
        .Resume = WindowWasher_Resume,
        .Shutdown = WindowWasher_Shutdown
    },
    {
        .Init = TemplateGame_Init,
        .Update = TemplateGame_Update,
        .Render = TemplateGame_Render,
        .GetSplashScreenPalette = TemplateGame_GetSplashScreenPalette,
        .DrawSplashScreen = TemplateGame_DrawSplashScreen,
        .Pause = TemplateGame_Pause,
        .Resume = TemplateGame_Resume,
        .Shutdown = TemplateGame_Shutdown
    }
};

static AppManager_StateTypeDef AppManager_State;
static uint16_t AppManager_ActiveApplicationIndex;
static bool AppManager_Initialized;
static bool AppManager_Paused;

static bool AppManager_IsApplicationIndexValid(uint16_t ApplicationIndex)
{
    return ApplicationIndex < NUM_APPS;
}

static const AppManager_ApplicationInterfaceTypeDef *AppManager_GetApplication(uint16_t ApplicationIndex)
{
    if(!AppManager_IsApplicationIndexValid(ApplicationIndex))
    {
        return NULL;
    }

    return &AppManager_Applications[ApplicationIndex];
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

bool AppManager_StartApplication(uint16_t ApplicationIndex)
{
    const AppManager_ApplicationInterfaceTypeDef *Application = AppManager_GetApplication(ApplicationIndex);

    if((Application == NULL) || (Application->Init == NULL))
    {
        return false;
    }

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
    const AppManager_ApplicationInterfaceTypeDef *Application;

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
    const AppManager_ApplicationInterfaceTypeDef *Application;

    if(!AppManager_Initialized || AppManager_Paused)
    {
        return;
    }

    if(AppManager_State == APP_MANAGER_STATE_LAUNCHER)
    {
        if(AppManager_LauncherInterface.Render != NULL)
        {
            AppManager_LauncherInterface.Render();
        }

        return;
    }

    Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

    if((Application != NULL) && (Application->Render != NULL))
    {
        Application->Render();
    }
}

void AppManager_FillAudioBuffer(Audio_SampleTypeDef *Samples, uint32_t FrameCount, void *Context)
{
    (void)Context;

    if(Samples == NULL)
    {
        return;
    }

    for(uint32_t FrameIndex = 0U; FrameIndex < FrameCount; FrameIndex++)
    {
        Samples[FrameIndex] = 0;
    }
}

bool AppManager_GetAppSplashScreenPalette(uint16_t ApplicationIndex, Display_ColourTypeDef *Palette)
{
    const AppManager_ApplicationInterfaceTypeDef *Application;

    if(Palette == NULL)
    {
        return false;
    }

    Application = AppManager_GetApplication(ApplicationIndex);

    if((Application == NULL) || (Application->GetSplashScreenPalette == NULL))
    {
        return false;
    }

    return Application->GetSplashScreenPalette(Palette);
}

bool AppManager_DrawAppSplashScreen(uint16_t ApplicationIndex, Render_TargetTypeDef *Target)
{
    const AppManager_ApplicationInterfaceTypeDef *Application;

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
    const AppManager_ApplicationInterfaceTypeDef *Application;

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
    const AppManager_ApplicationInterfaceTypeDef *Application;

    if(!AppManager_Initialized || !AppManager_Paused)
    {
        return;
    }

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
    const AppManager_ApplicationInterfaceTypeDef *Application;

    if(!AppManager_Initialized || (AppManager_State == APP_MANAGER_STATE_LAUNCHER))
    {
        return;
    }

    Application = AppManager_GetApplication(AppManager_ActiveApplicationIndex);

    if((Application != NULL) && (Application->Shutdown != NULL))
    {
        Application->Shutdown();
    }

    AppManager_State = APP_MANAGER_STATE_LAUNCHER;
    AppManager_ActiveApplicationIndex = 0U;

    if(!AppManager_Paused && (AppManager_LauncherInterface.Resume != NULL))
    {
        AppManager_LauncherInterface.Resume();
    }
}

void AppManager_Shutdown(void)
{
    const AppManager_ApplicationInterfaceTypeDef *Application;

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

    AppManager_State = APP_MANAGER_STATE_LAUNCHER;
    AppManager_ActiveApplicationIndex = 0U;
    AppManager_Paused = false;
    AppManager_Initialized = false;
}