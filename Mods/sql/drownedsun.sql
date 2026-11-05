-- Sanctum of the Drowned Sun (map 801, Mods/DrownedSun/DESIGN.md, terrain in Tools/MpqTool/mapgen_drownedsun.cpp):
-- the instance, a portal in from the test map's clearing and one back out from the Sun Court. Custom ids: game objects
-- 500000+, creatures 500100+, spawns 5800000+. The scripts are in Mods/DrownedSun/mod-drowned-sun.

DELETE FROM `instance_template` WHERE `map` = 801;
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES (801, 800, 'instance_drowned_sun', 1); -- mounts: the Sun Court only (instance script)

-- The portals: goobers that send whoever uses them through (SmartAI: on use, teleport the user). They wear the Dalaran
-- portal model (4394); the classic instance portal (9527) is all particles and draws nothing in this client.
DELETE FROM `gameobject_template` WHERE `entry` IN (500000, 500001);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `AIName`) VALUES
(500000, 10, 4394, 'Sanctum of the Drowned Sun', 1, 'SmartGameObjectAI'),
(500001, 10, 4394, 'Leave the Sanctum', 1, 'SmartGameObjectAI');

DELETE FROM `smart_scripts` WHERE `entryorguid` IN (500000, 500001) AND `source_type` = 1;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `action_type`, `action_param1`,
                             `target_type`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
-- event 64 gossip hello (using it), action 62 teleport to map param1, target 7 the user, to target x/y/z/o
(500000, 1, 0, 0, 64, 62, 801, 7, -38, 0, 20.5, 0, 'Sanctum of the Drowned Sun - On use - Teleport into the Sun Court'),
(500001, 1, 0, 0, 64, 62, 800, 7, -21, -21, 8, 0.785, 'Leave the Sanctum - On use - Teleport to the test map clearing');

DELETE FROM `gameobject` WHERE `guid` IN (5800000, 5800001);
INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                          `orientation`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`) VALUES
(5800000, 500000, 800, 1, 1, -25, -25, 7.5, 0.785, 0.3827, 0.9239, 0, 100, 1),
(5800001, 500001, 801, 1, 1, -46, 0, 20, 0, 0, 1, 0, 100, 1);

-- .tele DrownedSun (GMs): the Sun Court
DELETE FROM `game_tele` WHERE `id` = 9801;
INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
(9801, -38, 0, 20.5, 0, 801, 'DrownedSun');

-- The Flooded Nave (milestone 2): the Sluices' wheels, the snapjaws that come for whoever turns them, Sebekhti in his
-- basin and the hoard he leaves. Stand-in models: the Zulian Crocolisk (833) scaled up, the Saltwater Crocolisk (2548),
-- the Wheel of the Black March (5873), a large chest (259). Spawn points from MapGen's printout ("sluice wheel at").
DELETE FROM `creature_template` WHERE `entry` IN (500100, 500101);
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `speed_walk`,
                                 `speed_run`, `detection_range`, `rank`, `DamageModifier`, `BaseAttackTime`,
                                 `RangeAttackTime`, `unit_class`, `type`, `HealthModifier`, `ArmorModifier`,
                                 `RegenHealth`, `flags_extra`, `ScriptName`) VALUES
(500100, 'Sebekhti', 'The River''s Jaw', 83, 83, 2, 14, 1, 1.14286, 40, 3, 4, 2000, 2000, 1, 1, 30, 1, 1, 1, 'boss_sebekhti'),
(500101, 'Nave Snapjaw', '', 80, 80, 2, 14, 1, 1.14286, 30, 0, 1.5, 2000, 2000, 1, 1, 2, 1, 1, 0, '');

DELETE FROM `creature_template_model` WHERE `CreatureID` IN (500100, 500101);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(500100, 0, 833, 3.5, 1),
(500101, 0, 2548, 1.2, 1);

DELETE FROM `creature` WHERE `guid` = 5800100;
INSERT INTO `creature` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                        `orientation`, `spawntimesecs`) VALUES
(5800100, 500100, 801, 1, 1, 0, -205, 5, 1.5708, 604800);

-- The wheels: goobers the script turns (go_sluice_wheel); the hoard: a chest (lock 57 opens for anyone) with its loot.
DELETE FROM `gameobject_template` WHERE `entry` IN (500002, 500010);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `Data3`, `ScriptName`) VALUES
(500002, 10, 5873, 'Sluice Wheel', 3, 0, 0, 0, 'go_sluice_wheel'),
(500010, 3, 259, 'Sebekhti''s Hoard', 1.5, 57, 500010, 1, '');

DELETE FROM `gameobject` WHERE `guid` IN (5800002, 5800003, 5800004);
INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                          `orientation`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`) VALUES
(5800002, 500002, 801, 1, 1, 20.1, -100.9, 7.73, 3.7533, 0.9536, -0.3011, 0, 100, 1),
(5800003, 500002, 801, 1, 1, -22.1, -96.4, 7.55, 5.5836, 0.3427, -0.9394, 0, 100, 1),
(5800004, 500002, 801, 1, 1, -25.8, -119.6, 7.54, 0.1764, 0.0881, 0.9961, 0, 100, 1);

