// mod-drowned-sun: the Sanctum of the Drowned Sun (map 801), Mods/DrownedSun/DESIGN.md. Its terrain comes from
// Tools/MpqTool/mapgen_drownedsun.cpp, its templates and spawns from Mods/sql/drownedsun.sql.
#ifndef MOD_DROWNED_SUN_H
#define MOD_DROWNED_SUN_H

#include <vector>
#include "CreatureAIImpl.h"
#include "Map.h"
#include "Player.h"
#include "Position.h"

#define DrownedSunScriptName "instance_drowned_sun"
#define DataHeader "DS"

uint32 const MAP_DROWNED_SUN = 801;

enum DrownedSunData
{
    // Encounters, kept by the instance's boss states: each wing's trial, then its boss
    DATA_SLUICES = 0, // the Flooded Nave
    DATA_SEBEKHTI = 1,
    DATA_LONG_RAMP = 2, // the Dawn Gallery
    DATA_KHEPRET = 3,
    MAX_ENCOUNTERS = 4,

    DATA_WHEEL_TURNED = 10, // SetData: a sluice wheel has been turned
    DATA_TURNER = 11,       // SetGuidData: the player who last took hold of a wheel
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
};

enum DrownedSunGameObjects
{
    GO_SLUICE_WHEEL = 500002,
    GO_SEBEKHTI_HOARD = 500010,
    GO_KHEPRET_HOARD = 500011,
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

template <class AI, class T>
inline AI* GetDrownedSunAI(T* obj)
{
    return GetInstanceAI<AI>(obj, DrownedSunScriptName);
}

#define RegisterDrownedSunCreatureAI(ai_name) RegisterCreatureAIWithFactory(ai_name, GetDrownedSunAI)
#define RegisterDrownedSunGameObjectAI(ai_name) RegisterGameObjectAIWithFactory(ai_name, GetDrownedSunAI)

#endif
