-- Roelyrep, in the basement of the inn on TestMap (map 800). Position from MapGen's "basement" output.
SET @ENTRY := 900001, @GUID := 9000001, @TEXT := 900001;

DELETE FROM `creature_template` WHERE `entry` = @ENTRY;
INSERT INTO `creature_template` (`entry`, `name`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `type`) VALUES
(@ENTRY, 'Roelyrep', @TEXT, 30, 30, 35, 1, 1, 7); -- friendly to everyone, gossip, humanoid

DELETE FROM `creature_template_model` WHERE `CreatureID` = @ENTRY;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(@ENTRY, 0, 1291, 1, 1); -- human male, same look as Innkeeper Farley

DELETE FROM `npc_text` WHERE `ID` = @TEXT;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(@TEXT, 'Ah, a visitor! Not many find their way down to this basement. Welcome, $n.',
        'Ah, a visitor! Not many find their way down to this basement. Welcome, $n.', 1);

DELETE FROM `gossip_menu` WHERE `MenuID` = @TEXT;
INSERT INTO `gossip_menu` (`MenuID`, `TextID`) VALUES (@TEXT, @TEXT);

DELETE FROM `creature` WHERE `guid` = @GUID;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `MovementType`) VALUES
(@GUID, @ENTRY, 800, 77.43, -79.90, 0.1, 1.5708, 300, 0); -- facing the stairs
