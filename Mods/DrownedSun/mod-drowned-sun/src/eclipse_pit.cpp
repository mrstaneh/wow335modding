// The Eclipse Pit: the Lightless Way's braziers and Apepheth, the Unlit Coil, in his pit. Stand-in models: Hakkar's
// serpent for Apepheth, Freya's sun beams for the sunbeams, a dust cloud marking a coil, the Shadowforge brazier.
//
// Apepheth wakes when every brazier on the Lightless Way burns at once, then:
// - Eclipse: sunbeams wander the pit. Out of every beam, players turn Unlit (see Darken in drowned_sun.h). At 75% and
//   50% a beam goes out.
// - Coil Slam: every 15 seconds the ground is marked in a line from him through a player; a moment later his coils
//   burst up along it, hurting and throwing whoever stands there.
// - Swallow: every 40 seconds he swallows a player (not the tank) into his belly, where a heart beats. They must kill it
//   to be spat out, and every blow the others land on him wounds it too; after 20 seconds they are digested, half
//   dead, and spat out anyway.
// - Total Eclipse, at 25%: the beams go out for good but one, which follows whoever last lit a brazier (or anyone):
//   the fight ends in a huddle around them.
// His health is set for the number of players when the fight starts.
#include "CreatureScript.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameObjectScript.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "StringFormat.h"
#include "drowned_sun.h"

namespace
{
uint32 const HEALTH_PER_PLAYER = 300000;
float const BEAM_REACH = 6.0f;
uint32 const BEAMS = 4;
float const BEAM_SPEED = 2.5f;
float const COIL_STEP = 6.0f;
uint32 const COIL_MARKS = 7;
float const COIL_REACH = 4.0f;
int32 const COIL_PCT = 18;
int32 const DIGEST_MS = 20000;
int32 const DIGEST_PCT = 40;
uint32 const HEART_HEALTH = 60000;

enum Points
{
    POINT_WANDER = 1,
};

// A random spot in the pit, away from its walls.
Position RandomInPit()
{
    float angle = frand(0.0f, 2 * float(M_PI)), r = frand(0.0f, PIT_RADIUS - 8);
    return { PIT_CENTER.GetPositionX() + r * std::cos(angle), PIT_CENTER.GetPositionY() + r * std::sin(angle), PIT_CENTER.GetPositionZ(), 0.0f };
}
} // namespace

// A brazier on the Lightless Way: using it tells the instance who used which.
struct go_lightless_brazier : public GameObjectAI
{
    go_lightless_brazier(GameObject* go) : GameObjectAI(go), _instance(go->GetInstanceScript()) {}

    bool GossipHello(Player* player, bool reportUse) override
    {
        if (reportUse) // the same click, reported again
        {
            return true;
        }
        if (!_instance)
        {
            return true;
        }
        uint32 index = 0;
        for (uint32 i = 1; i < std::size(LIGHTLESS_BRAZIERS); ++i)
        {
            if (me->GetExactDist2d(&LIGHTLESS_BRAZIERS[i]) < me->GetExactDist2d(&LIGHTLESS_BRAZIERS[index]))
            {
                index = i;
            }
        }
        _instance->SetGuidData(DATA_BRAZIER_USER, player->GetGUID());
        _instance->SetData(DATA_BRAZIER_USED, index);
        return true;
    }

private:
    InstanceScript* _instance;
};

// A sunbeam: wanders the pit slowly, or follows one player once the eclipse is total.
struct npc_sunbeam : public ScriptedAI
{
    npc_sunbeam(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        Wander();
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE && id == POINT_WANDER)
        {
            Wander();
        }
    }

    void UpdateAI(uint32 /*diff*/) override {}

private:
    void Wander()
    {
        Position to = RandomInPit();
        me->GetMotionMaster()->MovePoint(POINT_WANDER, to.GetPositionX(), to.GetPositionY(), to.GetPositionZ(), FORCED_MOVEMENT_NONE, BEAM_SPEED);
    }
};

struct boss_apepheth : public BossAI
{
    boss_apepheth(Creature* creature) : BossAI(creature, DATA_APEPHETH) {}

