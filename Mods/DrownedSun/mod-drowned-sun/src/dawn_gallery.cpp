// The Dawn Gallery: the Long Ramp's rolling suns and Khepret, Who Rolls the Sun, in his arena. Stand-in models: the
// Ball of Flame for every sun, a giant scarab for Khepret and Qiraji scarabs for the dusk swarm.
//
// Khepret wakes when someone reaches the top of the Long Ramp, then:
// - The Sun's Path: at dawn his sun drops embers behind it that burn whoever stands in them, until dusk puts them out.
// - Dawn and Dusk, alternating every 45 seconds. At dawn the sun blazes every 10 seconds: each player takes a share of
//   the fire split with everyone within 8 yards of them, so standing together halves it, three together a third...
//   At dusk scarabs pour from the arena's edge and burst on the first player they reach.
// - Roll: every 20 seconds he picks a player and charges straight through where they stood, to the far side; anyone in
//   the way is thrown aside and burned.
// His health is set for the number of players when the fight starts.
#include "CreatureScript.h"
#include "ScriptedCreature.h"
#include "StringFormat.h"
#include "drowned_sun.h"

namespace
{
uint32 const SUN_FLARE_MS = 2500;      // a rolling sun waits at the top this long, as the warning
float const SUN_ROLL_SPEED = 16.0f;    // yards a second
float const SUN_REACH = 3.0f;
int32 const SUN_HIT_PCT = 10;          // of the player's health

uint32 const HEALTH_PER_PLAYER = 400000;
int32 const TRAIL_PCT = 3;             // of the player's health every second in an ember
float const TRAIL_REACH = 2.5f;
int32 const BLAZE_PCT = 60;            // of the player's health, before sharing
float const BLAZE_SHARE = 8.0f;
int32 const SCARAB_BURST_PCT = 20;
float const SCARAB_BURST_REACH = 6.0f;
int32 const ROLL_HIT_PCT = 15;
float const ROLL_REACH = 5.0f;

enum Phases
{
    PHASE_DAWN = 1,
    PHASE_DUSK = 2,
};

enum Points
{
    POINT_BOTTOM = 1,
    POINT_ROLL_END = 2,
};

// Deals a share of a player's health as fire, attributed to the caster.
void Burn(Unit* caster, Unit* target, int32 pct)
{
    Unit::DealDamage(caster, target, target->CountPctFromMaxHealth(pct), nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_FIRE);
}
} // namespace

// A sun on the Long Ramp: flares at the top of its lane, then rolls to the bottom, throwing whoever it touches down the
// ramp. Each player is hit at most once per sun.
struct npc_rolling_sun : public ScriptedAI
{
    npc_rolling_sun(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        _waited = 0;
        _rolling = false;
        _hit.clear();
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE && id == POINT_BOTTOM)
        {
            me->DespawnOrUnsummon();
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_rolling)
        {
            _waited += diff;
            if (_waited >= SUN_FLARE_MS)
            {
                _rolling = true;
                me->GetMotionMaster()->MovePoint(POINT_BOTTOM, RAMP_BOTTOM_X, me->GetPositionY(), RAMP_BOTTOM_Z + 1.5f, FORCED_MOVEMENT_NONE, SUN_ROLL_SPEED, 0.0f, false);
            }
            return;
        }
        for (Player* player : PlayersNear(me->GetMap(), *me, SUN_REACH))
        {
            if (std::find(_hit.begin(), _hit.end(), player->GetGUID()) != _hit.end())
            {
                continue;
            }
            _hit.push_back(player->GetGUID());
            // Away from a point just uphill of the sun: down the ramp, and a long way.
            player->KnockbackFrom(me->GetPositionX() - 3.0f, player->GetPositionY(), 28.0f, 9.0f);
            Burn(me, player, SUN_HIT_PCT);
        }
    }

private:
    uint32 _waited = 0;
    bool _rolling = false;
    std::vector<ObjectGuid> _hit;
};