-- Two rolls: cloth or leather, mail or plate (item level 200 epics).
DELETE FROM `gameobject_loot_template` WHERE `Entry` = 500010;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`,
                                        `MinCount`, `MaxCount`, `Comment`) VALUES
(500010, 37172, 0, 0, 0, 1, 1, 1, 1, 'Sebekhti''s Hoard - Gloves of Glistening Runes'),
(500010, 37622, 0, 0, 0, 1, 1, 1, 1, 'Sebekhti''s Hoard - Skirt of the Old Kingdom'),
(500010, 37409, 0, 0, 0, 1, 1, 1, 1, 'Sebekhti''s Hoard - Gilt-Edged Leather Gauntlets'),
(500010, 37644, 0, 0, 0, 1, 1, 1, 1, 'Sebekhti''s Hoard - Gored Hide Legguards'),
(500010, 37623, 0, 0, 0, 1, 2, 1, 1, 'Sebekhti''s Hoard - Fiery Obelisk Handguards'),
(500010, 37669, 0, 0, 0, 1, 2, 1, 1, 'Sebekhti''s Hoard - Leggings of the Stone Halls'),
(500010, 37363, 0, 0, 0, 1, 2, 1, 1, 'Sebekhti''s Hoard - Gauntlets of Dragon Wrath'),
(500010, 37193, 0, 0, 0, 1, 2, 1, 1, 'Sebekhti''s Hoard - Staggering Legplates');

-- The Dawn Gallery (milestone 3): the Long Ramp's rolling suns, Khepret, the sun he pushes, the embers it leaves and the
-- dusk scarabs, and his hoard. Stand-in models: the Ball of Flame (26767) for every sun and ember, the Giant Scarab
-- (10031) scaled up, the Qiraji Scarab (9354). The suns and embers can't be targeted or attacked (unit_flags
-- 0x2000000 not selectable, 0x100 immune to players, 0x2 non attackable).
DELETE FROM `creature_template` WHERE `entry` BETWEEN 500102 AND 500106;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `speed_walk`,
                                 `speed_run`, `detection_range`, `rank`, `DamageModifier`, `BaseAttackTime`,
                                 `RangeAttackTime`, `unit_class`, `unit_flags`, `type`, `HealthModifier`, `ArmorModifier`,
                                 `RegenHealth`, `flags_extra`, `ScriptName`) VALUES
(500102, 'Khepret', 'Who Rolls the Sun', 83, 83, 2, 14, 1, 1.14286, 40, 3, 4, 2000, 2000, 1, 0, 1, 30, 1, 1, 1, 'boss_khepret'),
(500103, 'Khepret''s Sun', '', 83, 83, 2, 14, 1, 1.4, 0, 0, 1, 2000, 2000, 1, 33554690, 10, 1, 1, 1, 2, ''),
(500104, 'Rolling Sun', '', 80, 80, 2, 14, 1, 1, 0, 0, 1, 2000, 2000, 1, 33554690, 10, 1, 1, 1, 2, 'npc_rolling_sun'),
(500105, 'Sun''s Path', '', 80, 80, 2, 14, 1, 1, 0, 0, 1, 2000, 2000, 1, 33554690, 10, 1, 1, 1, 2, 'npc_sun_trail'),
(500106, 'Dusk Scarab', '', 80, 80, 2, 14, 1, 1.4, 30, 0, 1, 2000, 2000, 1, 0, 1, 0.3, 1, 1, 0, 'npc_dusk_scarab');

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 500102 AND 500106;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(500102, 0, 10031, 12, 1),
(500103, 0, 26767, 3, 1),
(500104, 0, 26767, 2.2, 1),
(500105, 0, 26767, 0.8, 1),
(500106, 0, 9354, 1.3, 1);

DELETE FROM `creature` WHERE `guid` = 5800101;
INSERT INTO `creature` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                        `orientation`, `spawntimesecs`) VALUES
(5800101, 500102, 801, 1, 1, -225, 0, 52, 0, 604800);

DELETE FROM `gameobject_template` WHERE `entry` = 500011;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `Data3`, `ScriptName`) VALUES
(500011, 3, 259, 'Khepret''s Hoard', 1.5, 57, 500011, 1, '');

-- Two rolls: a necklace, a cloak (item level 200 epics).
DELETE FROM `gameobject_loot_template` WHERE `Entry` = 500011;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`,
                                        `MinCount`, `MaxCount`, `Comment`) VALUES
