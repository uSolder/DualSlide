/**
 * @file tanks_internal.h
 * @brief Shared private types and contracts for TANKS.
 */

#ifndef TANKS_INTERNAL_H
#define TANKS_INTERNAL_H

#include "tanks.h"
#include "input.h"

#include <stdbool.h>
#include <stdint.h>

#define TANKS_INPUT_LEFT_TRACK       ((Input_NumberTypeDef)1U)
#define TANKS_INPUT_RIGHT_TRACK      ((Input_NumberTypeDef)2U)
#define TANKS_INPUT_PRIMARY          ((Input_NumberTypeDef)3U)
#define TANKS_INPUT_SECONDARY        ((Input_NumberTypeDef)4U)
#define TANKS_INPUT_FIRE             TANKS_INPUT_PRIMARY
#define TANKS_INPUT_SPECIAL          TANKS_INPUT_SECONDARY

#define TANKS_FP_SHIFT               (8)
#define TANKS_FP_ONE                 (1 << TANKS_FP_SHIFT)
#define TANKS_FP(Value)              ((int32_t)(Value) << TANKS_FP_SHIFT)
#define TANKS_TO_INT(Value)          ((int16_t)((Value) >> TANKS_FP_SHIFT))
#define TANKS_ANGLE_FULL             (3600)
#define TANKS_ANGLE_HALF             (1800)
#define TANKS_ANGLE_QUARTER          (900)
#define TANKS_TRIG_ONE               (1024)

#define TANKS_TILE_SIZE              (25)
#define TANKS_MAP_WIDTH              (32)
#define TANKS_MAP_HEIGHT             (19)
#define TANKS_WORLD_WIDTH            (TANKS_MAP_WIDTH * TANKS_TILE_SIZE)
#define TANKS_WORLD_HEIGHT           (TANKS_MAP_HEIGHT * TANKS_TILE_SIZE)
#define TANKS_ARENA_SCREEN_X         (0)
#define TANKS_ARENA_SCREEN_Y         (0)
#define TANKS_PLAYER_SCREEN_X        ((int16_t)(TANKS_ARENA_SCREEN_X + (16 * TANKS_TILE_SIZE) + (TANKS_TILE_SIZE / 2)))
#define TANKS_PLAYER_SCREEN_Y        ((int16_t)(TANKS_ARENA_SCREEN_Y + (10 * TANKS_TILE_SIZE) + (TANKS_TILE_SIZE / 2)))
#define TANKS_MAX_ENEMY_GROUPS       (4U)

#define TANKS_MAX_ENEMIES            (32U)
#define TANKS_MAX_BULLETS            (96U)
#define TANKS_MAX_PARTICLES          (144U)
#define TANKS_MAX_MINES              (24U)
#define TANKS_MAX_TRACK_MARKS        (120U)
#define TANKS_MAX_WRECKS             (48U)
#define TANKS_MAX_FLOATING_TEXT      (16U)
#define TANKS_MAXIMUM_DELTA_MS       (50U)
#define TANKS_SPLASH_X               (60)
#define TANKS_SPLASH_Y               (60)
#define TANKS_SPLASH_WIDTH           (680U)
#define TANKS_SPLASH_HEIGHT          (360U)

#define TANKS_HQ_TILE_X              (15U)
#define TANKS_HQ_TILE_Y              (9U)
#define TANKS_HQ_CENTRE_X            ((TANKS_HQ_TILE_X + 1U) * TANKS_TILE_SIZE)
#define TANKS_HQ_CENTRE_Y            ((TANKS_HQ_TILE_Y + 1U) * TANKS_TILE_SIZE)


typedef enum
{
    TANKS_COLOUR_OUTSIDE = 0U,
    TANKS_COLOUR_OUTSIDE_LIGHT,
    TANKS_COLOUR_OUTSIDE_DARK,
    TANKS_COLOUR_FLOOR,
    TANKS_COLOUR_FLOOR_LIGHT,
    TANKS_COLOUR_FLOOR_DARK,
    TANKS_COLOUR_GROUT,
    TANKS_COLOUR_WALL,
    TANKS_COLOUR_WALL_LIGHT,
    TANKS_COLOUR_WALL_SHADOW,
    TANKS_COLOUR_PIT,
    TANKS_COLOUR_PIT_EDGE,
    TANKS_COLOUR_PLAYER,
    TANKS_COLOUR_PLAYER_LIGHT,
    TANKS_COLOUR_PLAYER_DARK,
    TANKS_COLOUR_ENEMY,
    TANKS_COLOUR_ENEMY_LIGHT,
    TANKS_COLOUR_ENEMY_DARK,
    TANKS_COLOUR_TRACK,
    TANKS_COLOUR_TRACK_LIGHT,
    TANKS_COLOUR_HQ,
    TANKS_COLOUR_HQ_LIGHT,
    TANKS_COLOUR_HQ_DARK,
    TANKS_COLOUR_BULLET,
    TANKS_COLOUR_PLAYER_LASER,
    TANKS_COLOUR_ENEMY_LASER,
    TANKS_COLOUR_FIRE,
    TANKS_COLOUR_FIRE_LIGHT,
    TANKS_COLOUR_SMOKE,
    TANKS_COLOUR_SMOKE_DARK,
    TANKS_COLOUR_MINE,
    TANKS_COLOUR_TEXT,
    TANKS_COLOUR_MUTED,
    TANKS_COLOUR_PANEL,
    TANKS_COLOUR_PANEL_EDGE,
    TANKS_COLOUR_DANGER,
    TANKS_COLOUR_WARNING,
    TANKS_COLOUR_SUCCESS,
    TANKS_COLOUR_SHADOW,
    TANKS_COLOUR_WHITE,
    TANKS_COLOUR_BLACK,
    TANKS_COLOUR_WRECK,
    TANKS_COLOUR_SCORCH,
    TANKS_COLOUR_COUNT
} Tanks_ColourTypeDef;