// An ember on the Sun's Path: burns whoever stands in it.
struct npc_sun_trail : public ScriptedAI
{
    npc_sun_trail(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        _timer = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        _timer += diff;
        if (_timer < 1000)
        {
            return;
        }
        _timer = 0;
        for (Player* player : PlayersNear(me->GetMap(), *me, TRAIL_REACH))
        {
            Burn(me, player, TRAIL_PCT);
        }
    }

private:
    uint32 _timer = 0;
};

// A dusk scarab: runs at the player it was sent after and bursts when it reaches anyone.
struct npc_dusk_scarab : public ScriptedAI
{
    npc_dusk_scarab(Creature* creature) : ScriptedAI(creature) {}

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
        {
            return;
        }
        if (PlayersNear(me->GetMap(), *me, 3.0f).empty())
        {
            return;
        }
        for (Player* player : PlayersNear(me->GetMap(), *me, SCARAB_BURST_REACH))
        {
            Burn(me, player, SCARAB_BURST_PCT);
        }
        me->KillSelf();
    }
};

struct boss_khepret : public BossAI
{
    boss_khepret(Creature* creature) : BossAI(creature, DATA_KHEPRET) {}

    void Reset() override
    {
        BossAI::Reset();
        _sun.Clear();
        if (instance->GetBossState(DATA_LONG_RAMP) == DONE)
        {
            Show(true);
        }
        else
        {
            Show(false);
        }
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_EMERGE)
        {
            Show(true);
            me->Yell("The sun must climb. Who stands in its path?", LANG_UNIVERSAL);
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        uint32 players = std::max<uint32>(1, me->GetMap()->GetPlayersCountExceptGMs());
        me->SetMaxHealth(HEALTH_PER_PLAYER * players);
        me->SetFullHealth();
        me->Yell("Dawn! Burn with it.", LANG_UNIVERSAL);
        if (Creature* sun = me->SummonCreature(NPC_KHEPRET_SUN, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() + 2.0f))
        {
            _sun = sun->GetGUID();
            sun->GetMotionMaster()->MoveFollow(me, 6.0f, 0.0f);
        }
        Dawn();

        scheduler.Schedule(2s, [this](TaskContext context)
        {
            if (_phase == PHASE_DAWN)
            {
                if (Creature* sun = ObjectAccessor::GetCreature(*me, _sun))
                {
                    me->SummonCreature(NPC_SUN_TRAIL, sun->GetPositionX(), sun->GetPositionY(), sun->GetPositionZ() - 1.5f);
                }
            }
            context.Repeat(2s);
        }).Schedule(10s, [this](TaskContext context)
        {
            if (_phase == PHASE_DAWN)
            {
                Blaze();
            }
            context.Repeat(10s);
        }).Schedule(3s, [this](TaskContext context)
        {
            if (_phase == PHASE_DUSK)
            {
                Swarm();
            }
            context.Repeat(3s);
        }).Schedule(45s, [this](TaskContext context)
        {
            _phase == PHASE_DAWN ? Dusk() : Dawn();
            context.Repeat(45s);
        }).Schedule(20s, [this](TaskContext context)
        {
            Roll();
            context.Repeat(20s);
        });
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE && id == POINT_ROLL_END)
        {
            _rolling = false;
            me->SetReactState(REACT_AGGRESSIVE);
            if (Unit* victim = me->GetVictim())
            {
                me->GetMotionMaster()->MoveChase(victim);
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        BossAI::UpdateAI(diff);
        if (!_rolling)
        {
            return;
        }
        for (Player* player : PlayersNear(me->GetMap(), *me, ROLL_REACH))
        {
            if (std::find(_rolled.begin(), _rolled.end(), player->GetGUID()) == _rolled.end())
            {
                _rolled.push_back(player->GetGUID());
                player->KnockbackFrom(me->GetPositionX(), me->GetPositionY(), 18.0f, 8.0f);
                Burn(me, player, ROLL_HIT_PCT);
            }
        }
    }

    void JustDied(Unit* killer) override
    {
        BossAI::JustDied(killer);
        me->Yell("Dusk... forever...", LANG_UNIVERSAL);
        me->SummonGameObject(GO_KHEPRET_HOARD, ARENA_CENTER.GetPositionX(), ARENA_CENTER.GetPositionY(), ARENA_CENTER.GetPositionZ(), 0, 0, 0, 0, 0, WEEK);
    }

private:
    void Show(bool shown)
    {
        me->SetVisible(shown);
        me->SetReactState(shown ? REACT_AGGRESSIVE : REACT_PASSIVE);
        if (shown)
        {
            me->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
            me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        }
        else
        {
            me->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        }
    }

    std::vector<Player*> Players()
    {
        return PlayersNear(me->GetMap(), ARENA_CENTER, ARENA_RADIUS + 8);
    }

    void Dawn()
    {
        _phase = PHASE_DAWN;
        me->TextEmote("Dawn breaks. Khepret's sun blazes and burns a path behind it. Stand together when it flares!", nullptr, true);
    }

    void Dusk()
    {
        _phase = PHASE_DUSK;
        summons.DespawnEntry(NPC_SUN_TRAIL);
        me->TextEmote("Dusk falls. The sun's path goes dark, and scarabs pour from the walls!", nullptr, true);
    }

    // Each player takes BLAZE_PCT of their health shared with everyone standing close to them.
    void Blaze()
    {
        me->TextEmote("The sun blazes!", nullptr, true);
        for (Player* player : Players())
        {
            int32 sharing = int32(PlayersNear(me->GetMap(), *player, BLAZE_SHARE).size());
            Burn(me, player, BLAZE_PCT / std::max(1, sharing));
        }
    }

    void Swarm()
    {
        std::vector<Player*> players = Players();
        if (players.empty())
        {
            return;
        }
        uint32 count = 1 + players.size() / 2;
        for (uint32 i = 0; i < count; ++i)
        {
            float angle = frand(0.0f, 2 * float(M_PI));
            float x = ARENA_CENTER.GetPositionX() + (ARENA_RADIUS - 2) * std::cos(angle);
            float y = ARENA_CENTER.GetPositionY() + (ARENA_RADIUS - 2) * std::sin(angle);
            if (Creature* scarab = me->SummonCreature(NPC_DUSK_SCARAB, x, y, ARENA_CENTER.GetPositionZ() + 1.0f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5000))
            {
                scarab->AI()->AttackStart(players[urand(0, players.size() - 1)]);
            }
        }
    }

    // Charges through a random player's spot to the arena's far edge.
    void Roll()
    {
        Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 80.0f, true);
        if (!target)
        {
            return;
        }
        float angle = me->GetAngle(target);
        float x = ARENA_CENTER.GetPositionX(), y = ARENA_CENTER.GetPositionY();
        // From Khepret through the target, to where that line leaves the arena.
        float dx = std::cos(angle), dy = std::sin(angle);
        float px = me->GetPositionX() - x, py = me->GetPositionY() - y;
        float b = px * dx + py * dy, c = px * px + py * py - (ARENA_RADIUS - 4) * (ARENA_RADIUS - 4);
        float t = -b + std::sqrt(std::max(0.0f, b * b - c));
        me->TextEmote(Acore::StringFormat("Khepret lowers his head towards {} and charges the sun across the arena!", target->GetName()), nullptr, true);
        _rolled.clear();
        _rolling = true;
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->GetMotionMaster()->MoveCharge(me->GetPositionX() + dx * t, me->GetPositionY() + dy * t, ARENA_CENTER.GetPositionZ(), 30.0f, POINT_ROLL_END);
    }

    ObjectGuid _sun;
    std::vector<ObjectGuid> _rolled;
    uint8 _phase = PHASE_DAWN;
    bool _rolling = false;
};

void AddSC_dawn_gallery()
{
    RegisterDrownedSunCreatureAI(npc_rolling_sun);
    RegisterDrownedSunCreatureAI(npc_sun_trail);
    RegisterDrownedSunCreatureAI(npc_dusk_scarab);
    RegisterDrownedSunCreatureAI(boss_khepret);
}
