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
// The Scales (Hall of Weighing): each pan asks for a weight, made of stones (3) and feathers (1) that players carry from
// two piles one at a time (go_weighing in hall_of_weighing.cpp). Too much spills a pan back to nothing. Mummies shuffle
// after whoever carries something and knock it out of their hands. Both pans exact within two minutes wakes Anpuet;
// otherwise the scales empty and ask again.
//
// The Lightless Way (Eclipse Pit): out of the light, players turn Unlit (Darken in drowned_sun.h). Light comes from lit
// braziers and from torches: anyone can take a torch from a lit brazier and light a cold one with it (go_lightless_brazier
// in eclipse_pit.cpp). Every 25 seconds the serpent's coils slide through the rock and put one out. With every brazier
// burning at once, Apepheth wakes in the pit.
//
// A trial resets when everyone in it has left or died.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include "Chat.h"
#include "CreatureAI.h"
#include "MotionMaster.h"
#include "StringFormat.h"
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
    { NPC_ANPUET, DATA_ANPUET },
    { NPC_APEPHETH, DATA_APEPHETH },
    { 0, 0 },
};

// The Sluices
uint32 const FLOOD_RISE_MS = 30000;
uint8 const FLOOD_AT_LEDGE = 3; // the level at which the water is over the ledge
uint8 const FLOOD_MAX = 6;
uint32 const DROWNING_PER_LEVEL = 400; // every second, per level from the ledge up
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

// The Scales
uint32 const SCALES_MS = 120000;
uint8 const STONE = 3;
uint8 const FEATHER = 1;
float const MUMMY_REACH = 2.5f;
float const MUMMY_SPEED = 3.0f; // yards a second: slower than a player, faster than one who isn't watching

// The Lightless Way
uint32 const BRAZIERS = std::size(LIGHTLESS_BRAZIERS); // the first, at the court end, always burns
float const BRAZIER_LIGHT = 14.0f;
float const TORCH_LIGHT = 8.0f;
uint32 const SNUFF_MS = 25000;
uint32 const SPELL_TORCH = 71706; // Ball of Flames Visual: the torch a player carries

