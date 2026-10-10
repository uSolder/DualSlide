/**
 * @file tug_career.c
 * @brief Canal Tug's game flow: the job board, the shipyard, towing jobs
 *        and the money that ties them together.
 */

#include "tug_internal.h"

#include "tug_audio.h"

#include "controls.h"
#include "save.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Private configuration                                                      */
/* -------------------------------------------------------------------------- */

/* Sliders read as idle within this much of the centre (of 1000 each way). */
#define THROTTLE_DEAD_ZONE      (130)

/*
 * Cargo damage, in percent of its condition, before its fragility is
 * applied: knocks harder than CARGO_SAFE_SPEED (pixels per second) cost
 * CARGO_KNOCK_DAMAGE per pixel per second over; scraping along the bank
 * costs CARGO_SCRAPE_DAMAGE per pixel slid, and dragging it through the
 * shallows CARGO_GROUNDING_DAMAGE per pixel dragged. The tug's own knocks
 * don't hurt the cargo.
 */
#define CARGO_SAFE_SPEED        (10.0f)
#define CARGO_KNOCK_DAMAGE      (0.6f)
#define CARGO_SCRAPE_DAMAGE     (0.15f)
#define CARGO_GROUNDING_DAMAGE  (0.12f)

/*
 * Upgrades: soft fenders leave knocks this share of their damage and keel
 * guards leave scrapes and grounding this share; the turbo engine adds this
 * share of push; the trade licence adds this share to every job's pay.
 */
#define FENDER_KNOCK_SHARE      (0.6f)
#define KEEL_GUARD_WEAR_SHARE   (0.5f)
#define TURBO_EXTRA_THRUST      (0.12f)
#define LICENCE_EXTRA_PAY       (0.1f)

/* Hooking on: the tug's stern this close to the cargo's bow, not going too fast. */
#define HOOK_DISTANCE           (40.0f)
#define HOOK_SPEED              (80.0f)

/* Delivering: the cargo held this still, this close to its site's dock (more for long cargo), for this long. */
#define DELIVER_DISTANCE        (50.0f)
#define DELIVER_SPEED           (18.0f)
#define DELIVER_MILLISECONDS    (1000.0f)

/* In the marina, holding a button this long buys or leaves instead of moving along. */
#define MARINA_HOLD_MS          (700U)

/*
 * A cargo is worth its Pay times PAY_BASE_SHARE, plus that again for every
 * PAY_DISTANCE pixels of route by water between the sites. The job pays what
 * it's worth less the deposit, but never less than PAY_BASE_SHARE of the
 * difference between the two.
 */
#define PAY_BASE_SHARE          (0.6f)
#define PAY_DISTANCE            (5600.0f)

/*
 * The on-time bonus: delivered within BONUS_SPARE_SECONDS plus a second for
 * every BONUS_SPEED pixels of route, counted from hooking on, the pay goes
 * up by BONUS_SHARE. Missing it costs nothing.
 */
#define BONUS_SHARE             (0.2f)
#define BONUS_SPEED             (38.0f)
#define BONUS_SPARE_SECONDS     (30.0f)

/*
 * Reputation, kept quietly for each site: careful deliveries raise it at
 * both ends of the route, on-time ones a little more, and wrecks lower it.
 * Every REPUTATION_PER_LEVEL points the site pays REPUTATION_PAY_STEP more,
 * and sites that think well of us offer their work more often.
 */
#define REPUTATION_MAX          (50U)
#define REPUTATION_PER_LEVEL    (10U)
#define REPUTATION_PAY_STEP     (0.03f)
#define REPUTATION_WRECK_LOSS   (4)

/* The wind's strength (pixels per second per second, before gusts) on windy and stormy days. */
#define WIND_STRENGTH           (14.0f)
#define STORM_WIND_STRENGTH     (20.0f)

/* The board offers up to this many paid jobs, the rest free. */
#define PAID_OFFERS             (2U)

/* Results ignore the bumpers for a moment, so a held press doesn't skip them. */
#define RESULT_LOCKOUT_MS       (800U)

/* Hints show for this long when something new starts. */
#define HINT_MILLISECONDS       (5000U)

#define SAVE_NAME               ("TUGC")
#define REPUTATION_SAVE_NAME    ("TUGR")

/* How often each weather comes up, out of 100: clear, rain, wind, storm. */
static const uint8_t Tug_WeatherChance[TUG_WEATHER_COUNT] = { 75U, 10U, 10U, 5U };

/* -------------------------------------------------------------------------- */
/* Private data                                                               */
/* -------------------------------------------------------------------------- */

Tug_GameTypeDef Tug_Game;

static uint32_t Tug_CareerRandomState = 0x3C6EF372U;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static uint32_t Tug_CareerRandom(void)
{
    Tug_CareerRandomState = (Tug_CareerRandomState * 1664525U) + 1013904223U;
    return Tug_CareerRandomState >> 8;
}