    void Reset() override
    {
        BossAI::Reset();
        _swallowed.Clear();
        _heart.Clear();
        _beams.clear();
        Show(instance->GetBossState(DATA_LIGHTLESS_WAY) == DONE);
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_EMERGE)
        {
            Show(true);
            me->Yell("Light, in my pit? I swallowed the sun once. I will again.", LANG_UNIVERSAL);
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        uint32 players = std::max<uint32>(1, me->GetMap()->GetPlayersCountExceptGMs());
        me->SetMaxHealth(HEALTH_PER_PLAYER * players);
        me->SetFullHealth();
        me->SetControlled(true, UNIT_STATE_ROOT); // coiled around his pit
        me->Yell("Into the dark with you.", LANG_UNIVERSAL);
        for (uint32 i = 0; i < BEAMS; ++i)
        {
            if (Creature* beam = me->SummonCreature(NPC_SUNBEAM, RandomInPit()))
            {
                _beams.push_back(beam->GetGUID());
            }
        }
        me->TextEmote("The eclipse begins. Keep to the sunbeams!", nullptr, true);

        scheduler.Schedule(1s, [this](TaskContext context)
        {
            Shade();
            Digest();
            context.Repeat(1s);
        }).Schedule(15s, [this](TaskContext context)
        {
            MarkCoils();
            context.Repeat(15s);
        }).Schedule(30s, [this](TaskContext context)
        {
            Swallow();
            context.Repeat(40s);
        });

        ScheduleHealthCheckEvent({ 75, 50 }, [this]
        {
            if (_beams.size() > 1)
            {
                if (Creature* beam = ObjectAccessor::GetCreature(*me, _beams.back()))
                {
                    beam->DespawnOrUnsummon();
                }
                _beams.pop_back();
                me->TextEmote("The eclipse deepens: a sunbeam goes out.", nullptr, true);
            }
        });
        ScheduleHealthCheckEvent(25, [this] { TotalEclipse(); });
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType type, SpellSchoolMask school) override
    {
        BossAI::DamageTaken(attacker, damage, type, school);
        // Blows on the head reach the heart inside.
        if (Creature* heart = _heart ? ObjectAccessor::GetCreature(*me, _heart) : nullptr)
        {
            if (heart->IsAlive() && attacker != heart)
            {
                Unit::DealDamage(attacker, heart, std::min<uint32>(damage / 3, heart->GetHealth()));
            }
        }
    }

    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() == NPC_COIL_HEART)
        {
            me->TextEmote("Apepheth retches. The swallowed are spat out!", nullptr, true);
            SpitOut();
        }
    }

    void JustDied(Unit* killer) override
    {
        SpitOut();
        BossAI::JustDied(killer);
        me->Yell("The sun... passes... through...", LANG_UNIVERSAL);
        me->SummonGameObject(GO_APEPHETH_HOARD, PIT_CENTER.GetPositionX() - 15, PIT_CENTER.GetPositionY(), PIT_CENTER.GetPositionZ(), 0, 0, 0, 0, 0, WEEK);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        SpitOut();
        me->SetControlled(false, UNIT_STATE_ROOT);
        BossAI::EnterEvadeMode(why);
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
        return PlayersNear(me->GetMap(), PIT_CENTER, PIT_RADIUS + 8);
    }

    // Everyone in the pit outside every beam turns a little more Unlit; inside one, the dark leaves them.
    void Shade()
    {
        for (Player* player : Players())
        {
            bool lit = false;
            for (ObjectGuid guid : _beams)
            {
                Creature* beam = ObjectAccessor::GetCreature(*me, guid);
                if (beam && beam->GetExactDist2d(player) < BEAM_REACH)
                {
                    lit = true;
                    break;
                }
            }
            lit ? Lighten(player) : Darken(player);
        }
    }

    void MarkCoils()
    {
        Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true);
        if (!target)
        {
            return;
        }
        float angle = me->GetAngle(target);
        std::vector<ObjectGuid> marks;
        for (uint32 i = 1; i <= COIL_MARKS; ++i)
        {
            float x = me->GetPositionX() + std::cos(angle) * COIL_STEP * i, y = me->GetPositionY() + std::sin(angle) * COIL_STEP * i;
            if (Creature* mark = me->SummonCreature(NPC_COIL_MARK, x, y, PIT_CENTER.GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 4000))
            {
                marks.push_back(mark->GetGUID());
            }
        }
        me->TextEmote("The ground heaves: Apepheth's coils stir beneath it!", nullptr, true);
        scheduler.Schedule(2500ms, [this, marks](TaskContext)
        {
            std::vector<ObjectGuid> hit;
            for (ObjectGuid guid : marks)
            {
                Creature* mark = ObjectAccessor::GetCreature(*me, guid);
                if (!mark)
                {
                    continue;
                }
                for (Player* player : PlayersNear(me->GetMap(), *mark, COIL_REACH))
                {
                    if (std::find(hit.begin(), hit.end(), player->GetGUID()) == hit.end())
                    {
                        hit.push_back(player->GetGUID());
                        player->KnockbackFrom(mark->GetPositionX(), mark->GetPositionY(), 6.0f, 18.0f);
                        Unit::DealDamage(me, player, player->CountPctFromMaxHealth(COIL_PCT), nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW);
                    }
                }
            }
        });
    }

    void Swallow()
    {
        if (_swallowed)
        {
            return;
        }
        Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true, false);
        Player* player = target ? target->ToPlayer() : nullptr;
        if (!player)
        {
            return;
        }
        me->TextEmote(Acore::StringFormat("Apepheth swallows {} whole! Strike his head to wound the heart inside!", player->GetName()), nullptr, true);
        _swallowed = player->GetGUID();
        _digest = 0;
        player->NearTeleportTo(BELLY.GetPositionX(), BELLY.GetPositionY(), BELLY.GetPositionZ() + 1.0f, 0.0f);
        if (Creature* heart = me->SummonCreature(NPC_COIL_HEART, BELLY.GetPositionX() + 4, BELLY.GetPositionY(), BELLY.GetPositionZ() + 1.0f, M_PI))
        {
            heart->SetMaxHealth(HEART_HEALTH);
            heart->SetFullHealth();
            _heart = heart->GetGUID();
        }
    }

    void Digest()
    {
        if (!_swallowed)
        {
            return;
        }
        _digest += 1000;
        if (_digest < DIGEST_MS)
        {
            return;
        }
        if (Player* player = ObjectAccessor::GetPlayer(*me, _swallowed))
        {
            Unit::DealDamage(me, player, player->CountPctFromMaxHealth(DIGEST_PCT), nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NATURE);
        }
        me->TextEmote("Apepheth spits out what he could not digest.", nullptr, true);
        SpitOut();
    }

    // Whoever is in the belly lands in the pit beside him, and the heart is gone.
    void SpitOut()
    {
        if (Creature* heart = _heart ? ObjectAccessor::GetCreature(*me, _heart) : nullptr)
        {
            heart->DespawnOrUnsummon();
        }
        _heart.Clear();
        if (Player* player = _swallowed ? ObjectAccessor::GetPlayer(*me, _swallowed) : nullptr)
        {
            float angle = frand(0.0f, 2 * float(M_PI));
            player->NearTeleportTo(me->GetPositionX() + 10 * std::cos(angle), me->GetPositionY() + 10 * std::sin(angle), PIT_CENTER.GetPositionZ() + 1.0f, angle + M_PI);
        }
        _swallowed.Clear();
    }

    void TotalEclipse()
    {
        for (ObjectGuid guid : _beams)
        {
            if (Creature* beam = ObjectAccessor::GetCreature(*me, guid))
            {
                beam->DespawnOrUnsummon();
            }
        }
        _beams.clear();
        Player* bearer = ObjectAccessor::GetPlayer(*me, instance->GetGuidData(DATA_TORCH_BEARER));
        if (!bearer || !bearer->IsAlive() || bearer->GetExactDist2d(&PIT_CENTER) > PIT_RADIUS + 8)
        {
            std::vector<Player*> players = Players();
            bearer = players.empty() ? nullptr : players[urand(0, players.size() - 1)];
        }
        if (!bearer)
        {
            return;
        }
        if (Creature* beam = me->SummonCreature(NPC_SUNBEAM, *bearer))
        {
            beam->GetMotionMaster()->MoveFollow(bearer, 0.0f, 0.0f);
            _beams.push_back(beam->GetGUID());
        }
        me->TextEmote(Acore::StringFormat("Total eclipse! The last of the light clings to {}'s torch. Stay with them!", bearer->GetName()), nullptr, true);
    }

    std::vector<ObjectGuid> _beams;
    ObjectGuid _swallowed;
    ObjectGuid _heart;
    int32 _digest = 0;
};

void AddSC_eclipse_pit()
{
    RegisterDrownedSunGameObjectAI(go_lightless_brazier);
    RegisterDrownedSunCreatureAI(npc_sunbeam);
    RegisterDrownedSunCreatureAI(boss_apepheth);
}