// How far a point is from the Lightless Way's middle line.
float DistanceToWay(Position const& p)
{
    float best = 1e9f;
    for (uint32 i = 0; i + 1 < std::size(LIGHTLESS_WAY); ++i)
    {
        float ax = LIGHTLESS_WAY[i].GetPositionX(), ay = LIGHTLESS_WAY[i].GetPositionY();
        float dx = LIGHTLESS_WAY[i + 1].GetPositionX() - ax, dy = LIGHTLESS_WAY[i + 1].GetPositionY() - ay;
        float t = std::clamp(((p.GetPositionX() - ax) * dx + (p.GetPositionY() - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
        best = std::min(best, std::hypot(p.GetPositionX() - ax - t * dx, p.GetPositionY() - ay - t * dy));
    }
    return best;
}
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
            if (go->GetEntry() == GO_LIGHTLESS_BRAZIER)
            {
                for (uint32 i = 0; i < BRAZIERS; ++i)
                {
                    if (go->GetExactDist2d(&LIGHTLESS_BRAZIERS[i]) < 3.0f)
                    {
                        _braziers[i] = go->GetGUID(); // the first is lit on the next update, not while its grid loads
                    }
                }
            }
        }

        ObjectGuid GetGuidData(uint32 type) const override
        {
            return type == DATA_TORCH_BEARER ? _torchBearer : InstanceScript::GetGuidData(type);
        }

        void SetData(uint32 type, uint32 value) override
        {
            if (type == DATA_BRAZIER_USED)
            {
                UseBrazier(value);
                return;
            }
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
            switch (type)
            {
                case DATA_TURNER:
                    _turner = guid;
                    break;
                case DATA_PICK_STONE:
                case DATA_PICK_FEATHER:
                case DATA_PLACE_LEFT:
                case DATA_PLACE_RIGHT:
                    UseScales(type, guid);
                    break;
                case DATA_BRAZIER_USER:
                    _brazierUser = guid;
                    break;
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
            if (type == DATA_APEPHETH) // the sun is back: torches go out
            {
                instance->DoForAllPlayers([&](Player* player) { DropTorch(player); });
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
            else if (type == DATA_SCALES)
            {
                DoSendNotifyToInstance("The scales hang level. In the hall beyond, the Embalmer rises from his work.");
                Despawn(_mummies);
                _carrying.clear();
                boss = DATA_ANPUET;
            }
            else if (type == DATA_LIGHTLESS_WAY)
            {
                DoSendNotifyToInstance("Every brazier burns. Light spills into the pit, and the dark there begins to move...");
                boss = DATA_APEPHETH;
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
            UpdateScales(elapsed);
            // Mounts are allowed in the instance (instance_template.allowMount) for the Sun Court only: riding out of it
            // ends the ride.
            instance->DoForAllPlayers([](Player* player) {
                if (player->IsMounted() && player->GetAreaId() != AREA_SUN_COURT)
                {
                    player->RemoveAurasByType(SPELL_AURA_MOUNTED);
                    ChatHandler(player->GetSession()).SendNotification("Mounts can only be ridden in the Sun Court.");
                }
            });
            _lightlessTimer += elapsed;
            if (_lightlessTimer >= 1000)
            {
                UpdateLightlessWay(_lightlessTimer);
                _lightlessTimer = 0;
            }
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
                        _snapjaws.push_back(snapjaw->GetGUID());
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
            Despawn(_snapjaws);
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

        // The Scales

        static void Tell(Player* player, std::string const& text)
        {
            ChatHandler(player->GetSession()).SendNotification("{}", text);
        }

        std::string Pans() const
        {
            return Acore::StringFormat("Left pan {} of {}, right pan {} of {}.", int(_left), int(_leftNeed), int(_right), int(_rightNeed));
        }

        void StartScales()
        {
            SetBossState(DATA_SCALES, IN_PROGRESS);
            _scalesTime = 0;
            _mummyTimer = 0;
            _left = _right = 0;
            _leftNeed = urand(7, 11);
            _rightNeed = urand(5, 9);
            _carrying.clear();
            DoSendNotifyToInstance("Weigh true: the left pan asks for %u, the right for %u. A stone weighs 3, a feather 1. Two minutes.",
                                   uint32(_leftNeed), uint32(_rightNeed));
            uint32 mummies = instance->GetPlayersCountExceptGMs() > 2 ? 3 : 2;
            for (uint32 i = 0; i < mummies; ++i)
            {
                float angle = 2 * float(M_PI) * i / mummies;
                Position at = { SCALES_CENTER.GetPositionX() + 15 * std::cos(angle), SCALES_CENTER.GetPositionY() + 15 * std::sin(angle), SCALES_CENTER.GetPositionZ(), 0.0f };
                if (Creature* mummy = instance->SummonCreature(NPC_HALL_MUMMY, at))
                {
                    _mummies.push_back(mummy->GetGUID());
                }
            }
        }

        void UseScales(uint32 type, ObjectGuid guid)
        {
            Player* player = ObjectAccessor::GetPlayer(instance, guid);
            if (!player || GetBossState(DATA_SCALES) != IN_PROGRESS)
            {
                return;
            }
            uint8& carried = _carrying[guid];
            if (type == DATA_PICK_STONE || type == DATA_PICK_FEATHER)
            {
                if (carried)
                {
                    Tell(player, "You can carry only one thing at a time.");
                    return;
                }
                carried = type == DATA_PICK_STONE ? STONE : FEATHER;
                Tell(player, carried == STONE ? "You lift a stone weight (3)." : "You pick up a feather (1).");
                return;
            }
            if (!carried)
            {
                Tell(player, "You have nothing to put on the scale.");
                return;
            }
            bool left = type == DATA_PLACE_LEFT;
            uint8& pan = left ? _left : _right;
            uint8 need = left ? _leftNeed : _rightNeed;
            pan += carried;
            carried = 0;
            if (pan > need)
            {
                pan = 0;
                DoSendNotifyToInstance("Too heavy! The %s pan tips and spills. %s", left ? "left" : "right", Pans().c_str());
                return;
            }
            if (_left == _leftNeed && _right == _rightNeed)
            {
                SetBossState(DATA_SCALES, DONE);
                return;
            }
            DoSendNotifyToInstance("%s", Pans().c_str());
        }

        void UpdateScales(uint32 elapsed)
        {
            if (NotStarted(DATA_SCALES))
            {
                if (!PlayersNear(instance, SCALES_CENTER, SCALES_RADIUS).empty())
                {
                    StartScales();
                }
                return;
            }
            if (GetBossState(DATA_SCALES) != IN_PROGRESS)
            {
                return;
            }
            std::vector<Player*> players = PlayersNear(instance, SCALES_CENTER, SCALES_RADIUS + 10);
            _scalesTime += elapsed;
            if (players.empty() || _scalesTime >= SCALES_MS)
            {
                if (!players.empty())
                {
                    DoSendNotifyToInstance("Time! The scales empty themselves.");
                }
                SetBossState(DATA_SCALES, NOT_STARTED);
                Despawn(_mummies);
                return;
            }
            if (_scalesTime / 30000 != (_scalesTime - elapsed) / 30000)
            {
                DoSendNotifyToInstance("%u seconds left. %s", (SCALES_MS - _scalesTime) / 1000, Pans().c_str());
            }

            // Mummies shuffle after the nearest player carrying something and knock it away; with nobody carrying,
            // they wander.
            _mummyTimer += elapsed;
            bool wander = _mummyTimer >= 4000;
            if (wander)
            {
                _mummyTimer = 0;
            }
            for (ObjectGuid mummyGuid : _mummies)
            {
                Creature* mummy = instance->GetCreature(mummyGuid);
                if (!mummy || !mummy->IsAlive())
                {
                    continue;
                }
                Player* nearest = nullptr;
                for (Player* player : players)
                {
                    if (_carrying[player->GetGUID()] && (!nearest || mummy->GetExactDist2d(player) < mummy->GetExactDist2d(nearest)))
                    {
                        nearest = player;
                    }
                }
                if (!nearest)
                {
                    if (wander)
                    {
                        float angle = frand(0.0f, 2 * float(M_PI)), r = frand(0.0f, SCALES_RADIUS - 6);
                        mummy->GetMotionMaster()->MovePoint(0, SCALES_CENTER.GetPositionX() + r * std::cos(angle),
                                                           SCALES_CENTER.GetPositionY() + r * std::sin(angle), SCALES_CENTER.GetPositionZ(),
                                                           FORCED_MOVEMENT_NONE, MUMMY_SPEED);
                    }
                    continue;
                }
                if (mummy->GetExactDist2d(nearest) < MUMMY_REACH)
                {
                    _carrying[nearest->GetGUID()] = 0;
                    Tell(nearest, "A mummy knocks it out of your hands!");
                    nearest->KnockbackFrom(mummy->GetPositionX(), mummy->GetPositionY(), 8.0f, 4.0f);
                    continue;
                }
                mummy->GetMotionMaster()->MovePoint(0, nearest->GetPositionX(), nearest->GetPositionY(), nearest->GetPositionZ(),
                                                   FORCED_MOVEMENT_NONE, MUMMY_SPEED);
            }
        }

        // The Lightless Way

        // Lights or puts out a brazier: its flame burns over it while lit.
        void SetLit(uint32 index, bool lit)
        {
            _lit[index] = lit;
            if (Creature* flame = instance->GetCreature(_flames[index]))
            {
                flame->DespawnOrUnsummon();
            }
            _flames[index].Clear();
            GameObject* brazier = instance->GetGameObject(_braziers[index]);
            if (brazier)
            {
                brazier->SetGoState(lit ? GO_STATE_ACTIVE : GO_STATE_READY);
            }
            if (lit)
            {
                Position above = LIGHTLESS_BRAZIERS[index];
                above.m_positionZ += 2.0f;
                if (Creature* flame = instance->SummonCreature(NPC_BRAZIER_FLAME, above))
                {
                    _flames[index] = flame->GetGUID();
                }
            }
        }

        bool HasTorch(Player* player) const
        {
            return _torches.count(player->GetGUID()) != 0;
        }

        void DropTorch(Player* player)
        {
            _torches.erase(player->GetGUID());
            player->RemoveAurasDueToSpell(SPELL_TORCH);
        }

        void UseBrazier(uint32 index)
        {
            Player* player = ObjectAccessor::GetPlayer(instance, _brazierUser);
            if (!player || index >= BRAZIERS || GetBossState(DATA_LIGHTLESS_WAY) == DONE)
            {
                return;
            }
            if (_lit[index])
            {
                if (!HasTorch(player))
                {
                    _torches.insert(player->GetGUID());
                    player->AddAura(SPELL_TORCH, player);
                    Tell(player, "You take a burning torch from the brazier.");
                }
                return;
            }
            if (!HasTorch(player))
            {
                Tell(player, "The brazier is cold. Bring fire from a burning one.");
                return;
            }
            SetLit(index, true);
            _torchBearer = player->GetGUID();
            uint32 lit = 0;
            for (uint32 i = 0; i < BRAZIERS; ++i)
            {
                lit += _lit[i] ? 1 : 0;
            }
            if (lit == BRAZIERS)
            {
                SetBossState(DATA_LIGHTLESS_WAY, DONE);
                return;
            }
            DoSendNotifyToInstance("A brazier flares up. (%u of %u burning)", lit, BRAZIERS);
        }

        // Living players (not GMs) on the way itself: near its line, past the court, short of the pit.
        std::vector<Player*> PlayersOnWay()
        {
            std::vector<Player*> players;
            instance->DoForAllPlayers([&](Player* player) {
                if (player->IsAlive() && !player->IsGameMaster() && player->GetPositionX() > LIGHTLESS_WAY[0].GetPositionX() + 2 &&
                    DistanceToWay(*player) < 12.0f && player->GetExactDist2d(&PIT_CENTER) > PIT_RADIUS)
                {
                    players.push_back(player);
                }
            });
            return players;
        }

        void UpdateLightlessWay(uint32 elapsed)
        {
            if (!_lit[0] && _braziers[0])
            {
                SetLit(0, true);
            }
            std::vector<Player*> players = PlayersOnWay();
            if (NotStarted(DATA_LIGHTLESS_WAY))
            {
                if (!players.empty())
                {
                    SetBossState(DATA_LIGHTLESS_WAY, IN_PROGRESS);
                    _snuffTimer = 0;
                    DoSendNotifyToInstance("The dark eats you here. Take fire from the brazier and light the way to the pit.");
                }
                return;
            }
            if (GetBossState(DATA_LIGHTLESS_WAY) != IN_PROGRESS)
            {
                return;
            }
            if (players.empty() && PlayersNear(instance, PIT_CENTER, PIT_RADIUS).empty())
            {
                SetBossState(DATA_LIGHTLESS_WAY, NOT_STARTED);
                for (uint32 i = 1; i < BRAZIERS; ++i)
                {
                    SetLit(i, false);
                }
                instance->DoForAllPlayers([&](Player* player) { DropTorch(player); });
                return;
            }
            // Torches go out with whoever carried them.
            instance->DoForAllPlayers([&](Player* player) {
                if (!player->IsAlive() && HasTorch(player))
                {
                    DropTorch(player);
                }
            });
            for (Player* player : players)
            {
                bool lit = false;
                for (uint32 i = 0; i < BRAZIERS && !lit; ++i)
                {
                    lit = _lit[i] && player->GetExactDist2d(&LIGHTLESS_BRAZIERS[i]) < BRAZIER_LIGHT;
                }
                instance->DoForAllPlayers([&](Player* other) {
                    if (!lit && HasTorch(other) && other->GetExactDist2d(player) < TORCH_LIGHT)
                    {
                        lit = true;
                    }
                });
                lit ? Lighten(player) : Darken(player);
            }
            _snuffTimer += elapsed;
            if (_snuffTimer >= SNUFF_MS)
            {
                _snuffTimer = 0;
                std::vector<uint32> burning;
                for (uint32 i = 1; i < BRAZIERS; ++i)
                {
                    if (_lit[i])
                    {
                        burning.push_back(i);
                    }
                }
                if (!burning.empty())
                {
                    SetLit(burning[urand(0, burning.size() - 1)], false);
                    DoSendNotifyToInstance("Something vast slides through the rock beside you... and a brazier goes dark.");
                }
            }
        }

        void Despawn(std::vector<ObjectGuid>& creatures)
        {
            for (ObjectGuid guid : creatures)
            {
                if (Creature* creature = instance->GetCreature(guid))
                {
                    creature->DespawnOrUnsummon();
                }
            }
            creatures.clear();
        }

        std::vector<ObjectGuid> _wheels;
        std::vector<ObjectGuid> _snapjaws, _mummies;
        std::map<ObjectGuid, uint8> _carrying; // what each player holds at the Scales: a stone, a feather or nothing
        uint32 _scalesTime = 0;
        uint32 _mummyTimer = 0;
        uint8 _left = 0, _right = 0, _leftNeed = 0, _rightNeed = 0;
        ObjectGuid _braziers[BRAZIERS];
        ObjectGuid _flames[BRAZIERS];
        bool _lit[BRAZIERS] = {};
        std::set<ObjectGuid> _torches;
        ObjectGuid _brazierUser;
        ObjectGuid _torchBearer;
        uint32 _lightlessTimer = 0;
        uint32 _snuffTimer = 0;
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
