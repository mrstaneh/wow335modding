// The Flooded Nave: the Sluices' wheels and Sebekhti, the River's Jaw, in his basin. Stand-in spells and models until
// the art pass (Mods/DrownedSun/DESIGN.md, milestone 6).
//
// Sebekhti lies submerged and untouchable until the Sluices are drained, then:
// - Death Roll: seizes a random player (not the tank) and rolls; they are stunned and drowning until the others deal
//   ROLL_FREE_PCT of his health, or after ROLL_MS they drown outright.
// - Undertow: drags everyone towards him, and a moment later his tail sweeps whoever is in melee range away.
// - Rising Flood, at 70% and 35%: the basin floods a level; anyone standing below it is slowed and healed for half.
//   The islands stay dry.
// - Ambush: sinks out of sight and bursts up under the player furthest away, throwing them into the air.
// His health is set for the number of players when the fight starts.
#include "Chat.h"
#include "CreatureScript.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameObjectScript.h"
#include "InstanceScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "StringFormat.h"
#include "drowned_sun.h"

namespace
{
uint32 const WHEEL_TURN_MS = 10000;
float const WHEEL_REACH = 5.0f; // yards, on the ground: the wheel stands up on the ledge

enum Spells
{
    SPELL_DEATH_ROLL = 20170, // Stun
    SPELL_DEEP_WATER_SLOW = 25809, // Crippling Poison: 50% slower
    SPELL_DEEP_WATER_HEAL = 13737, // Mortal Strike: 50% less healing
    SPELL_TAIL_SWEEP = 10101, // Knock Away
};

uint32 const HEALTH_PER_PLAYER = 300000;
int32 const ROLL_MS = 12000;
int32 const ROLL_FREE_PCT = 3;      // of his health, dealt while he holds someone
int32 const ROLL_DROWN_PCT = 3;     // of the held player's health, every second
int32 const ROLL_DROWNED_PCT = 60;  // of the held player's health when time runs out
int32 const AMBUSH_PCT = 15;        // of the ambushed player's health
float const FLOOD_LEVELS[] = { 6.0f, 7.8f }; // under these heights the water is deep: the basin's floor, then the ledge
int32 const DEEP_WATER_MS = 2500;
} // namespace

// A sluice wheel: a player takes hold of it (uses it) and turns it by staying close for WHEEL_TURN_MS in all.
struct go_sluice_wheel : public GameObjectAI
{
    go_sluice_wheel(GameObject* go) : GameObjectAI(go), _instance(go->GetInstanceScript()) {}

    void Reset() override
    {
        _turner.Clear();
        _turned = 0;
        _shown = 0;
        _done = false;
    }

    bool GossipHello(Player* player, bool reportUse) override
    {
        if (reportUse) // the same click, reported again
        {
            return true;
        }
        if (_done || !_instance || _instance->GetBossState(DATA_SLUICES) != IN_PROGRESS)
        {
            return true;
        }
        _turner = player->GetGUID();
        _instance->SetGuidData(DATA_TURNER, _turner);
        ChatHandler(player->GetSession()).SendNotification("You put your weight on the wheel. Stay at it!");
        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (_done || !_turner)
        {
            return;
        }
        Player* player = ObjectAccessor::GetPlayer(*me, _turner);
        if (!player || !player->IsAlive() || player->GetExactDist2d(me) > WHEEL_REACH)
        {
            if (player)
            {
                ChatHandler(player->GetSession()).SendNotification("You let go of the wheel.");
            }
            _turner.Clear();
            return;
        }
        _turned += diff;
        if (_turned >= WHEEL_TURN_MS)
        {
            _done = true;
            _turner.Clear();
            me->SetGoState(GO_STATE_ACTIVE);
            _instance->SetData(DATA_WHEEL_TURNED, 1);
            return;
        }
        uint32 percent = _turned * 100 / WHEEL_TURN_MS;
        if (percent / 20 != _shown)
        {
            _shown = percent / 20;
            ChatHandler(player->GetSession()).SendNotification("The wheel turns... {}%", percent);
        }
    }

private:
    InstanceScript* _instance;
    ObjectGuid _turner;
    uint32 _turned = 0;
    uint32 _shown = 0;
    bool _done = false;
};

