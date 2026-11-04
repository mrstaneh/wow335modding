// mod-drowned-sun: the Sanctum of the Drowned Sun (map 801), Mods/DrownedSun/DESIGN.md. Its terrain comes from
// Tools/MpqTool/mapgen_drownedsun.cpp, its templates and spawns from Mods/sql/drownedsun.sql.
#ifndef MOD_DROWNED_SUN_H
#define MOD_DROWNED_SUN_H

#include "CreatureAIImpl.h"
#include "Position.h"

#define DrownedSunScriptName "instance_drowned_sun"
#define DataHeader "DS"

uint32 const MAP_DROWNED_SUN = 801;

enum DrownedSunData
{
    // Encounters, kept by the instance's boss states
    DATA_SLUICES = 0, // the Flooded Nave's trial
    DATA_SEBEKHTI = 1,
    MAX_ENCOUNTERS = 2,

    DATA_WHEEL_TURNED = 10, // SetData: a sluice wheel has been turned
    DATA_TURNER = 11,       // SetGuidData: the player who last took hold of a wheel
};

enum DrownedSunCreatures
{
    NPC_SEBEKHTI = 500100,
    NPC_NAVE_SNAPJAW = 500101,
};

enum DrownedSunGameObjects
{
    GO_SLUICE_WHEEL = 500002,
    GO_SEBEKHTI_HOARD = 500010,
};

enum DrownedSunActions
{
    ACTION_EMERGE = 1,
};

// The Flooded Nave, from mapgen_drownedsun.cpp: the Sluices and Sebekhti's basin, their floors at 5 under water at 6.5.
Position const SLUICES_CENTER = { 0.0f, -115.0f, 5.0f, 0.0f };
float const SLUICES_RADIUS = 34.0f;
Position const BASIN_CENTER = { 0.0f, -205.0f, 5.0f, 0.0f };
float const BASIN_RADIUS = 50.0f;
Position const HOARD_ISLAND = { 14.0f, -187.0f, 8.0f, 0.0f }; // on the first island, beside its palm

template <class AI, class T>
inline AI* GetDrownedSunAI(T* obj)
{
    return GetInstanceAI<AI>(obj, DrownedSunScriptName);
}

#define RegisterDrownedSunCreatureAI(ai_name) RegisterCreatureAIWithFactory(ai_name, GetDrownedSunAI)
#define RegisterDrownedSunGameObjectAI(ai_name) RegisterGameObjectAIWithFactory(ai_name, GetDrownedSunAI)

#endif