/* A slider as a throttle: -1 full astern, 0 idle around the centre, 1 full ahead. */
static float Tug_ReadThrottle(Controls_SliderTypeDef Slider)
{
    const int32_t Raw = Controls_SliderBetween(Slider, -1000, 1000);
    if(Raw > THROTTLE_DEAD_ZONE)
    {
        return (float)(Raw - THROTTLE_DEAD_ZONE) / (float)(1000 - THROTTLE_DEAD_ZONE);
    }
    if(Raw < -THROTTLE_DEAD_ZONE)
    {
        return (float)(Raw + THROTTLE_DEAD_ZONE) / (float)(1000 - THROTTLE_DEAD_ZONE);
    }
    return 0.0f;
}

static bool Tug_AnyBumperPressed(void)
{
    return Controls_WasPressed(TUG_LEFT_BUMPER) || Controls_WasPressed(TUG_RIGHT_BUMPER);
}

static bool Tug_HasUpgrade(uint8_t Bit)
{
    return (Tug_Game.Career.Upgrades & Bit) != 0U;
}

/* How far it is by water between two sites, in pixels. */
static float Tug_RouteDistance(uint8_t First, uint8_t Second)
{
    return (float)Tug_RouteTiles[First][Second] * (float)TUG_TILE_SIZE;
}

/* Money rounded to the nearest $10. */
static uint32_t Tug_RoundTen(float Amount)
{
    return (uint32_t)(lrintf(Amount / 10.0f) * 10);
}

static float Tug_DistanceToDock(const Tug_BodyTypeDef *Body, uint8_t Dock)
{
    float DockX;
    float DockY;
    Tug_DockPosition(Dock, &DockX, &DockY);
    return sqrtf(((Body->X - DockX) * (Body->X - DockX)) + ((Body->Y - DockY) * (Body->Y - DockY)));
}

/* "$12,500" */
void Tug_FormatMoney(char *Buffer, uint32_t Size, int32_t Amount)
{
    const uint32_t Magnitude = (uint32_t)((Amount < 0) ? -Amount : Amount);
    const char *Sign = (Amount < 0) ? "-" : "";
    if(Magnitude >= 1000000U)
    {
        (void)Render_FormatText(Buffer, Size, "%s$%u,%03u,%03u", Sign, (unsigned int)(Magnitude / 1000000U), (unsigned int)((Magnitude / 1000U) % 1000U), (unsigned int)(Magnitude % 1000U));
    }
    else if(Magnitude >= 1000U)
    {
        (void)Render_FormatText(Buffer, Size, "%s$%u,%03u", Sign, (unsigned int)(Magnitude / 1000U), (unsigned int)(Magnitude % 1000U));
    }
    else
    {
        (void)Render_FormatText(Buffer, Size, "%s$%u", Sign, (unsigned int)Magnitude);
    }
}

static void Tug_SaveCareer(void)
{
    /* Test mode never saves, so the real career is untouched. */
    if(TUG_TEST_MODE != 0)
    {
        return;
    }
    (void)Save_Store(SAVE_NAME, &Tug_Game.Career, sizeof(Tug_Game.Career));
    (void)Save_Store(REPUTATION_SAVE_NAME, Tug_Game.Reputation, sizeof(Tug_Game.Reputation));
}

void Tug_LoadCareer(void)
{
    Tug_CareerTypeDef *Career = &Tug_Game.Career;
    if(!Save_Load(SAVE_NAME, Career, sizeof(*Career)) || ((Career->OwnedModels & 1U) == 0U) || (Career->OwnedModels >= (1U << TUG_MODEL_COUNT)) ||
       (Career->Model >= TUG_MODEL_COUNT) || ((Career->OwnedModels & (1U << Career->Model)) == 0U) || (Career->Money < 0))
    {
        memset(Career, 0, sizeof(*Career));
        Career->OwnedModels = 1U;
        memset(Tug_Game.Reputation, 0, sizeof(Tug_Game.Reputation));
    }
    else if(!Save_Load(REPUTATION_SAVE_NAME, Tug_Game.Reputation, sizeof(Tug_Game.Reputation)))
    {
        memset(Tug_Game.Reputation, 0, sizeof(Tug_Game.Reputation));
    }

    /* Test mode: every tug and upgrade, money to spare, and the biggest tug so every cargo is offered. */
    if(TUG_TEST_MODE != 0)
    {
        Career->OwnedModels = (uint16_t)((1U << TUG_MODEL_COUNT) - 1U);
        Career->Upgrades = 0U;
        for(uint8_t Index = 0U; Index < TUG_UPGRADE_COUNT; Index++)
        {
            Career->Upgrades = (uint8_t)(Career->Upgrades | Tug_Upgrades[Index].Bit);
        }
        Career->Money = TUG_TEST_MONEY;
        Career->Model = (uint8_t)(TUG_MODEL_COUNT - 1U);
    }
}

