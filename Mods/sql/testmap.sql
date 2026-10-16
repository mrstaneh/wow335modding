-- .tele TestMap: the hill top of the generated test map (map 800, see Tools/MpqTool/mapgen.cpp)
DELETE FROM `game_tele` WHERE `id` = 9800;
INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
(9800, -266.7, -266.7, 45, 0, 800, 'TestMap');
