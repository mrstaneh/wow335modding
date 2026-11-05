// mod-drowned-sun: the Sanctum of the Drowned Sun (map 801), Mods/DrownedSun/DESIGN.md. Its terrain comes from
// Tools/MpqTool/mapgen_drownedsun.cpp, its templates and spawns from Mods/sql/drownedsun.sql.
#ifndef MOD_DROWNED_SUN_H
#define MOD_DROWNED_SUN_H

#include <vector>
#include "CreatureAIImpl.h"
#include "Map.h"
#include "Player.h"
#include "Position.h"
#include "SpellAuras.h"

#define DrownedSunScriptName "instance_drowned_sun"
#define DataHeader "DS"

uint32 const MAP_DROWNED_SUN = 801;
uint32 const AREA_SUN_COURT = 5001; // AreaTable.dbc, from MapGen

enum DrownedSunData
{
    // Encounters, kept by the instance's boss states: each wing's trial, then its boss
    DATA_SLUICES = 0, // the Flooded Nave
    DATA_SEBEKHTI = 1,
    DATA_LONG_RAMP = 2, // the Dawn Gallery
    DATA_KHEPRET = 3,
    DATA_SCALES = 4, // the Hall of Weighing
    DATA_ANPUET = 5,
    DATA_LIGHTLESS_WAY = 6, // the Eclipse Pit
    DATA_APEPHETH = 7,
    MAX_ENCOUNTERS = 8,

    DATA_WHEEL_TURNED = 10, // SetData: a sluice wheel has been turned
    DATA_TURNER = 11,       // SetGuidData: the player who last took hold of a wheel
    DATA_PICK_STONE = 12,   // SetGuidData: a player used the Scales' stone weights, feathers, left pan, right pan
    DATA_PICK_FEATHER = 13,
    DATA_PLACE_LEFT = 14,
    DATA_PLACE_RIGHT = 15,
    DATA_BRAZIER_USER = 16, // SetGuidData then SetData: a player used the Lightless Way's brazier of this index
    DATA_BRAZIER_USED = 17,
    DATA_TORCH_BEARER = 18, // GetGuidData: who last lit a brazier
};

enum DrownedSunCreatures
{
    NPC_SEBEKHTI = 500100,
    NPC_NAVE_SNAPJAW = 500101,
    NPC_KHEPRET = 500102,
    NPC_KHEPRET_SUN = 500103,  // the sun-orb Khepret pushes
    NPC_ROLLING_SUN = 500104,  // the Long Ramp's orbs
    NPC_SUN_TRAIL = 500105,    // embers on the Sun's Path
    NPC_DUSK_SCARAB = 500106,
    NPC_ANPUET = 500107,
    NPC_LINEN_WRAP = 500108,
    NPC_NATRON = 500109,
    NPC_CANOPIC_JAR = 500110,  // the spirit in a jar; the jar itself is a game object
    NPC_HALL_MUMMY = 500111,
    NPC_APEPHETH = 500112,
    NPC_SUNBEAM = 500113,
    NPC_COIL_MARK = 500114,    // where a coil is about to burst up
    NPC_BRAZIER_FLAME = 500115,
    NPC_COIL_HEART = 500116,   // inside Apepheth, with whoever he swallowed
};

enum DrownedSunGameObjects
{
    GO_SLUICE_WHEEL = 500002,
    GO_STONE_WEIGHTS = 500003,
    GO_FEATHERS = 500004,
    GO_LEFT_SCALE = 500005,
    GO_RIGHT_SCALE = 500006,
    GO_LIGHTLESS_BRAZIER = 500008,
    GO_SEBEKHTI_HOARD = 500010,
    GO_KHEPRET_HOARD = 500011,
    GO_ANPUET_HOARD = 500012,
    GO_APEPHETH_HOARD = 500013,
};

enum DrownedSunActions
{
    ACTION_EMERGE = 1, // a boss's trial is done: it wakes
};