/* How well a site thinks of us, 0 to REPUTATION_MAX / REPUTATION_PER_LEVEL. */
static uint8_t Tug_ReputationLevel(uint8_t Site)
{
    const uint8_t Points = (Tug_Game.Reputation[Site] > REPUTATION_MAX) ? (uint8_t)REPUTATION_MAX : Tug_Game.Reputation[Site];
    return (uint8_t)(Points / REPUTATION_PER_LEVEL);
}

static void Tug_ChangeReputation(uint8_t Site, int16_t Change)
{
    int16_t Points = (int16_t)((int16_t)Tug_Game.Reputation[Site] + Change);
    Points = (Points < 0) ? 0 : ((Points > (int16_t)REPUTATION_MAX) ? (int16_t)REPUTATION_MAX : Points);
    Tug_Game.Reputation[Site] = (uint8_t)Points;
}

/* One of a cargo's two sites, more often the one that thinks better of us. */
static uint8_t Tug_PickSite(const uint8_t Sites[2])
{
    const uint8_t First = Sites[0];
    const uint8_t Second = Sites[1];
    const uint8_t Liked = (Tug_Game.Reputation[Second] > Tug_Game.Reputation[First]) ? Second : First;
    if(Tug_Game.Reputation[First] == Tug_Game.Reputation[Second])
    {
        return Sites[Tug_CareerRandom() & 1U];
    }
    return ((Tug_CareerRandom() % 3U) != 0U) ? Liked : ((Liked == First) ? Second : First);
}

static Tug_WeatherTypeDef Tug_PickWeather(void)
{
    uint16_t Total = 0U;
    uint16_t Roll;
    for(uint8_t Weather = 0U; Weather < (uint8_t)TUG_WEATHER_COUNT; Weather++)
    {
        Total = (uint16_t)(Total + Tug_WeatherChance[Weather]);
    }
    Roll = (uint16_t)(Tug_CareerRandom() % Total);
    for(uint8_t Weather = 0U; Weather < (uint8_t)TUG_WEATHER_COUNT; Weather++)
    {
        if(Roll < Tug_WeatherChance[Weather])
        {
            return (Tug_WeatherTypeDef)Weather;
        }
        Roll = (uint16_t)(Roll - Tug_WeatherChance[Weather]);
    }
    return TUG_WEATHER_CLEAR;
}

/* Set the day's weather: its colours, and on windy days a wind from a random direction. */
static void Tug_SetWeather(Tug_WeatherTypeDef Weather)
{
    Tug_WorldTypeDef *World = &Tug_Game.World;
    const float Strength = (Weather == TUG_WEATHER_STORM) ? STORM_WIND_STRENGTH : ((Weather == TUG_WEATHER_WIND) ? WIND_STRENGTH : 0.0f);
    const float Direction = (float)(Tug_CareerRandom() % 6283U) / 1000.0f;
    World->Weather = Weather;
    World->WindX = Strength * sinf(Direction);
    World->WindY = -Strength * cosf(Direction);
    Tug_SetWeatherColours(Weather);
}

/* Swap to another tug, keeping its place in the water. */
static void Tug_UseModel(uint8_t Model)
{
    Tug_TugTypeDef *Tug = &Tug_Game.World.Tug;
    Tug_Game.Career.Model = Model;
    Tug->Model = Model;
    Tug->Body.Length = Tug_Models[Model].Length;
    Tug->Body.Width = Tug_Models[Model].Width;
}

/* -------------------------------------------------------------------------- */
/* Job board                                                                  */
/* -------------------------------------------------------------------------- */

/* Shuffle a list of cargo types. */
static void Tug_Shuffle(uint8_t *Types, uint8_t Count)
{
    for(uint8_t Index = Count; Index > 1U; Index--)
    {
        const uint8_t Other = (uint8_t)(Tug_CareerRandom() % Index);
        const uint8_t Swap = Types[Index - 1U];
        Types[Index - 1U] = Types[Other];
        Types[Other] = Swap;
    }
}

/*
 * Three jobs for the board: only cargo the tug can tow and whose deposit the
 * career can afford. As soon as any such work is within reach, up to two
 * jobs with a deposit are offered alongside a free one; otherwise all three
 * are free.
 */
