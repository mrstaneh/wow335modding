-- Sanctum of the Drowned Sun (map 801, Mods/DrownedSun/DESIGN.md, terrain in Tools/MpqTool/mapgen_drownedsun.cpp):
-- the instance, a portal in from the test map's clearing and one back out from the Sun Court. Custom ids: game objects
-- 500000+, creatures 500100+, spawns 5800000+. The scripts are in Mods/DrownedSun/mod-drowned-sun.

DELETE FROM `instance_template` WHERE `map` = 801;
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES (801, 800, 'instance_drowned_sun', 0);

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
(500100, 'Sebekhti', 'The River''s Jaw', 83, 83, 2, 14, 1, 1.14286, 40, 3, 6, 2000, 2000, 1, 1, 30, 1, 1, 1, 'boss_sebekhti'),
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