// The Flooded Nave, from mapgen_drownedsun.cpp: the Sluices and Sebekhti's basin, their floors at 5 under water at 6.5.
Position const SLUICES_CENTER = { 0.0f, -115.0f, 5.0f, 0.0f };
float const SLUICES_RADIUS = 34.0f;
Position const BASIN_CENTER = { 0.0f, -205.0f, 5.0f, 0.0f };
float const BASIN_RADIUS = 50.0f;
Position const HOARD_ISLAND = { 14.0f, -187.0f, 8.0f, 0.0f }; // on the first island, beside its palm

// The Dawn Gallery: the Long Ramp climbs south (towards -x) from the Sun Court at 20 to Khepret's arena at 52.
float const RAMP_BOTTOM_X = -50.0f;
float const RAMP_TOP_X = -168.0f;
float const RAMP_HALF_WIDTH = 9.0f;
float const RAMP_BOTTOM_Z = 20.0f;
float const RAMP_TOP_Z = 52.0f;
Position const ARENA_CENTER = { -215.0f, 0.0f, 52.0f, 0.0f };
float const ARENA_RADIUS = 40.0f;

// The Hall of Weighing: the Scales, then Anpuet's hall further west, both with floors at 20.
Position const SCALES_CENTER = { 0.0f, 112.0f, 20.0f, 0.0f };
float const SCALES_RADIUS = 27.0f;
Position const HALL_CENTER = { 0.0f, 200.0f, 20.0f, 0.0f };
float const HALL_RADIUS = 36.0f;

// The Eclipse Pit: the Lightless Way winds north from the Sun Court down to Apepheth's pit, its floor at 0. Its braziers
// (MapGen prints them): the torch's at the court end, one at each bend, the last at the pit.
Position const LIGHTLESS_WAY[] = {
    { 46.0f, 0.0f, 20.0f, 0.0f }, { 80.0f, 25.0f, 15.0f, 0.0f }, { 115.0f, -15.0f, 10.0f, 0.0f },
    { 150.0f, 15.0f, 5.0f, 0.0f }, { 190.0f, 0.0f, 0.0f, 0.0f },
};
Position const LIGHTLESS_BRAZIERS[] = {
    { 52.0f, -4.0f, 19.74f, 0.0f }, { 80.0f, 21.0f, 14.97f, 0.0f }, { 115.0f, -19.0f, 10.12f, 0.0f },
    { 150.0f, 11.0f, 5.09f, 0.0f }, { 190.0f, -4.0f, 0.0f, 0.0f },
};
Position const PIT_CENTER = { 235.0f, 0.0f, 0.0f, 0.0f };
float const PIT_RADIUS = 43.0f;
Position const BELLY = { -470.0f, -470.0f, 94.33f, 0.0f }; // where swallowed players go: a corner of the plateau

// Living players, not GMs, within radius of a point (on the ground).
inline std::vector<Player*> PlayersNear(Map* map, Position const& at, float radius)
{
    std::vector<Player*> players;
    map->DoForAllPlayers([&](Player* player) {
        if (player->IsAlive() && !player->IsGameMaster() && player->GetExactDist2d(&at) < radius)
        {
            players.push_back(player);
        }
    });
    return players;
}

// Unlit, in the Eclipse Pit's dark: every second out of the light adds a stack (Dark Touched, which also spoils healing a
// little) and hurts by 1% of health per stack; back in the light they go.
uint32 const SPELL_UNLIT = 45347;

inline void Darken(Player* player)
{
    player->AddAura(SPELL_UNLIT, player);
    Aura* unlit = player->GetAura(SPELL_UNLIT);
    uint32 stacks = unlit ? unlit->GetStackAmount() : 1;
    player->EnvironmentalDamage(DAMAGE_EXHAUSTED, player->CountPctFromMaxHealth(int32(stacks)));
}

inline void Lighten(Player* player)
{
    player->RemoveAurasDueToSpell(SPELL_UNLIT);
}

template <class AI, class T>
inline AI* GetDrownedSunAI(T* obj)
{
    return GetInstanceAI<AI>(obj, DrownedSunScriptName);
}

#define RegisterDrownedSunCreatureAI(ai_name) RegisterCreatureAIWithFactory(ai_name, GetDrownedSunAI)
#define RegisterDrownedSunGameObjectAI(ai_name) RegisterGameObjectAIWithFactory(ai_name, GetDrownedSunAI)

#endif