(500011, 40678, 0, 0, 0, 1, 1, 1, 1, 'Khepret''s Hoard - Pendant of the Outcast Hero'),
(500011, 40679, 0, 0, 0, 1, 1, 1, 1, 'Khepret''s Hoard - Chained Military Gorget'),
(500011, 40680, 0, 0, 0, 1, 1, 1, 1, 'Khepret''s Hoard - Encircling Burnished Gold Chains'),
(500011, 40681, 0, 0, 0, 1, 1, 1, 1, 'Khepret''s Hoard - Lattice Choker of Light'),
(500011, 39225, 0, 0, 0, 1, 2, 1, 1, 'Khepret''s Hoard - Cloak of Armed Strife'),
(500011, 39241, 0, 0, 0, 1, 2, 1, 1, 'Khepret''s Hoard - Dark Shroud of the Scourge'),
(500011, 39272, 0, 0, 0, 1, 2, 1, 1, 'Khepret''s Hoard - Drape of Surgery'),
(500011, 39297, 0, 0, 0, 1, 2, 1, 1, 'Khepret''s Hoard - Cloak of Darkening');

-- The Ball of Flame model is only a carrier: its fire is the Ball of Flames Visual aura (71706), and it hovers
-- (bytes1 0x03000000), as Blizzard's own Ball of Flame (38332) has it.
DELETE FROM `creature_template_addon` WHERE `entry` BETWEEN 500103 AND 500105;
INSERT INTO `creature_template_addon` (`entry`, `bytes1`, `auras`) VALUES
(500103, 50331648, '71706'),
(500104, 50331648, '71706'),
(500105, 0, '71706');

-- The Hall of Weighing (milestone 3): the Scales' piles and pans, the mummies that walk them, Anpuet among his canopic
-- jars, the linen he wraps players in, the natron he throws, and his hoard. Stand-in models: an Anubisath Sentinel
-- (15347), Maexxna's web wrap (16213), the Mummified Atal'ai (4769), an unseen spirit (11686) in a jar (6358) for each
-- canopic jar, offering bowls (7928) for the pans, gold coins (2614) for the stones and leaves (6884) for the feathers,
-- and a purple acid cloud (70108) for the natron.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 500107 AND 500111;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `speed_walk`,
                                 `speed_run`, `detection_range`, `rank`, `DamageModifier`, `BaseAttackTime`,
                                 `RangeAttackTime`, `unit_class`, `unit_flags`, `type`, `HealthModifier`, `ArmorModifier`,
                                 `RegenHealth`, `flags_extra`, `ScriptName`) VALUES
(500107, 'Anpuet', 'The Embalmer', 83, 83, 2, 14, 1, 1.14286, 40, 3, 4, 2000, 2000, 1, 0, 6, 30, 1, 1, 1, 'boss_anpuet'),
(500108, 'Linen Wrapping', '', 80, 80, 2, 14, 1, 1, 0, 0, 1, 2000, 2000, 1, 0, 10, 1.5, 1, 0, 0, 'npc_linen_wrap'),
(500109, 'Natron', '', 80, 80, 2, 14, 1, 1, 0, 0, 1, 2000, 2000, 1, 33554690, 10, 1, 1, 1, 2, 'npc_natron'),
(500110, 'Canopic Jar', '', 82, 82, 2, 14, 1, 1, 0, 1, 1, 2000, 2000, 1, 0, 10, 4, 1, 0, 0, ''),
(500111, 'Wrapped Servant', '', 80, 80, 2, 14, 1, 1, 0, 0, 1, 2000, 2000, 1, 33554690, 6, 1, 1, 1, 2, '');

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 500107 AND 500111;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(500107, 0, 15347, 1, 1),
(500108, 0, 16213, 1, 1),
(500109, 0, 11686, 1, 1),
(500110, 0, 11686, 2, 1),
(500111, 0, 4769, 1.2, 1);

DELETE FROM `creature_template_addon` WHERE `entry` = 500109;
INSERT INTO `creature_template_addon` (`entry`, `auras`) VALUES (500109, '70108');