static void Tug_MakeOffers(void)
{
    const uint8_t Class = Tug_Models[Tug_Game.Career.Model].Class;
    uint8_t Paid[TUG_CARGO_TYPE_COUNT];
    uint8_t Free[TUG_CARGO_TYPE_COUNT];
    uint8_t PaidCount = 0U;
    uint8_t FreeCount = 0U;
    uint8_t Chosen[TUG_OFFER_COUNT];
    uint8_t Count = 0U;

    for(uint8_t Type = 0U; Type < TUG_CARGO_TYPE_COUNT; Type++)
    {
        const Tug_CargoTypeDef *Kind = &Tug_CargoTypes[Type];
        if(Kind->Class > Class)
        {
            continue;
        }
        if(Kind->Deposit == 0U)
        {
            Free[FreeCount++] = Type;
        }
        else if((int32_t)Kind->Deposit <= Tug_Game.Career.Money)
        {
            Paid[PaidCount++] = Type;
        }
    }
    Tug_Shuffle(Paid, PaidCount);
    Tug_Shuffle(Free, FreeCount);

    for(uint8_t Index = 0U; (Index < PaidCount) && (Count < PAID_OFFERS); Index++)
    {
        Chosen[Count++] = Paid[Index];
    }
    for(uint8_t Index = 0U; Count < TUG_OFFER_COUNT; Index++)
    {
        Chosen[Count++] = Free[Index % FreeCount];
    }

    Tug_Game.OfferCount = TUG_OFFER_COUNT;
    for(uint8_t Index = 0U; Index < TUG_OFFER_COUNT; Index++)
    {
        Tug_OfferTypeDef *Offer = &Tug_Game.Offers[Index];
        const Tug_CargoTypeDef *Kind = &Tug_CargoTypes[Chosen[Index]];
        float Distance;
        float Worth;
        Offer->Cargo = Chosen[Index];
        Offer->From = Tug_PickSite(Kind->From);
        Offer->To = Tug_PickSite(Kind->To);
        Distance = Tug_RouteDistance(Offer->From, Offer->To);
        Offer->Deposit = (TUG_TEST_MODE != 0) ? 0U : Kind->Deposit;
        Worth = (float)Kind->Pay * (PAY_BASE_SHARE + (Distance / PAY_DISTANCE));
        Worth = fmaxf(Worth - (float)Kind->Deposit, (float)(Kind->Pay - Kind->Deposit) * PAY_BASE_SHARE);
        Offer->Pay = Tug_RoundTen(Worth * (1.0f + (REPUTATION_PAY_STEP * (float)Tug_ReputationLevel(Offer->To)) + (Tug_HasUpgrade(TUG_UPGRADE_TRADE_LICENCE) ? LICENCE_EXTRA_PAY : 0.0f)));
        Offer->BonusSeconds = (uint16_t)(lrintf((BONUS_SPARE_SECONDS + (Distance / BONUS_SPEED)) / 5.0f) * 5);
        Offer->Weather = Tug_PickWeather();
    }

    /* Smallest deposit first. */
    for(uint8_t Pass = 0U; Pass < TUG_OFFER_COUNT; Pass++)
    {
        for(uint8_t Index = 1U; Index < TUG_OFFER_COUNT; Index++)
        {
            if(Tug_Game.Offers[Index].Deposit < Tug_Game.Offers[Index - 1U].Deposit)
            {
                const Tug_OfferTypeDef Swap = Tug_Game.Offers[Index];
                Tug_Game.Offers[Index] = Tug_Game.Offers[Index - 1U];
                Tug_Game.Offers[Index - 1U] = Swap;
            }
        }
    }
}

static void Tug_SetScreen(Tug_ScreenTypeDef Screen)
{
    Tug_Game.Screen = Screen;
    Tug_Game.ScreenMilliseconds = 0U;
    Controls_ResetSliderMoved(CONTROLS_RIGHT_SLIDER);
}

static void Tug_OpenBoard(void)
{
    TugAudio_StopEngines();
    Tug_SetWeather(TUG_WEATHER_CLEAR);
    Tug_MakeOffers();
    Tug_Game.Selection = 0U;
    Tug_SetScreen(TUG_SCREEN_BOARD);
}

/* Straight to the boatyard's marina, where every tug is moored. */
static void Tug_OpenShop(void)
{
    TugAudio_StopEngines();
    Tug_Game.World.TargetDock = -1;
    Tug_Game.World.TargetIsCargo = false;
    Tug_Game.World.Cargo.Present = false;
    Tug_Game.Selection = Tug_Game.Career.Model;

    /* A button still held from choosing the boatyard mustn't count as a press here. */
    Tug_Game.PrimaryHoldDone = Controls_IsDown(CONTROLS_PRIMARY);
    Tug_Game.SecondaryHoldDone = Controls_IsDown(CONTROLS_SECONDARY);
    Tug_SetScreen(TUG_SCREEN_SHOP);
}

/* Leave the marina in the chosen tug, at the boatyard's dock. */
static void Tug_LeaveShop(void)
{
    Tug_Game.World.Tug.Model = Tug_Game.Career.Model;
    Tug_PlaceTugAtShipyard(&Tug_Game.World);
    Tug_FollowCamera(&Tug_Game.World, 0.0f, true);
    Tug_OpenBoard();
}