struct boss_sebekhti : public BossAI
{
    boss_sebekhti(Creature* creature) : BossAI(creature, DATA_SEBEKHTI) {}

    void Reset() override
    {
        BossAI::Reset();
        _flood = 0;
        _roll.Clear();
        _submerged = false;
        if (instance->GetBossState(DATA_SLUICES) == DONE)
        {
            Surface();
        }
        else
        {
            Sink();
        }
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_EMERGE)
        {
            Surface();
            me->Yell("Who opens my river? Who lets the light into my dark water?", LANG_UNIVERSAL);
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        uint32 players = std::max<uint32>(1, me->GetMap()->GetPlayersCountExceptGMs());
        me->SetMaxHealth(HEALTH_PER_PLAYER * players);
        me->SetFullHealth();
        me->Yell("The priest chained me. You will feed me.", LANG_UNIVERSAL);

        scheduler.Schedule(15s, [this](TaskContext context)
        {
            DeathRoll();
            context.Repeat(30s);
        }).Schedule(10s, [this](TaskContext context)
        {
            Undertow();
            context.Repeat(25s);
        }).Schedule(35s, [this](TaskContext context)
        {
            Ambush();
            context.Repeat(40s);
        }).Schedule(1s, [this](TaskContext context)
        {
            UpdateRoll();
            UpdateFlood();
            context.Repeat(1s);
        });

        ScheduleHealthCheckEvent({ 70, 35 }, [this]
        {
            ++_flood;
            me->TextEmote("Sebekhti roars and the basin floods! Deep water slows you and spoils healing: climb the islands!", nullptr, true);
        });
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType type, SpellSchoolMask school) override
    {
        BossAI::DamageTaken(attacker, damage, type, school);
        if (_roll)
        {
            _rollDamage += damage;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        // Under the water he neither moves nor fights; only his scheduled tasks (the Ambush's return) go on.
        if (_submerged)
        {
            scheduler.Update(diff);
            return;
        }
        BossAI::UpdateAI(diff);
    }

    void JustDied(Unit* killer) override
    {
        BossAI::JustDied(killer);
        me->Yell("The river... runs free...", LANG_UNIVERSAL);
        me->SummonGameObject(GO_SEBEKHTI_HOARD, HOARD_ISLAND.GetPositionX(), HOARD_ISLAND.GetPositionY(), HOARD_ISLAND.GetPositionZ(), 0, 0, 0, 0, 0, WEEK);
    }

private:
    void Sink()
    {
        _submerged = true;
        me->SetVisible(false);
        me->SetReactState(REACT_PASSIVE);
        me->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
        me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
    }

    void Surface()
    {
        _submerged = false;
        me->SetVisible(true);
        me->SetReactState(REACT_AGGRESSIVE);
        me->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
    }

    // Living players (not GMs) in the basin.
    std::vector<Player*> Players()
    {
        return PlayersNear(me->GetMap(), BASIN_CENTER, BASIN_RADIUS);
    }

    void DeathRoll()
    {
        if (_submerged || _roll)
        {
            return;
        }
        Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true, false);
        if (!target)
        {
            target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true);
        }
        if (!target)
        {
            return;
        }
        if (Aura* stun = me->AddAura(SPELL_DEATH_ROLL, target))
        {
            stun->SetMaxDuration(ROLL_MS);
            stun->SetDuration(ROLL_MS);
        }
        _roll = target->GetGUID();
        _rollDamage = 0;
        _rollTime = 0;
        me->TextEmote(Acore::StringFormat("Sebekhti seizes {} and rolls into the deep! Strike him to make him let go!", target->GetName()), nullptr, true);
    }

    void UpdateRoll()
    {
        if (!_roll)
        {
            return;
        }
        Player* held = ObjectAccessor::GetPlayer(*me, _roll);
        _rollTime += 1000;
        if (!held || !held->IsAlive())
        {
            _roll.Clear();
            return;
        }
        if (_rollDamage >= me->CountPctFromMaxHealth(ROLL_FREE_PCT))
        {
            me->TextEmote(Acore::StringFormat("Sebekhti lets go of {}.", held->GetName()), nullptr, true);
        }
        else if (_rollTime >= ROLL_MS)
        {
            held->EnvironmentalDamage(DAMAGE_DROWNING, held->CountPctFromMaxHealth(ROLL_DROWNED_PCT));
        }
        else
        {
            held->EnvironmentalDamage(DAMAGE_DROWNING, held->CountPctFromMaxHealth(ROLL_DROWN_PCT));
            return;
        }
        held->RemoveAurasDueToSpell(SPELL_DEATH_ROLL);
        _roll.Clear();
    }

    void Undertow()
    {
        if (_submerged)
        {
            return;
        }
        me->TextEmote("Sebekhti thrashes, and the undertow drags you towards his jaws!", nullptr, true);
        for (Player* player : Players())
        {
            // A hop of 6 up lasts about 0.6 seconds; fast enough sideways to land a few yards short of him.
            float distance = player->GetExactDist2d(me);
            if (distance > 6.0f)
            {
                player->KnockbackFrom(me->GetPositionX(), me->GetPositionY(), -(distance - 5.0f) / 0.62f, 6.0f);
            }
        }
        scheduler.Schedule(2500ms, [this](TaskContext)
        {
            for (Player* player : Players())
            {
                if (player->GetExactDist2d(me) < 9.0f)
                {
                    me->CastSpell(player, SPELL_TAIL_SWEEP, true);
                }
            }
        });
    }

    void UpdateFlood()
    {
        if (!_flood)
        {
            return;
        }
        float deep = FLOOD_LEVELS[std::min<uint8>(_flood, 2) - 1];
        for (Player* player : Players())
        {
            if (player->GetPositionZ() < deep)
            {
                for (uint32 spell : { SPELL_DEEP_WATER_SLOW, SPELL_DEEP_WATER_HEAL })
                {
                    if (Aura* aura = me->AddAura(spell, player))
                    {
                        aura->SetMaxDuration(DEEP_WATER_MS);
                        aura->SetDuration(DEEP_WATER_MS);
                    }
                }
            }
        }
    }

    void Ambush()
    {
        if (_submerged || _roll)
        {
            return;
        }
        Player* furthest = nullptr;
        for (Player* player : Players())
        {
            if (!furthest || player->GetExactDist2d(me) > furthest->GetExactDist2d(me))
            {
                furthest = player;
            }
        }
        if (!furthest)
        {
            return;
        }
        me->TextEmote("Sebekhti sinks beneath the water...", nullptr, true);
        me->AttackStop();
        Sink();
        ObjectGuid guid = furthest->GetGUID();
        scheduler.Schedule(3s, [this, guid](TaskContext)
        {
            Player* target = ObjectAccessor::GetPlayer(*me, guid);
            if (target && target->IsAlive())
            {
                me->NearTeleportTo(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), me->GetOrientation());
            }
            Surface();
            if (target && target->IsAlive())
            {
                target->KnockbackFrom(target->GetPositionX() + 0.5f, target->GetPositionY(), 2.0f, 22.0f);
                Unit::DealDamage(me, target, target->CountPctFromMaxHealth(AMBUSH_PCT));
                AttackStart(target);
            }
        });
    }

    ObjectGuid _roll;
    uint32 _rollDamage = 0;
    int32 _rollTime = 0;
    uint8 _flood = 0;
    bool _submerged = false;
};

void AddSC_flooded_nave()
{
    RegisterDrownedSunGameObjectAI(go_sluice_wheel);
    RegisterDrownedSunCreatureAI(boss_sebekhti);
}
