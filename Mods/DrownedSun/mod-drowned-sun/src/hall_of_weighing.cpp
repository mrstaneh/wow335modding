// The Hall of Weighing: the Scales' weights and pans, and Anpuet the Embalmer among his canopic jars. Stand-in models:
// an Anubisath for Anpuet, Maexxna's web wrap for the linen, jars with an unseen spirit for the canopic jars.
//
// Anpuet wakes when the Scales are balanced, then:
// - Linen Wrap: every 25 seconds wraps a random player (not the tank), who can't act until the others destroy the
//   wrapping; after 15 seconds they suffocate.
// - Canopic Jars: every 20 seconds one of four jars wakes and strengthens him until destroyed: Imsety heals him, Hapi
//   speeds his attacks, Duamutef halves the damage he takes, Qebehsenuef burns everyone a little every 2 seconds.
// - Natron: every 15 seconds he flings embalming salt under two players; standing in it hurts and gives a Sin.
// - Judgement, at 50% and 15%: everyone with more than JUDGEMENT_SINS Sins is stunned and takes heavy damage; everyone
//   else is cleansed of them.
// His health is set for the number of players when the fight starts.
#include "Chat.h"
#include "CreatureScript.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameObjectScript.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "StringFormat.h"
#include "drowned_sun.h"

namespace
{
enum Spells
{
    SPELL_WRAPPED = 20170, // Stun
    SPELL_SIN = 25646,     // Mortal Wound: stacks, each a little less healing
};

uint32 const HEALTH_PER_PLAYER = 300000;
int32 const WRAP_MS = 15000;
int32 const SUFFOCATE_PCT = 50;
int32 const NATRON_PCT = 3;
float const NATRON_REACH = 4.0f;
uint32 const JUDGEMENT_SINS = 5;
int32 const JUDGEMENT_PCT = 40;
int32 const ROT_PCT = 1;

// The jars, their spots along the hall's walls (between the coffins and clear of the way in), and what they do.
struct Jar
{
    char const* name;
    char const* wakes;
    Position at;
};
Jar const JARS[] = {
    { "Jar of Imsety", "The Jar of Imsety wakes: Anpuet's wounds close while it stands!", { 28.0f, 200.0f, 20.0f, 3.14f } },
    { "Jar of Hapi", "The Jar of Hapi wakes: Anpuet strikes faster while it stands!", { -14.0f, 224.2f, 20.0f, 5.24f } },
    { "Jar of Duamutef", "The Jar of Duamutef wakes: a shield turns half of every blow while it stands!", { -26.3f, 190.4f, 20.0f, 0.35f } },
    { "Jar of Qebehsenuef", "The Jar of Qebehsenuef wakes: its breath burns you all while it stands!", { 21.4f, 182.0f, 20.0f, 2.44f } },
};
enum JarIndex
{
    JAR_HEAL,
    JAR_HASTE,
    JAR_SHIELD,
    JAR_ROT,
};

void Hurt(Unit* source, Unit* target, int32 pct)
{
    Unit::DealDamage(source, target, target->CountPctFromMaxHealth(pct), nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW);
}

void AddSin(Unit* source, Unit* target)
{
    source->AddAura(SPELL_SIN, target);
}

uint32 Sins(Unit* target)
{
    Aura* sin = target->GetAura(SPELL_SIN);
    return sin ? sin->GetStackAmount() : 0;
}
} // namespace

// The Scales' piles and pans: using one tells the instance who picked up or put down what.
struct go_weighing : public GameObjectAI
{
    go_weighing(GameObject* go) : GameObjectAI(go), _instance(go->GetInstanceScript()) {}

    bool GossipHello(Player* player, bool reportUse) override
    {
        if (reportUse) // the same click, reported again
        {
            return true;
        }
        if (_instance)
        {
            switch (me->GetEntry())
            {
                case GO_STONE_WEIGHTS: _instance->SetGuidData(DATA_PICK_STONE, player->GetGUID()); break;
                case GO_FEATHERS: _instance->SetGuidData(DATA_PICK_FEATHER, player->GetGUID()); break;
                case GO_LEFT_SCALE: _instance->SetGuidData(DATA_PLACE_LEFT, player->GetGUID()); break;
                case GO_RIGHT_SCALE: _instance->SetGuidData(DATA_PLACE_RIGHT, player->GetGUID()); break;
            }
        }
        return true;
    }

private:
    InstanceScript* _instance;
};