static void Tug_StartDriving(Tug_PhaseTypeDef Phase)
{
    Tug_Game.Phase = Phase;
    Tug_Game.HintMilliseconds = HINT_MILLISECONDS;
    TugAudio_PlayDoubleHorn();
    TugAudio_StartEngines();
    Tug_SetScreen(TUG_SCREEN_DRIVING);
}

static void Tug_AcceptOffer(const Tug_OfferTypeDef *Offer)
{
    Tug_Game.Job = *Offer;
    Tug_Game.HasJob = true;
    Tug_Game.BonusMilliseconds = 0U;
    Tug_Game.Career.Money -= (int32_t)Offer->Deposit;
    Tug_SaveCareer();
    Tug_SetWeather(Offer->Weather);
    Tug_PlaceCargo(&Tug_Game.World, Offer->Cargo, Offer->From);
    Tug_Game.World.Cargo.Thrusters = Tug_HasUpgrade(TUG_UPGRADE_CARGO_THRUSTERS);
    Tug_Game.World.Cargo.Covered = Tug_HasUpgrade(TUG_UPGRADE_WEATHER_COVERS);
    Tug_Game.World.Tug.ExtraThrust = Tug_HasUpgrade(TUG_UPGRADE_TURBO) ? TURBO_EXTRA_THRUST : 0.0f;
    Tug_Game.World.TargetDock = -1;
    Tug_Game.World.TargetIsCargo = true;
    Tug_Game.World.DeliverProgress = 0.0f;
    Tug_StartDriving(TUG_PHASE_TO_CARGO);
}

/* -------------------------------------------------------------------------- */
/* Jobs                                                                       */
/* -------------------------------------------------------------------------- */

static void Tug_FinishJob(bool Delivered)
{
    Tug_TowTypeDef *Cargo = &Tug_Game.World.Cargo;
    Tug_CareerTypeDef *Career = &Tug_Game.Career;

    Tug_Game.Delivered = Delivered;
    Tug_Game.HasJob = false;
    Tug_Game.World.TargetDock = -1;
    Tug_Game.World.TargetIsCargo = false;
    Cargo->Hooked = false;
    Tug_Game.ResultFavour = -1;
    if(Delivered)
    {
        /* Damage comes off the deposit and the pay alike: cargo at 70% condition gets back 70% of each. */
        const float Share = fmaxf(Cargo->Condition, 0.0f) / 100.0f;
        const uint32_t Back = Tug_RoundTen((float)Tug_Game.Job.Deposit * Share);
        const uint32_t Pay = Tug_RoundTen((float)Tug_Game.Job.Pay * Share);
        const bool OnTime = Tug_Game.BonusMilliseconds > 0U;
        const uint32_t Bonus = OnTime ? Tug_RoundTen((float)Pay * BONUS_SHARE) : 0U;
        const uint8_t Before = Tug_ReputationLevel(Tug_Game.Job.To);
        const int16_t Gain = (int16_t)(((Cargo->Condition >= 90.0f) ? 2 : ((Cargo->Condition >= 70.0f) ? 1 : 0)) + (OnTime ? 1 : 0));

        Tug_Game.ResultDeposit = (int32_t)Back;
        Tug_Game.ResultPay = (int32_t)Pay;
        Tug_Game.ResultBonus = (int32_t)Bonus;
        Tug_Game.ResultAmount = (int32_t)(Back + Pay + Bonus);
        Career->Money += Tug_Game.ResultAmount;
        Career->Earned += Pay + Bonus;
        Career->Deliveries++;
        Tug_ChangeReputation(Tug_Game.Job.From, Gain);
        Tug_ChangeReputation(Tug_Game.Job.To, Gain);
        if(Tug_ReputationLevel(Tug_Game.Job.To) > Before)
        {
            Tug_Game.ResultFavour = (int8_t)Tug_Game.Job.To;
        }
        TugAudio_PlayPaid();
    }
    else
    {
        Tug_Game.ResultAmount = 0;
        Tug_Game.ResultDeposit = 0;
        Tug_Game.ResultPay = 0;
        Tug_Game.ResultBonus = 0;
        Tug_ChangeReputation(Tug_Game.Job.From, -REPUTATION_WRECK_LOSS);
        Tug_ChangeReputation(Tug_Game.Job.To, -REPUTATION_WRECK_LOSS);
        Career->Wrecks++;
        Cargo->SinkMilliseconds = 1U;
        TugAudio_PlayWrecked();
    }
    Tug_SaveCareer();
    TugAudio_StopEngines();
    Tug_SetScreen(TUG_SCREEN_RESULT);
}

