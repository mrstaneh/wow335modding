// The instance: encounter states and the wings' trials.
//
// The Sluices (Flooded Nave): the water rises every 30 seconds (as a count: the map's water itself can't move); once it
// is over the ledge, everyone standing in it drowns a little more each second. Three wheels on the ledge drain it, each
// turned by a player standing at it for a while (go_sluice_wheel in flooded_nave.cpp) while snapjaws climb out of the
// flood after them. All three turned: Sebekhti wakes in the basin.
//
// The Long Ramp (Dawn Gallery): while anyone is on the ramp, burning suns appear in one of three lanes at the top,
// flare for a moment as a warning and roll down it (npc_rolling_sun in dawn_gallery.cpp), throwing whoever they hit
// back down. The first player to reach the top wakes Khepret.
//
// A trial resets when everyone in it has left or died.
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceMapScript.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "drowned_sun.h"

namespace
{
ObjectData const creatureData[] = {
    { NPC_SEBEKHTI, DATA_SEBEKHTI },
    { NPC_KHEPRET, DATA_KHEPRET },
    { 0, 0 },
};

// The Sluices
uint32 const FLOOD_RISE_MS = 30000;
uint8 const FLOOD_AT_LEDGE = 3; // the level at which the water is over the ledge
uint8 const FLOOD_MAX = 6;
uint32 const DROWNING_PER_LEVEL = 500; // every second, per level from the ledge up
uint32 const SNAPJAW_MS = 12000;
uint8 const WHEELS = 3;
// Where snapjaws climb out of the flood.
Position const SNAPJAW_SPAWNS[] = {
    { 8.0f, -104.0f, 5.0f, 0.0f },
    { -8.0f, -124.0f, 5.0f, 0.0f },
    { 10.0f, -128.0f, 5.0f, 0.0f },
    { -10.0f, -106.0f, 5.0f, 0.0f },
};

// The Long Ramp
uint32 const SUN_MS = 2000; // between suns
float const LANES[] = { -5.5f, 0.0f, 5.5f };
} // namespace

class instance_drowned_sun : public InstanceMapScript
{
public:
    instance_drowned_sun() : InstanceMapScript(DrownedSunScriptName, MAP_DROWNED_SUN) {}

    struct instance_drowned_sun_InstanceMapScript : public InstanceScript
    {
        instance_drowned_sun_InstanceMapScript(Map* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(MAX_ENCOUNTERS);
            LoadObjectData(creatureData, nullptr);
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);
            if (go->GetEntry() == GO_SLUICE_WHEEL)
            {
                _wheels.push_back(go->GetGUID());
            }
        }

        void SetData(uint32 type, uint32 /*value*/) override
        {
            if (type != DATA_WHEEL_TURNED || GetBossState(DATA_SLUICES) != IN_PROGRESS)
            {
                return;
            }
            _flood = _flood ? _flood - 1 : 0;
            if (++_wheelsTurned < WHEELS)
            {
                DoSendNotifyToInstance("A sluice wheel grinds round and the water falls. (%u of %u)", _wheelsTurned, WHEELS);
                return;
            }
            SetBossState(DATA_SLUICES, DONE);
        }