// The linen around a wrapped player: destroy it to free them; left alone, they suffocate.
struct npc_linen_wrap : public ScriptedAI
{
    npc_linen_wrap(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        _held = 0;
    }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        _player = guid;
    }

    void JustDied(Unit* /*killer*/) override
    {
        Free();
    }

    void UpdateAI(uint32 diff) override
    {
        _held += diff;
        if (_held < WRAP_MS)
        {
            return;
        }
        if (Player* player = ObjectAccessor::GetPlayer(*me, _player))
        {
            Hurt(me, player, SUFFOCATE_PCT);
        }
        Free();
        me->DespawnOrUnsummon();
    }

private:
    void Free()
    {
        if (Player* player = ObjectAccessor::GetPlayer(*me, _player))
        {
            player->RemoveAurasDueToSpell(SPELL_WRAPPED);
        }
        _player.Clear();
    }

    ObjectGuid _player;
    int32 _held = 0;
};

// Natron on the floor: hurts and gives a Sin every second to whoever stands in it, for 20 seconds.
struct npc_natron : public ScriptedAI
{
    npc_natron(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);
        _timer = 0;
        _life = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        _life += diff;
        if (_life >= 20000)
        {
            me->DespawnOrUnsummon();
            return;
        }
        _timer += diff;
        if (_timer < 1000)
        {
            return;
        }
        _timer = 0;
        for (Player* player : PlayersNear(me->GetMap(), *me, NATRON_REACH))
        {
            Hurt(me, player, NATRON_PCT);
            AddSin(me, player);
        }
    }

private:
    uint32 _timer = 0;
    uint32 _life = 0;
};

struct boss_anpuet : public BossAI
{
    boss_anpuet(Creature* creature) : BossAI(creature, DATA_ANPUET) {}

    void Reset() override
    {
        BossAI::Reset();
        for (ObjectGuid& jar : _jars)
        {
            jar.Clear();
        }
        _awake = 0;
        me->SetAttackTime(BASE_ATTACK, ATTACK_MS);
        Show(instance->GetBossState(DATA_SCALES) == DONE);
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_EMERGE)
        {
            Show(true);
            me->Yell("The scales are true. Come, then, and be weighed.", LANG_UNIVERSAL);
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        uint32 players = std::max<uint32>(1, me->GetMap()->GetPlayersCountExceptGMs());
        me->SetMaxHealth(HEALTH_PER_PLAYER * players);
        me->SetFullHealth();
        me->Yell("Every heart is heavy with something.", LANG_UNIVERSAL);
        for (uint32 i = 0; i < std::size(JARS); ++i)
        {
            if (Creature* jar = me->SummonCreature(NPC_CANOPIC_JAR, JARS[i].at))
            {
                jar->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
                jar->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                _jars[i] = jar->GetGUID();
            }
        }

        scheduler.Schedule(25s, [this](TaskContext context)
        {
            Wrap();
            context.Repeat(25s);
        }).Schedule(15s, [this](TaskContext context)
        {
            Natron();
            context.Repeat(15s);
        }).Schedule(20s, [this](TaskContext context)
        {
            WakeJar();
            if (_awake < std::size(JARS))
            {
                context.Repeat(20s);
            }
        }).Schedule(1s, [this](TaskContext context)
        {
            JarEffects();
            context.Repeat(1s);
        });

        ScheduleHealthCheckEvent({ 50, 15 }, [this] { Judgement(); });
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType type, SpellSchoolMask school) override
    {
        if (JarStands(JAR_SHIELD))
        {
            damage /= 2;
        }
        BossAI::DamageTaken(attacker, damage, type, school);
    }