static void Tug_UpdateDriving(uint32_t DeltaMilliseconds)
{
    Tug_WorldTypeDef *World = &Tug_Game.World;
    Tug_TugTypeDef *Tug = &World->Tug;
    Tug_TowTypeDef *Cargo = &World->Cargo;
    uint32_t Remaining = DeltaMilliseconds;
    float TugKnock = 0.0f;
    float CargoKnock = 0.0f;
    float Scrape = 0.0f;
    float Grounding = 0.0f;
    const float Left = Tug_Game.LeftThrottle;
    const float Right = Tug_Game.RightThrottle;

    /* The bumpers work the cargo thrusters, once they're fitted and the cargo's hooked on; otherwise they sound the horn. */
    if(Cargo->Thrusters && Cargo->Hooked)
    {
        Cargo->Thruster = (int8_t)((Controls_IsDown(TUG_RIGHT_BUMPER) ? 1 : 0) - (Controls_IsDown(TUG_LEFT_BUMPER) ? 1 : 0));
    }
    else
    {
        Cargo->Thruster = 0;
        if(Tug_AnyBumperPressed())
        {
            TugAudio_PlayHorn();
        }
    }

    /* The on-time bonus counts down from hooking on. */
    if(Tug_Game.Phase == TUG_PHASE_TOWING)
    {
        Tug_Game.BonusMilliseconds = (Tug_Game.BonusMilliseconds > DeltaMilliseconds) ? (Tug_Game.BonusMilliseconds - DeltaMilliseconds) : 0U;
    }

    while(Remaining > 0U)
    {
        const uint32_t Step = (Remaining > 10U) ? 10U : Remaining;
        Tug_StepResultTypeDef Result;
        Tug_StepBoats(World, (float)Step / 1000.0f, Left, Right, &Result);
        TugKnock = fmaxf(TugKnock, Result.TugKnock);
        CargoKnock = fmaxf(CargoKnock, Result.CargoKnock);
        Scrape += Result.CargoScrape;
        Grounding += Result.CargoGrounding;
        Remaining -= Step;
    }

    if(Cargo->Present && Tug_Game.HasJob)
    {
        float Wear = (Scrape * CARGO_SCRAPE_DAMAGE) + (Grounding * CARGO_GROUNDING_DAMAGE);
        float Knock = (CargoKnock > CARGO_SAFE_SPEED) ? ((CargoKnock - CARGO_SAFE_SPEED) * CARGO_KNOCK_DAMAGE) : 0.0f;
        float Damage;
        if(Tug_HasUpgrade(TUG_UPGRADE_SOFT_FENDERS))
        {
            Knock *= FENDER_KNOCK_SHARE;
        }
        if(Tug_HasUpgrade(TUG_UPGRADE_KEEL_GUARDS))
        {
            Wear *= KEEL_GUARD_WEAR_SHARE;
        }
        Damage = (Wear + Knock) * Tug_CargoTypes[Cargo->Type].Fragility;
        if(Damage > 0.0f)
        {
            Cargo->Condition -= Damage;
            if(Damage > 0.5f)
            {
                Cargo->FlashMilliseconds = 250U;
            }
        }
    }
    if((Tug_Game.KnockMilliseconds == 0U) && (fmaxf(TugKnock, CargoKnock) > 8.0f))
    {
        TugAudio_PlayKnock(fmaxf(TugKnock, CargoKnock) / 120.0f);
        Tug_Game.KnockMilliseconds = 250U;
    }
    TugAudio_SetEngines(fmaxf(fabsf(Tug->LeftEngine), fabsf(Tug->RightEngine)));
    Tug_FollowCamera(World, (float)DeltaMilliseconds / 1000.0f, false);

    if(Tug_Game.HasJob && (Cargo->Condition <= 0.0f))
    {
        Cargo->Condition = 0.0f;
        Tug_FinishJob(false);
        return;
    }

    switch(Tug_Game.Phase)
    {
        case TUG_PHASE_TO_CARGO:
        {
            float SternX;
            float SternY;
            float EndX;
            float EndY;
            const float End = Tug_HookEnd(World, &EndX, &EndY);
            Tug_LocalPoint(&Tug->Body, -Tug->Body.Length / 2.0f, 0.0f, &SternX, &SternY);
            if((sqrtf(((SternX - EndX) * (SternX - EndX)) + ((SternY - EndY) * (SternY - EndY))) < HOOK_DISTANCE) && (Tug_Speed(&Tug->Body) < HOOK_SPEED))
            {
                /* Hooked at the stern end, the cargo is towed that end first: it becomes the front. */
                if(End < 0.0f)
                {
                    Cargo->Body.Heading += (Cargo->Body.Heading > 0.0f) ? -3.14159265f : 3.14159265f;
                }
                Cargo->Hooked = true;
                World->TargetIsCargo = false;
                World->TargetDock = (int8_t)Tug_Game.Job.To;
                Tug_Game.Phase = TUG_PHASE_TOWING;
                Tug_Game.HintMilliseconds = HINT_MILLISECONDS;
                Tug_Game.BonusMilliseconds = (uint32_t)Tug_Game.Job.BonusSeconds * 1000U;
                TugAudio_PlayHook();
            }
            break;
        }

        case TUG_PHASE_TOWING:
            if((Tug_DistanceToDock(&Cargo->Body, Tug_Game.Job.To) < (DELIVER_DISTANCE + (Cargo->Body.Length / 4.0f))) && (Tug_Speed(&Cargo->Body) < DELIVER_SPEED))
            {
                World->DeliverProgress += (float)DeltaMilliseconds / DELIVER_MILLISECONDS;
                if(World->DeliverProgress >= 1.0f)
                {
                    World->DeliverProgress = 0.0f;
                    Tug_FinishJob(true);
                }
            }
            else
            {
                World->DeliverProgress = fmaxf(0.0f, World->DeliverProgress - ((float)DeltaMilliseconds * 2.0f / DELIVER_MILLISECONDS));
            }
            break;

        default:
            break;
    }
}