        void SetGuidData(uint32 type, ObjectGuid guid) override
        {
            if (type == DATA_TURNER)
            {
                _turner = guid;
            }
        }

        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
            {
                return false;
            }
            if (state != DONE)
            {
                return true;
            }
            uint32 boss = 0;
            if (type == DATA_SLUICES)
            {
                DoSendNotifyToInstance("The sluices open and the flood pours into the basin. Something vast stirs below.");
                boss = DATA_SEBEKHTI;
            }
            else if (type == DATA_LONG_RAMP)
            {
                DoSendNotifyToInstance("You reach the top of the Long Ramp. The sun rises over the arena...");
                boss = DATA_KHEPRET;
            }
            if (boss)
            {
                if (Creature* creature = GetCreature(boss))
                {
                    creature->AI()->DoAction(ACTION_EMERGE);
                }
            }
            return true;
        }

        void Update(uint32 diff) override
        {
            _tick += diff;
            if (_tick < 500)
            {
                return;
            }
            uint32 elapsed = _tick;
            _tick = 0;
            UpdateSluices(elapsed);
            UpdateLongRamp(elapsed);
        }

    private:
        // Not set yet (TO_BE_DECIDED) counts as not started.
        bool NotStarted(uint32 type) const
        {
            EncounterState state = GetBossState(type);
            return state == NOT_STARTED || state == TO_BE_DECIDED;
        }

        // The Sluices

        void UpdateSluices(uint32 elapsed)
        {
            if (NotStarted(DATA_SLUICES))
            {
                if (!PlayersNear(instance, SLUICES_CENTER, SLUICES_RADIUS).empty())
                {
                    SetBossState(DATA_SLUICES, IN_PROGRESS);
                    _flood = 0;
                    _floodTimer = 0;
                    _secondTimer = 0;
                    _snapjawTimer = SNAPJAW_MS / 2;
                    _wheelsTurned = 0;
                    DoSendNotifyToInstance("The Sluices are flooding! Turn the three wheels on the ledge before the water rises over it.");
                }
                return;
            }
            if (GetBossState(DATA_SLUICES) != IN_PROGRESS)
            {
                return;
            }
            std::vector<Player*> players = PlayersNear(instance, SLUICES_CENTER, SLUICES_RADIUS * 1.5f);
            if (players.empty())
            {
                FailSluices();
                return;
            }
            // Everything below happens once a second.
            _secondTimer += elapsed;
            if (_secondTimer < 1000)
            {
                return;
            }
            elapsed = _secondTimer;
            _secondTimer = 0;

            _floodTimer += elapsed;
            if (_floodTimer >= FLOOD_RISE_MS && _flood < FLOOD_MAX)
            {
                _floodTimer = 0;
                ++_flood;
                DoSendNotifyToInstance(_flood < FLOOD_AT_LEDGE ? "The water rises." : "The water is over the ledge! Get out of it, or drown.");
            }
            if (_flood >= FLOOD_AT_LEDGE)
            {
                for (Player* player : players)
                {
                    if (player->IsInWater())
                    {
                        player->EnvironmentalDamage(DAMAGE_DROWNING, DROWNING_PER_LEVEL * (_flood - FLOOD_AT_LEDGE + 1));
                    }
                }
            }

            _snapjawTimer += elapsed;
            if (_snapjawTimer >= SNAPJAW_MS)
            {
                _snapjawTimer = 0;
                Player* turner = ObjectAccessor::GetPlayer(instance, _turner);
                Player* target = turner && turner->IsAlive() ? turner : players[urand(0, players.size() - 1)];
                uint32 count = players.size() > 2 ? 2 : 1;
                for (uint32 i = 0; i < count; ++i)
                {
                    Position const& at = SNAPJAW_SPAWNS[urand(0, std::size(SNAPJAW_SPAWNS) - 1)];
                    if (Creature* snapjaw = target->SummonCreature(NPC_NAVE_SNAPJAW, at, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15000))
                    {
                        _summons.push_back(snapjaw->GetGUID());
                        snapjaw->AI()->AttackStart(target);
                    }
                }
            }
        }

        void FailSluices()
        {
            SetBossState(DATA_SLUICES, NOT_STARTED);
            for (ObjectGuid guid : _wheels)
            {
                if (GameObject* wheel = instance->GetGameObject(guid))
                {
                    wheel->SetGoState(GO_STATE_READY);
                    wheel->AI()->Reset();
                }
            }
            DespawnSummons();
        }

        // The Long Ramp

        std::vector<Player*> PlayersOnRamp()
        {
            std::vector<Player*> players;
            instance->DoForAllPlayers([&](Player* player) {
                if (player->IsAlive() && !player->IsGameMaster() && player->GetPositionX() < RAMP_BOTTOM_X + 4 &&
                    player->GetPositionX() > RAMP_TOP_X && std::abs(player->GetPositionY()) < RAMP_HALF_WIDTH + 3)
                {
                    players.push_back(player);
                }
            });
            return players;
        }

        void UpdateLongRamp(uint32 elapsed)
        {
            if (NotStarted(DATA_LONG_RAMP))
            {
                if (!PlayersOnRamp().empty())
                {
                    SetBossState(DATA_LONG_RAMP, IN_PROGRESS);
                    _sunTimer = SUN_MS;
                    DoSendNotifyToInstance("Suns roll down the Long Ramp! Watch the top step: a flaring sun shows its lane.");
                }
                return;
            }
            if (GetBossState(DATA_LONG_RAMP) != IN_PROGRESS)
            {
                return;
            }
            if (!PlayersNear(instance, ARENA_CENTER, ARENA_RADIUS).empty())
            {
                SetBossState(DATA_LONG_RAMP, DONE);
                return;
            }
            if (PlayersOnRamp().empty())
            {
                SetBossState(DATA_LONG_RAMP, NOT_STARTED);
                return;
            }
            _sunTimer += elapsed;
            if (_sunTimer >= SUN_MS)
            {
                _sunTimer = 0;
                Position top = { RAMP_TOP_X, LANES[urand(0, std::size(LANES) - 1)], RAMP_TOP_Z + 1.5f, 0.0f };
                instance->SummonCreature(NPC_ROLLING_SUN, top);
            }
        }

        void DespawnSummons()
        {
            for (ObjectGuid guid : _summons)
            {
                if (Creature* creature = instance->GetCreature(guid))
                {
                    creature->DespawnOrUnsummon();
                }
            }
            _summons.clear();
        }

        std::vector<ObjectGuid> _wheels;
        std::vector<ObjectGuid> _summons;
        ObjectGuid _turner;
        uint32 _tick = 0;
        uint32 _secondTimer = 0;
        uint32 _floodTimer = 0;
        uint32 _snapjawTimer = 0;
        uint32 _sunTimer = 0;
        uint8 _flood = 0;
        uint8 _wheelsTurned = 0;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_drowned_sun_InstanceMapScript(map);
    }
};

void AddSC_instance_drowned_sun()
{
    new instance_drowned_sun();
}