typedef enum
{
    TANKS_SCREEN_TITLE,
    TANKS_SCREEN_WAVE_INTRO,
    TANKS_SCREEN_PLAYING,
    TANKS_SCREEN_GAME_OVER,
    TANKS_SCREEN_VICTORY
} Tanks_ScreenTypeDef;

typedef enum
{
    TANKS_TILE_FLOOR,
    TANKS_TILE_WALL,
    TANKS_TILE_PIT,
    TANKS_TILE_HQ
} Tanks_TileTypeDef;

typedef enum
{
    TANKS_ENEMY_DUMB,
    TANKS_ENEMY_RICOCHET,
    TANKS_ENEMY_MINELAYER,
    TANKS_ENEMY_ROCKET,
    TANKS_ENEMY_HUNTER_DIRECT,
    TANKS_ENEMY_HUNTER_RICOCHET,
    TANKS_ENEMY_HUNTER_ROCKET
} Tanks_EnemyTypeDef;

typedef enum
{
    TANKS_PROJECTILE_RICOCHET,
    TANKS_PROJECTILE_ROCKET
} Tanks_ProjectileTypeDef;

typedef enum
{
    TANKS_AI_PATROL,
    TANKS_AI_GUARD,
    TANKS_AI_CHASE,
    TANKS_AI_FLANK,
    TANKS_AI_FLEE
} Tanks_EnemyAiModeTypeDef;

typedef struct { int32_t X; int32_t Y; } Tanks_VectorTypeDef;

typedef struct
{
    bool Down;
    bool PreviousDown;
    bool Pressed;
    bool Released;
} Tanks_ButtonTypeDef;

typedef struct
{
    int16_t LeftTarget;
    int16_t RightTarget;
    int16_t LeftTrack;
    int16_t RightTrack;
    Tanks_ButtonTypeDef Primary;
    Tanks_ButtonTypeDef Secondary;
} Tanks_InputStateTypeDef;

typedef struct
{
    Tanks_VectorTypeDef Position;
    Tanks_VectorTypeDef HomePosition;
    Tanks_VectorTypeDef MoveTarget;
    int16_t Heading;
    int16_t TurretHeading;
    int16_t LinearVelocity;
    int16_t AngularVelocity;
    int16_t LeftTrack;
    int16_t RightTrack;
    int16_t Hull;
    int16_t MaximumHull;
    uint16_t ReloadMilliseconds;
    uint16_t AimRefreshMilliseconds;
    uint16_t InvulnerableMilliseconds;
    uint16_t AiThinkMilliseconds;
    uint16_t StuckMilliseconds;
    int16_t AiTurnBias;
    uint8_t Mines;
    uint8_t Type;
    uint8_t Group;
    uint8_t AiMode;
    uint8_t FlashMilliseconds;
    bool AimValid;
    bool Awake;
    bool Active;
} Tanks_TankTypeDef;

typedef struct
{
    Tanks_VectorTypeDef Position;
    Tanks_VectorTypeDef Velocity;
    int16_t Heading;
    uint16_t LifeMilliseconds;
    uint8_t Damage;
    uint8_t Owner;
    uint8_t Bounces;
    uint8_t Type;
    bool Active;
} Tanks_BulletTypeDef;

typedef struct
{
    Tanks_VectorTypeDef Position;
    Tanks_VectorTypeDef Velocity;
    uint16_t LifeMilliseconds;
    uint16_t MaximumLifeMilliseconds;
    uint8_t Size;
    uint8_t Colour;
    bool Active;
} Tanks_ParticleTypeDef;

typedef struct
{
    Tanks_VectorTypeDef Position;
    uint16_t ArmMilliseconds;
    uint16_t LifeMilliseconds;
    uint8_t Owner;
    bool Active;
} Tanks_MineTypeDef;

typedef struct
{
    Tanks_VectorTypeDef Position;
    int16_t Heading;
    uint16_t LifeMilliseconds;
    bool Active;
} Tanks_TrackMarkTypeDef;