/* -------------------------------------------------------------------------- */
/* Screens                                                                    */
/* -------------------------------------------------------------------------- */

void Tug_StartTitle(void)
{
    TugAudio_StopEngines();
    Tug_PrepareWorld(&Tug_Game.World);
    Tug_Game.World.Tug.Model = Tug_Game.Career.Model;
    Tug_PlaceTugAtShipyard(&Tug_Game.World);
    Tug_FollowCamera(&Tug_Game.World, 0.0f, true);
    Tug_Game.HasJob = false;
    Tug_SetWeather(TUG_WEATHER_CLEAR);
    Tug_SetScreen(TUG_SCREEN_TITLE);
}

/* Take or buy the tug, or buy the upgrade, picked at the boatyard. */
static void Tug_ChooseModel(uint8_t Choice)
{
    if(Choice >= TUG_MODEL_COUNT)
    {
        const Tug_UpgradeTypeDef *Upgrade = &Tug_Upgrades[Choice - TUG_MODEL_COUNT];
        if(Tug_HasUpgrade(Upgrade->Bit))
        {
            TugAudio_PlaySelect();
        }
        else if((int32_t)Upgrade->Price <= Tug_Game.Career.Money)
        {
            Tug_Game.Career.Money -= (int32_t)Upgrade->Price;
            Tug_Game.Career.Upgrades = (uint8_t)(Tug_Game.Career.Upgrades | Upgrade->Bit);
            Tug_SaveCareer();
            TugAudio_PlayPurchase();
        }
        else
        {
            TugAudio_PlayKnock(0.3f);
        }
        return;
    }
    if((Tug_Game.Career.OwnedModels & (1U << Choice)) != 0U)
    {
        if(Choice != Tug_Game.Career.Model)
        {
            Tug_UseModel(Choice);
            Tug_SaveCareer();
        }
        TugAudio_PlaySelect();
    }
    else if((int32_t)Tug_Models[Choice].Price <= Tug_Game.Career.Money)
    {
        Tug_Game.Career.Money -= (int32_t)Tug_Models[Choice].Price;
        Tug_Game.Career.OwnedModels = (uint16_t)(Tug_Game.Career.OwnedModels | (1U << Choice));
        Tug_UseModel(Choice);
        Tug_SaveCareer();
        TugAudio_PlayPurchase();
    }
    else
    {
        TugAudio_PlayKnock(0.3f);
    }
}

/*
 * The marina: a tap of primary moves to the next tug and a tap of secondary
 * to the one before. Holding primary takes or buys the tug; holding
 * secondary leaves.
 */
static void Tug_UpdateMarina(void)
{
    if(Controls_IsDown(CONTROLS_PRIMARY) && !Tug_Game.PrimaryHoldDone && (Controls_HeldMilliseconds(CONTROLS_PRIMARY) >= MARINA_HOLD_MS))
    {
        Tug_Game.PrimaryHoldDone = true;
        Tug_ChooseModel(Tug_Game.Selection);
    }
    if(Controls_WasReleased(CONTROLS_PRIMARY))
    {
        if(!Tug_Game.PrimaryHoldDone)
        {
            Tug_Game.Selection = (uint8_t)((Tug_Game.Selection + 1U) % TUG_SHOP_ITEM_COUNT);
            TugAudio_PlaySelect();
        }
        Tug_Game.PrimaryHoldDone = false;
    }

    if(Controls_IsDown(CONTROLS_SECONDARY) && !Tug_Game.SecondaryHoldDone && (Controls_HeldMilliseconds(CONTROLS_SECONDARY) >= MARINA_HOLD_MS))
    {
        Tug_Game.SecondaryHoldDone = true;
        Tug_LeaveShop();
        return;
    }
    if(Controls_WasReleased(CONTROLS_SECONDARY))
    {
        if(!Tug_Game.SecondaryHoldDone)
        {
            Tug_Game.Selection = (uint8_t)((Tug_Game.Selection + TUG_SHOP_ITEM_COUNT - 1U) % TUG_SHOP_ITEM_COUNT);
            TugAudio_PlaySelect();
        }
        Tug_Game.SecondaryHoldDone = false;
    }
}

