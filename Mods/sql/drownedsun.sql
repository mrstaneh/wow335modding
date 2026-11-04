-- Sanctum of the Drowned Sun (map 801, Mods/DrownedSun/DESIGN.md, terrain in Tools/MpqTool/mapgen_drownedsun.cpp):
-- the instance, a portal in from the test map's clearing and one back out from the Sun Court. Custom ids: game objects
-- 500000+, spawns 5800000+.

DELETE FROM `instance_template` WHERE `map` = 801;
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES (801, 800, '', 0);

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