typedef struct
{
    Tanks_VectorTypeDef Position;
    int16_t Heading;
    int16_t TurretHeading;
    uint8_t Type;
    bool Active;
} Tanks_WreckTypeDef;

typedef struct
{
    Tanks_VectorTypeDef Position;
    uint16_t LifeMilliseconds;
    uint16_t Value;
    uint8_t Colour;
    bool Active;
} Tanks_FloatingTextTypeDef;

typedef struct
{
    Tanks_ScreenTypeDef Screen;
    Tanks_InputStateTypeDef Input;
    Tanks_TankTypeDef Player;
    Tanks_TankTypeDef Enemies[TANKS_MAX_ENEMIES];
    Tanks_BulletTypeDef Bullets[TANKS_MAX_BULLETS];
    Tanks_ParticleTypeDef Particles[TANKS_MAX_PARTICLES];
    Tanks_MineTypeDef Mines[TANKS_MAX_MINES];
    Tanks_TrackMarkTypeDef TrackMarks[TANKS_MAX_TRACK_MARKS];
    Tanks_WreckTypeDef Wrecks[TANKS_MAX_WRECKS];
    Tanks_FloatingTextTypeDef FloatingText[TANKS_MAX_FLOATING_TEXT];
    uint8_t Tiles[TANKS_MAP_HEIGHT][TANKS_MAP_WIDTH];
    uint8_t TileDamage[TANKS_MAP_HEIGHT][TANKS_MAP_WIDTH];
    uint32_t RandomState;
    uint32_t PendingDeltaMilliseconds;
    uint32_t RunMilliseconds;
    uint32_t ScreenMilliseconds;
    uint32_t Score;
    uint32_t HighScore;
    uint16_t Wave;
    uint16_t WaveSpawnRemaining;
    uint16_t SpawnTimerMilliseconds;
    uint16_t HqHull;
    uint16_t HqMaximumHull;
    uint16_t CameraKickMilliseconds;
    uint16_t MessageMilliseconds;
    uint16_t TrackMarkTimerMilliseconds;
    uint16_t RoundClearMilliseconds;
    uint16_t GroupTransitionMilliseconds;
    uint8_t Lives;
    uint8_t NextSpawnPoint;
    uint8_t GroupCount;
    uint8_t CurrentGroup;
    uint8_t GateX[TANKS_MAX_ENEMY_GROUPS];
    uint8_t GateY[TANKS_MAX_ENEMY_GROUPS];
    char Message[40];
    bool Initialized;
    bool Paused;
    bool RoundComplete;
} Tanks_GameTypeDef;

extern Tanks_GameTypeDef Tanks_Game;
extern const Display_ColourTypeDef Tanks_Palette[TANKS_COLOUR_COUNT];

int32_t Tanks_Clamp32(int32_t Value, int32_t Minimum, int32_t Maximum);
int16_t Tanks_NormalizeAngle(int32_t Angle);
int16_t Tanks_Sine(int16_t Angle);
int16_t Tanks_Cosine(int16_t Angle);
uint32_t Tanks_IntegerSquareRoot(uint64_t Value);
uint32_t Tanks_DistancePixels(Tanks_VectorTypeDef A, Tanks_VectorTypeDef B);
int16_t Tanks_AngleTo(Tanks_VectorTypeDef From, Tanks_VectorTypeDef To);
int16_t Tanks_TurnToward(int16_t Current, int16_t Target, int16_t MaximumStep);
uint32_t Tanks_Random(void);
void Tanks_FormatUnsigned(char *Buffer, uint8_t Size, uint32_t Value);
void Tanks_CopyText(char *Destination, uint8_t Capacity, const char *Source);

void Tanks_ResetBattlefield(void);
void Tanks_StartNewGame(void);
void Tanks_StartWave(uint16_t Wave);
void Tanks_Simulate(uint32_t DeltaMilliseconds);
void Tanks_ShowMessage(const char *Text, uint16_t Milliseconds);
void Tanks_SpawnExplosion(Tanks_VectorTypeDef Position, uint8_t Strength);
void Tanks_AwardScore(Tanks_VectorTypeDef Position, uint16_t BaseScore);
void Tanks_DamagePlayer(uint8_t Damage);
void Tanks_DamageHq(uint8_t Damage);
uint8_t Tanks_CountActiveEnemies(void);
uint8_t Tanks_CountAwakeEnemies(void);

void Tanks_DrawGame(Render_TargetTypeDef *Target);
void Tanks_DrawTitle(Render_TargetTypeDef *Target);
void Tanks_DrawEndScreen(Render_TargetTypeDef *Target, bool Victory);
void Tanks_DrawSplashArtwork(Render_TargetTypeDef *Target, uint32_t ElapsedMilliseconds);
void Tanks_WorldToScreen(Tanks_VectorTypeDef World, int16_t *ScreenX, int16_t *ScreenY);

#endif /* TANKS_INTERNAL_H */
