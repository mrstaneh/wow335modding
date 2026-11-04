# Sanctum of the Drowned Sun — design

An original raid for this client and the AzerothCore server: a sun-cult's temple, swallowed by sand and flood in the
deep desert, where a priest tried to become a god by binding the four animal gods of his people. It has four wings
built on Egyptian mythology (crocodile, scarab, jackal, serpent), played in any order, then a finale. Difficulty is
opt-in: before entering, the party breaks **Seals**, and each one makes the raid harder and the rewards better.

Everything here — names, story, encounters, mechanics — is our own. The animal gods come from real mythology; the
encounters are designed from scratch.

## Story

Long ago the sun-priests of Ka-Meret kept four gods in balance: **Sebekhti** of the flood, **Khepret** who rolls
the sun each dawn, **Anpuet** who weighs the hearts of the dead, and **Apepheth**, the coiled dark the sun must
pass through each night. High Sun-Priest **Merenkhaal** wanted the sun for himself. He chained each god in a wing
of his sanctum and drew their power through four conduits into the heart of the temple. The working broke: the
river burst its banks, the sand swallowed the city, and the sun over Ka-Meret has stood still at a red, drowned dusk
ever since.

The party enters through the ruined Sun Court. Freeing a god (defeating its corrupted form) breaks one conduit.
With all four broken, Merenkhaal — now **the Unmade**, neither priest nor god — can be reached in the Sanctum below.

## Layout

One instance map, built as terrain plus placed buildings (MapGen), sunk in a canyon:

```
                 [ Eclipse Pit ]            serpent wing (north)
                        |
  [ Hall of  ] —— [ SUN COURT ] —— [ Flooded  ]
  [ Weighing ]    (entrance,       [   Nave   ]   crocodile wing (east)
  jackal (west)    Seal altar)
                        |
                 [ Dawn Gallery ]           scarab wing (south)

         Sanctum of the Unmade: beneath the Sun Court, reached by a stair that opens
         when the fourth conduit breaks
```

- **Sun Court** — open-air courtyard, ruined statues of the four gods, a dead-sun disc above, the Seal altar, the
  entrance portal and the loot chamber. Each wing gate glows its god's colour while its conduit holds.
- Each wing is a **trial room** (a short challenge without a boss) followed by a **boss arena**.
- Art in the first version is all Blizzard's own WotLK data: Tanaris ruins, Ahn'Qiraj columns, walls and gates,
  desert terrain and liquids. Custom models come later (see Milestones).

## Group size and tuning

Built for a group of friends: **1 to 5 players**, with boss health and damage scaled to the group size at the start
of each encounter. The base raid (no Seals) should be beatable by a level-80 group of two in normal quest gear;
high Seal levels are meant to wipe careless groups.

## The four wings

### Flooded Nave — crocodile (Sebekhti, the River's Jaw)

*Trial — the Sluices.* The nave is half flooded and the water rises every 30 seconds. Three sluice wheels around the
room drain it, but each must be turned by a player standing on it while crocolisk spawn come for them. Get all three
turned before the water reaches the ledge, or everyone in the water takes growing damage.

*Boss — Sebekhti.* A huge crocodile god fought in a shallow basin.
- **Death Roll** — grabs a random player and drags them under; the others must deal a set amount of damage to the
  boss to make it let go, or the held player drowns.
- **Undertow** — pulls everyone towards the boss for a few seconds; move against it or be in melee range for the
  next attack.
- **Rising Flood** — at 70% and 35% health the basin floods one level higher. Deep water slows players and halves
  their healing; raised stone islands stay dry but shrink each flood.
- **Ambush** — Sebekhti submerges and reappears under the player furthest from the group, knocking them up.

### Dawn Gallery — scarab (Khepret, Who Rolls the Sun)

*Trial — the Long Ramp.* A climb up a sloped gallery while burning sun-orbs roll down it in lanes. Lanes are
announced by a glow on the top step a few seconds ahead. Players hit by an orb are thrown back down the ramp.

*Boss — Khepret.* A giant scarab pushing a sun-orb around a round arena.
- **The Sun's Path** — the orb leaves a burning trail that lasts until the next dawn; the arena fills up over time.
- **Dawn and Dusk** — the fight cycles every 45 seconds. At *dawn* the orb blazes and pulses fire to everyone
  (stand close together to share it). At *dusk* the light dies, the trails go out, and small scarabs pour from the
  walls; they explode on players they reach.
- **Roll** — Khepret charges the orb in a straight line across the arena; anyone in the path is knocked away and
  set burning.
- Using the dusk to clear trails and the dawn to burn the boss is the core of the fight.

### Hall of Weighing — jackal (Anpuet the Embalmer)

*Trial — the Scales.* Two great scales and a pile of stone weights and feathers. Each player carries one item at a
time. Both scales must be balanced exactly within two minutes while wrapped mummies wander the hall; a mummy that
touches a player knocks their item out of their hands.

*Boss — Anpuet.* A jackal-headed embalmer among canopic jars.
- **Linen Wrap** — wraps a player, stunning them; anyone can cut them free by using the wrapping. Unfreed players
  start to suffocate.
- **Canopic Jars** — four jars along the walls wake one at a time. Each powers up the boss (faster attacks, a
  shield, healing, a damage aura) until destroyed.