/* Pick from a list with the right slider: the top of its travel is the first entry. */
static void Tug_UpdateSelection(uint8_t Count)
{
    const uint8_t Last = (uint8_t)(Count - 1U);
    uint8_t Selection = Tug_Game.Selection;
    if(Controls_SliderMoved(CONTROLS_RIGHT_SLIDER))
    {
        Selection = (uint8_t)Controls_SliderBetween(CONTROLS_RIGHT_SLIDER, Last, 0);
    }
    if((Selection != Tug_Game.Selection) && (Selection <= Last))
    {
        Tug_Game.Selection = Selection;
        TugAudio_PlaySelect();
    }
}

void Tug_UpdateGame(uint32_t DeltaMilliseconds)
{
    Tug_WorldTypeDef *World = &Tug_Game.World;
    const uint32_t ScreenMilliseconds = Tug_Game.ScreenMilliseconds + DeltaMilliseconds;

    Tug_Game.ScreenMilliseconds = (uint16_t)((ScreenMilliseconds > 60000U) ? 60000U : ScreenMilliseconds);
    Tug_Game.KnockMilliseconds = (Tug_Game.KnockMilliseconds > DeltaMilliseconds) ? (uint16_t)(Tug_Game.KnockMilliseconds - DeltaMilliseconds) : 0U;
    Tug_Game.HintMilliseconds = (Tug_Game.HintMilliseconds > DeltaMilliseconds) ? (uint16_t)(Tug_Game.HintMilliseconds - DeltaMilliseconds) : 0U;
    World->Cargo.FlashMilliseconds = (World->Cargo.FlashMilliseconds > DeltaMilliseconds) ? (uint16_t)(World->Cargo.FlashMilliseconds - DeltaMilliseconds) : 0U;
    if((World->Cargo.SinkMilliseconds > 0U) && (World->Cargo.SinkMilliseconds < 60000U))
    {
        World->Cargo.SinkMilliseconds = (uint16_t)(World->Cargo.SinkMilliseconds + DeltaMilliseconds);
    }
    Tug_Game.LeftThrottle = Tug_ReadThrottle(CONTROLS_LEFT_SLIDER);
    Tug_Game.RightThrottle = Tug_ReadThrottle(CONTROLS_RIGHT_SLIDER);
    Tug_UpdateCity(World, DeltaMilliseconds);

    switch(Tug_Game.Screen)
    {
        case TUG_SCREEN_TITLE:
        {
            /* Drift slowly over the city behind the title. */
            const float Time = (float)World->Milliseconds * 0.00005f;
            World->CameraX = (0.5f + (0.5f * sinf(Time))) * (float)(TUG_WORLD_WIDTH - (int32_t)RENDER_WIDTH);
            World->CameraY = (0.5f + (0.5f * sinf(Time * 0.7f + 1.0f))) * (float)(TUG_WORLD_HEIGHT - (int32_t)RENDER_HEIGHT);
            if(Tug_AnyBumperPressed())
            {
                Tug_FollowCamera(World, 0.0f, true);
                Tug_OpenBoard();
            }
            break;
        }

        case TUG_SCREEN_BOARD:
            Tug_UpdateSelection((uint8_t)(Tug_Game.OfferCount + 1U));
            if(Tug_AnyBumperPressed())
            {
                if(Tug_Game.Selection < Tug_Game.OfferCount)
                {
                    Tug_AcceptOffer(&Tug_Game.Offers[Tug_Game.Selection]);
                }
                else
                {
                    Tug_OpenShop();
                }
            }
            break;

        case TUG_SCREEN_SHOP:
            Tug_UpdateMarina();
            break;

        case TUG_SCREEN_DRIVING:
            Tug_UpdateDriving(DeltaMilliseconds);
            break;

        case TUG_SCREEN_RESULT:
        {
            /* Everything drifts to a stop while the result is shown. */
            Tug_StepResultTypeDef Result;
            Tug_StepBoats(World, (float)DeltaMilliseconds / 1000.0f, 0.0f, 0.0f, &Result);
            Tug_FollowCamera(World, (float)DeltaMilliseconds / 1000.0f, false);
            if((Tug_Game.ScreenMilliseconds >= RESULT_LOCKOUT_MS) && Tug_AnyBumperPressed())
            {
                World->Cargo.Present = false;
                World->Cargo.SinkMilliseconds = 0U;
                Tug_OpenBoard();
            }
            break;
        }

        default:
            break;
    }
}