DELETE FROM `creature` WHERE `guid` = 5800102;
INSERT INTO `creature` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                        `orientation`, `spawntimesecs`) VALUES
(5800102, 500107, 801, 1, 1, 0, 205, 20, 4.7124, 604800);

DELETE FROM `gameobject_template` WHERE `entry` IN (500003, 500004, 500005, 500006, 500007, 500012);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `Data3`, `ScriptName`) VALUES
(500003, 10, 2614, 'Stone Weights', 2, 0, 0, 0, 'go_weighing'),
(500004, 10, 6884, 'Feathers of Truth', 2, 0, 0, 0, 'go_weighing'),
(500005, 10, 7928, 'Left Pan', 3, 0, 0, 0, 'go_weighing'),
(500006, 10, 7928, 'Right Pan', 3, 0, 0, 0, 'go_weighing'),
(500007, 5, 9700, 'Canopic Jar', 1.6, 0, 0, 0, ''),
(500012, 3, 259, 'Anpuet''s Hoard', 1.5, 57, 500012, 1, '');

-- The Scales stand in the middle of their room (0, 112), the piles by the way in; a jar stands at each of Anpuet's
-- jar spirits (JARS in hall_of_weighing.cpp).
DELETE FROM `gameobject` WHERE `guid` BETWEEN 5800005 AND 5800012;
INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                          `orientation`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`) VALUES
(5800005, 500003, 801, 1, 1, 6, 98, 20, 0, 0, 1, 0, 100, 1),
(5800006, 500004, 801, 1, 1, -6, 98, 20, 0, 0, 1, 0, 100, 1),
(5800007, 500005, 801, 1, 1, 14, 118, 20, 0, 0, 1, 0, 100, 1),
(5800008, 500006, 801, 1, 1, -14, 118, 20, 0, 0, 1, 0, 100, 1),
(5800009, 500007, 801, 1, 1, 28, 200, 20, 3.14, 1, 0, 0, 100, 1),
(5800010, 500007, 801, 1, 1, -14, 224.2, 20, 5.24, -0.5, 0.866, 0, 100, 1),
(5800011, 500007, 801, 1, 1, -26.3, 190.4, 20, 0.35, 0.174, 0.985, 0, 100, 1),
(5800012, 500007, 801, 1, 1, 21.4, 182, 20, 2.44, 0.940, 0.342, 0, 100, 1);

-- Two rolls from the same rings (item level 200 epics).
DELETE FROM `gameobject_loot_template` WHERE `Entry` = 500012;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`,
                                        `MinCount`, `MaxCount`, `Comment`) VALUES
(500012, 37192, 0, 0, 0, 1, 1, 1, 1, 'Anpuet''s Hoard - Annhylde''s Ring'),
(500012, 37642, 0, 0, 0, 1, 1, 1, 1, 'Anpuet''s Hoard - Hemorrhaging Circle'),
(500012, 37694, 0, 0, 0, 1, 1, 1, 1, 'Anpuet''s Hoard - Band of Guile'),
(500012, 37784, 0, 0, 0, 1, 1, 1, 1, 'Anpuet''s Hoard - Keystone Great-Ring'),
(500012, 39141, 0, 0, 0, 1, 2, 1, 1, 'Anpuet''s Hoard - Deflection Band'),
(500012, 39193, 0, 0, 0, 1, 2, 1, 1, 'Anpuet''s Hoard - Band of Neglected Pleas'),
(500012, 39231, 0, 0, 0, 1, 2, 1, 1, 'Anpuet''s Hoard - Timeworn Silken Band'),
(500012, 39244, 0, 0, 0, 1, 2, 1, 1, 'Anpuet''s Hoard - Ring of the Fated');

-- The Eclipse Pit (milestone 3): the Lightless Way's braziers and their flames, Apepheth, his sunbeams, the marks where
-- his coils burst up, the heart in his belly, and his hoard. Stand-in models: Hakkar's serpent (15295), an unseen
-- carrier (11686) for the sunbeams (Freya's Unstable Sun Beam visual, 62216) and coil marks (a dust cloud, 43258), the
-- Ball of Flame for brazier flames, the Heart of the Deconstructor (28887), the Shadowforge brazier (3411). The
-- decoration creatures are friendly (faction 35) and can't be targeted.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 500112 AND 500116;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `speed_walk`,
                                 `speed_run`, `detection_range`, `rank`, `DamageModifier`, `BaseAttackTime`,
                                 `RangeAttackTime`, `unit_class`, `unit_flags`, `type`, `HealthModifier`, `ArmorModifier`,
                                 `RegenHealth`, `flags_extra`, `ScriptName`) VALUES