- **Sins** — standing in harmful effects during the fight gives a player stacks of *Sin*.
- **Judgement** — at 50% and 15% health, Anpuet weighs every heart: players with more than five Sin stacks are
  stunned and take heavy damage, everyone else is cleansed of Sin. Clean play is rewarded directly.

### Eclipse Pit — serpent (Apepheth, the Unlit Coil)

*Trial — the Lightless Way.* A dark winding passage. Braziers along the way can be lit by carrying a torch from the
previous one; players out of light for more than a few seconds gain stacking *Unlit* damage. The serpent's coils
slide through the walls and put out braziers as they pass.

*Boss — Apepheth.* A colossal serpent coiled around a pit, most of its body below the floor.
- **Eclipse** — the light dims in stages. Sunbeams fall through holes in the ceiling and move slowly across the
  floor; outside a beam, players take growing shadow damage.
- **Coil Slam** — body segments burst up through the floor in a line, marked a moment before.
- **Swallow** — at intervals Apepheth swallows a player. Inside, the swallowed player must kill a heart-shaped
  growth quickly to be spat out; the rest of the group can speed this up by hitting the boss's head.
- **Total Eclipse** — at 25% the beams go out for good; one last sunbeam must be carried by the player holding the
  torch from the trial, so the fight ends in a tight huddle around them.

## Finale — Merenkhaal the Unmade

Reached when all four conduits are broken. A round sanctum around a sun-disc; the four broken conduits still hang
on its walls, one per god.

- **Phase 1 — the Priest.** Merenkhaal fights with stolen power: every 30 seconds he channels an **Aspect** of one
  of the four gods, taking a signature move from its wing (a Death Roll grab, a sun-orb Roll, a Linen Wrap, a Coil
  Slam), always in the order the party cleared the wings. While channelling, the matching conduit can be struck by
  a player to interrupt him.
- **Phase 2 — the Breaking** (at 60%). The sun-disc cracks; Merenkhaal becomes untargetable and floats above it,
  pulling all four Aspects at once in weaker form. The group must break the four conduits for good, one per corner,
  while the arena shifts between flood, burning trails, judgement and darkness.
- **Phase 3 — the Unmade** (when the conduits fall). He tries to become the sun. The arena heats up every second;
  only a burn race remains. Players can drag him into the shadow of the broken disc to slow the heat, at the cost
  of the disc's protection from his pulses.

Defeating him frees the sun: the instance's sky changes from drowned dusk to clear day (a light change for the map),
and the loot chamber in the Sun Court opens.

## Seals

At the Seal altar in the Sun Court, before the first boss is engaged, the party can break any number of Seals. Each
has a level value; the total sets the raid's **Seal level**, which raises boss health and damage a little on its
own and decides the rewards. Seals cannot be changed after the first boss is pulled.

| Seal | Level | Effect |
|---|---|---|
| Seal of Haste | 2 | Bosses attack and cast 15% faster |
| Seal of the Famine | 3 | Healing received reduced by 25% |
| Seal of the Hourglass | 3 | Every encounter has an enrage timer |
| Seal of the Swarm | 2 | Trial and boss adds come in larger numbers |
| Seal of Deep Water | 1 | Sebekhti floods one level higher from the start |
| Seal of the Long Day | 1 | Khepret's dawns last longer, dusks shorter |
| Seal of Heavy Hearts | 1 | Judgement punishes at three Sin stacks instead of five |
| Seal of the Black Sun | 1 | Apepheth's beams are narrower |
| Seal of the Unmade | 4 | Merenkhaal channels two Aspects at once in phase 1 |

## Rewards

- A chest in the loot chamber after the finale, with items scaled to the Seal level (more and better rolls).
- A small chest after each wing boss.
- Item sets themed on the four gods (river, sun, embalmer, eclipse), using existing WotLK item models in the first
  version and our own later.
- A title for clearing at Seal level 10 or higher.

## How it gets built

**Client side (this repo):**
- MapGen grows a second map (id 801, an instance of type raid in `Map.dbc`): desert canyon terrain, water for the
  Flooded Nave, Tanaris and Ahn'Qiraj buildings placed per room, subzone names per wing (`AreaTable.dbc`) and a
  light for the drowned dusk.
- Seals at first through the altar's gossip menu; later their own window in the interface mod.
- Custom spells where existing ones don't fit, as `Spell.dbc` rows in the patch MPQ.

**Server side (AzerothCore):**
- A module `mod-drowned-sun` (C++): an instance script tracking the conduits and Seal level, one boss script per
  encounter, trial-room scripts, the Seal altar.
- SQL in `Mods/sql/`: creature and game-object templates and spawns, the instance entry, loot tables, items.
- The map's terrain and collision for the server via `Mods/mods.sh server`, as for the test map.

## Milestones

1. **The place** — map 801 with the Sun Court and four empty wings, an entrance portal from the test map, collision
   on the server. Walkable end to end.
2. **First wing complete** — Flooded Nave trial and Sebekhti with stand-in models and spells, the instance script,
   the module and a wing chest. This one sets the patterns the rest follow.
3. **The other three wings** — Dawn Gallery, Hall of Weighing, Eclipse Pit.
4. **The finale** — Merenkhaal, the day/dusk sky change, the loot chamber.
5. **Seals** — the altar, scaling, rewards by Seal level.
6. **Art pass** — our own models and textures for the four gods and Merenkhaal (Blender, exported to the client's
   M2 format), sounds, a loading screen.

Each milestone ends playable in the browser client; tuning numbers above are starting points to adjust in play.
