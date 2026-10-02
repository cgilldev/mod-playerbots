CREATE TABLE IF NOT EXISTS `playerbot_progression_state` (
  `id` tinyint unsigned NOT NULL,
  `level_ceiling` tinyint unsigned NOT NULL DEFAULT 0,
  `session_state` tinyint unsigned NOT NULL DEFAULT 0,
  `session_seconds` int unsigned NOT NULL DEFAULT 0,
  `anchor1_guid` int unsigned NOT NULL DEFAULT 5,
  `anchor2_guid` int unsigned NOT NULL DEFAULT 6,
  `anchor1_zone` int unsigned NOT NULL DEFAULT 0,
  `anchor1_map` int unsigned NOT NULL DEFAULT 0,
  `anchor2_zone` int unsigned NOT NULL DEFAULT 0,
  `anchor2_map` int unsigned NOT NULL DEFAULT 0,
  `updated_at` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

INSERT IGNORE INTO `playerbot_progression_state` (`id`, `anchor1_guid`, `anchor2_guid`) VALUES (1, 5, 6);

CREATE TABLE IF NOT EXISTS `playerbot_progression_cohort` (
  `guid` int unsigned NOT NULL,
  `race` tinyint unsigned NOT NULL,
  `created_at` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `playerbot_progression_session` (
  `guid` int unsigned NOT NULL,
  `admitted_at` int unsigned NOT NULL DEFAULT 0,
  `minimum_end` int unsigned NOT NULL DEFAULT 0,
  `next_login_at` int unsigned NOT NULL DEFAULT 0,
  `last_logout` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