(500112, 'Apepheth', 'The Unlit Coil', 83, 83, 2, 14, 1, 1.14286, 45, 3, 4, 2000, 2000, 1, 0, 1, 30, 1, 1, 1, 'boss_apepheth'),
(500113, 'Sunbeam', '', 80, 80, 2, 35, 1, 1, 0, 0, 1, 2000, 2000, 1, 33554690, 10, 1, 1, 1, 2, 'npc_sunbeam'),
(500114, 'Stirring Ground', '', 80, 80, 2, 35, 1, 1, 0, 0, 1, 2000, 2000, 1, 33554690, 10, 1, 1, 1, 2, ''),
(500115, 'Brazier Flame', '', 80, 80, 2, 35, 1, 1, 0, 0, 1, 2000, 2000, 1, 33554690, 10, 1, 1, 1, 2, ''),
(500116, 'Heart of the Coil', '', 82, 82, 2, 14, 1, 1, 0, 1, 1, 2000, 2000, 1, 131072, 10, 1, 1, 0, 0, '');

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 500112 AND 500116;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(500112, 0, 15295, 1, 1),
(500113, 0, 11686, 1, 1),
(500114, 0, 11686, 1, 1),
(500115, 0, 26767, 0.6, 1),
(500116, 0, 28887, 1, 1);

DELETE FROM `creature_template_addon` WHERE `entry` IN (500113, 500114, 500115);
INSERT INTO `creature_template_addon` (`entry`, `auras`) VALUES
(500113, '62216'),
(500114, '43258'),
(500115, '71706');

DELETE FROM `creature` WHERE `guid` = 5800103;
INSERT INTO `creature` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                        `orientation`, `spawntimesecs`) VALUES
(5800103, 500112, 801, 1, 1, 235, 0, 0, 3.1416, 604800);

DELETE FROM `gameobject_template` WHERE `entry` IN (500008, 500013);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `Data3`, `ScriptName`) VALUES
(500008, 10, 3411, 'Brazier', 1.5, 0, 0, 0, 'go_lightless_brazier'),
(500013, 3, 259, 'Apepheth''s Hoard', 1.5, 57, 500013, 1, '');

-- The braziers, from MapGen's printout ("brazier at"); the instance tells them apart by position.
DELETE FROM `gameobject` WHERE `guid` BETWEEN 5800013 AND 5800017;
INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`,
                          `orientation`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`) VALUES
(5800013, 500008, 801, 1, 1, 52, -4, 19.74, 0, 0, 1, 0, 100, 1),
(5800014, 500008, 801, 1, 1, 80, 21, 14.97, 0, 0, 1, 0, 100, 1),
(5800015, 500008, 801, 1, 1, 115, -19, 10.12, 0, 0, 1, 0, 100, 1),
(5800016, 500008, 801, 1, 1, 150, 11, 5.09, 0, 0, 1, 0, 100, 1),
(5800017, 500008, 801, 1, 1, 190, -4, 0, 0, 0, 1, 0, 100, 1);

-- One roll from the trinkets, one from the rest of them (item level 200 epics).
DELETE FROM `gameobject_loot_template` WHERE `Entry` = 500013;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`,
                                        `MinCount`, `MaxCount`, `Comment`) VALUES
(500013, 37254, 0, 0, 0, 1, 1, 1, 1, 'Apepheth''s Hoard - Super Simian Sphere'),
(500013, 37835, 0, 0, 0, 1, 1, 1, 1, 'Apepheth''s Hoard - Je''Tze''s Bell'),
(500013, 39229, 0, 0, 0, 1, 1, 1, 1, 'Apepheth''s Hoard - Embrace of the Spider'),
(500013, 39257, 0, 0, 0, 1, 1, 1, 1, 'Apepheth''s Hoard - Loatheb''s Shadow'),
(500013, 39292, 0, 0, 0, 1, 2, 1, 1, 'Apepheth''s Hoard - Repelling Charge'),
(500013, 39388, 0, 0, 0, 1, 2, 1, 1, 'Apepheth''s Hoard - Spirit-World Glass'),
(500013, 40430, 0, 0, 0, 1, 2, 1, 1, 'Apepheth''s Hoard - Majestic Dragon Figurine'),
(500013, 40682, 0, 0, 0, 1, 2, 1, 1, 'Apepheth''s Hoard - Sundial of the Exiled');