    void JustDied(Unit* killer) override
    {
        BossAI::JustDied(killer);
        me->Yell("My own heart... was the heaviest...", LANG_UNIVERSAL);
        me->SummonGameObject(GO_ANPUET_HOARD, HALL_CENTER.GetPositionX(), HALL_CENTER.GetPositionY(), HALL_CENTER.GetPositionZ(), 0, 0, 0, 0, 0, WEEK);
    }

private:
    static uint32 const ATTACK_MS = 2000;

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
        return PlayersNear(me->GetMap(), HALL_CENTER, HALL_RADIUS + 8);
    }

    // Whether a jar has woken and not yet been destroyed.
    bool JarStands(uint32 index)
    {
        if (index >= _awake)
        {
            return false;
        }
        Creature* jar = ObjectAccessor::GetCreature(*me, _jars[index]);
        return jar && jar->IsAlive();
    }

    void WakeJar()
    {
        if (_awake >= std::size(JARS))
        {
            return;
        }
        if (Creature* jar = ObjectAccessor::GetCreature(*me, _jars[_awake]))
        {
            jar->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
            jar->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            me->TextEmote(JARS[_awake].wakes, nullptr, true);
        }
        ++_awake;
    }

    void JarEffects()
    {
        if (JarStands(JAR_HEAL))
        {
            me->ModifyHealth(int32(me->CountPctFromMaxHealth(1) / 2));
        }
        me->SetAttackTime(BASE_ATTACK, JarStands(JAR_HASTE) ? ATTACK_MS * 6 / 10 : ATTACK_MS);
        if (JarStands(JAR_ROT) && (++_rotTicks % 2) == 0)
        {
            for (Player* player : Players())
            {
                Hurt(me, player, ROT_PCT);
            }
        }
    }

    void Wrap()
    {
        Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true, false);
        if (!target)
        {
            return;
        }
        if (Aura* stun = me->AddAura(SPELL_WRAPPED, target))
        {
            stun->SetMaxDuration(WRAP_MS + 2000);
            stun->SetDuration(WRAP_MS + 2000);
        }
        if (Creature* linen = me->SummonCreature(NPC_LINEN_WRAP, *target, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 3000))
        {
            linen->AI()->SetGUID(target->GetGUID());
        }
        me->TextEmote(Acore::StringFormat("Anpuet wraps {} in linen! Cut them free before they suffocate!", target->GetName()), nullptr, true);
    }

    void Natron()
    {
        std::vector<Player*> players = Players();
        std::sort(players.begin(), players.end(), [this](Player* a, Player* b) { return a->GetExactDist2d(me) > b->GetExactDist2d(me); });
        for (uint32 i = 0; i < players.size() && i < 2; ++i)
        {
            me->SummonCreature(NPC_NATRON, *players[i]);
        }
        if (!players.empty())
        {
            me->TextEmote("Anpuet flings embalming salt. Don't stand in it: the scales remember.", nullptr, true);
        }
    }

    void Judgement()
    {
        me->Yell("Hearts on the scale!", LANG_UNIVERSAL);
        for (Player* player : Players())
        {
            if (Sins(player) > JUDGEMENT_SINS)
            {
                if (Aura* stun = me->AddAura(SPELL_WRAPPED, player))
                {
                    stun->SetMaxDuration(5000);
                    stun->SetDuration(5000);
                }
                Hurt(me, player, JUDGEMENT_PCT);
                ChatHandler(player->GetSession()).SendNotification("Your heart is heavy with sin.");
            }
            else
            {
                player->RemoveAurasDueToSpell(SPELL_SIN);
                ChatHandler(player->GetSession()).SendNotification("Your heart is light.");
            }
        }
    }

    ObjectGuid _jars[std::size(JARS)];
    uint32 _awake = 0;
    uint32 _rotTicks = 0;
};

void AddSC_hall_of_weighing()
{
    RegisterDrownedSunGameObjectAI(go_weighing);
    RegisterDrownedSunCreatureAI(npc_linen_wrap);
    RegisterDrownedSunCreatureAI(npc_natron);
    RegisterDrownedSunCreatureAI(boss_anpuet);
}
