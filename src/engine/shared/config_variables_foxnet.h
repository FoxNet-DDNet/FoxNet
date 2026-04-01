/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
// This helps IDEs properly syntax highlight the uses of the macro below.
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc)
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc)
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc)
#endif

MACRO_CONFIG_INT(FoxExampleInt, fox_example_int, 2, 0, 100, CFGFLAG_SERVER | CFGFLAG_SAVE, "Example integer config variable")
MACRO_CONFIG_STR(FoxExampleStr, fox_example_str, 100, "FoxNet", CFGFLAG_SERVER | CFGFLAG_SAVE, "Example string config variable")

MACRO_CONFIG_INT(SvFoxNetType, sv_foxnet_type, 0, 0, 2, CFGFLAG_SERVER | CFGFLAG_SAVE, "(Change Gametype Color) 0=Gores | 1=DDRace | 2=FoxNetwork")

MACRO_CONFIG_INT(SvRandomMapVoteOnStart, sv_random_map_vote_on_start, 0, 0, 1, CFGFLAG_SERVER, "Call a random map vote on server startup")
MACRO_CONFIG_INT(SvVoteSkipPrefix, sv_vote_skip_prefix, 1, 0, 1, CFGFLAG_SERVER, "Skips Prefixes for the vote message when calling a vote")

MACRO_CONFIG_INT(SvCustomVoteMenu, sv_custom_vote_menu, 1, 0, 1, CFGFLAG_SERVER, "Whether to use the custom vote menu or just show votes")

// Accounts & Currency
MACRO_CONFIG_INT(SvAccounts, sv_accounts, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_SAVE, "Enable player accounts")
MACRO_CONFIG_INT(SvAccountsForced, sv_accounts_forced, 0, 0, 1, CFGFLAG_SERVER, "Whether an account is necessary to play or not")
MACRO_CONFIG_INT(SvAccountsAllowRegister, sv_accounts_allow_register, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_SAVE, "Whether to allow new account registrations")
MACRO_CONFIG_INT(SvAccountsMaxLoginAttempts, sv_accounts_max_login_attempts, 5, 1, 10, CFGFLAG_SERVER | CFGFLAG_SAVE, "Maximum number of login attempts before a ban")
MACRO_CONFIG_INT(SvAccountsMaxRegister, sv_accounts_max_register, 2, 1, 10, CFGFLAG_SERVER | CFGFLAG_SAVE, "Maximum number of register attempts before a ban")

MACRO_CONFIG_STR(SvCurrencyName, sv_currency_name, 13, "$", CFGFLAG_SERVER, "Whatever you want your currency name to be")
MACRO_CONFIG_INT(SvLevelUpMoney, sv_levelup_money, 1000, 0, 5000, CFGFLAG_SERVER, "How much money a player should get if they level up")
MACRO_CONFIG_INT(SvPlaytimeMoney, sv_playtime_money, 500, 0, 5000, CFGFLAG_SERVER, "How much money a player should get every hour if playtime")

// Bot Detection
MACRO_CONFIG_INT(SvAntiAdBot, sv_anti_ad_bot, 1, 0, 1, CFGFLAG_SERVER, "Anti chat ad bot")
MACRO_CONFIG_INT(SvAntiBotBantime, sv_anti_bot_bantime, 240, 0, 34560, CFGFLAG_SERVER, "How long sv_anti_bot bans for if its set to 2")
MACRO_CONFIG_INT(SvAutoBanJSClient, sv_auto_ban_jsclient, 1, 0, 1, CFGFLAG_SERVER, "JS Client is a bot client commonly used for chat spamming")

// Prediction
MACRO_CONFIG_INT(SvReversePrediction, sv_prediction_test, 14, 1, 200, CFGFLAG_SERVER, "Reverse prediction margin")
MACRO_CONFIG_INT(SvExperimentalPrediction, sv_experimental_prediction, 1, 0, 1, CFGFLAG_SERVER, "Experimental Prediction for cosmetics, tries to use clients ping to nudge cosmetics to the correct position")

// snake
MACRO_CONFIG_INT(SvSnakeAutoMove, sv_snake_auto_move, 0, 0, 1, CFGFLAG_SERVER, "Whether snake keeps last input or can stand still if no inputs applied")
MACRO_CONFIG_INT(SvSnakeSpeed, sv_snake_speed, 6, 1, 50, CFGFLAG_SERVER, "Snake blocks per second speed")
MACRO_CONFIG_INT(SvSnakeDiagonal, sv_snake_diagonal, 1, 0, 1, CFGFLAG_SERVER, "Whether snake can move diagonally")
MACRO_CONFIG_INT(SvSnakeSmooth, sv_snake_smooth, 1, 0, 1, CFGFLAG_SERVER, "Whether snake moves smoothly")
MACRO_CONFIG_INT(SvSnakeTeePickup, sv_snake_tee_pickup, 1, 0, 1, CFGFLAG_SERVER, "Whether to add tees to the snake on touch or not")
MACRO_CONFIG_INT(SvSnakeCollision, sv_snake_collision, 1, 0, 1, CFGFLAG_SERVER, "Whether to have collision with blocks or not")

// Ufo
MACRO_CONFIG_INT(SvUfoMaxSpeed, sv_ufo_max_speed, 16, 1, 50, CFGFLAG_SERVER, "Ufos maximum speed")
MACRO_CONFIG_INT(SvUfoFriction, sv_ufo_friction, 90, 0, 100, CFGFLAG_SERVER, "Ufos friction (how fast it slows down when theres no movement)")
MACRO_CONFIG_INT(SvUfoAccel, sv_ufo_accel, 12, 1, 200, CFGFLAG_SERVER, "Ufos acceleration in any direction")
MACRO_CONFIG_INT(SvUfoTranslateVel, sv_ufo_translate_vel, 1, 0, 1, CFGFLAG_SERVER, "Whether to use normal character velocity aswell as UFOs")
MACRO_CONFIG_INT(SvUfoDisableFreeze, sv_ufo_disable_freeze, 1, 0, 1, CFGFLAG_SERVER, "Whether the character gets affected by freeze (cant move)")
MACRO_CONFIG_INT(SvAutoUfo, sv_auto_ufo, 0, 0, 1, CFGFLAG_SERVER, "Automatically gives every player an UFO (always)")
MACRO_CONFIG_INT(SvUfoLaserType, sv_ufo_laser_type, 0, 0, 6, CFGFLAG_SERVER, "Ufos laser type")
MACRO_CONFIG_INT(SvUfoHideHookColl, sv_ufo_hide_hook_coll, 2, 0, 2, CFGFLAG_SERVER, "Whether other people see UFO players hookcoll when flying down")
MACRO_CONFIG_INT(SvUfoBrakes, sv_ufo_brakes, 0, 0, 1, CFGFLAG_SERVER, "Allows the UFO to instantly stop and stay still if player is flying up, down, and holding Fire")

// Quiet Join
MACRO_CONFIG_INT(SvQuietJoin, sv_quiet_join, 0, 0, 1, CFGFLAG_SERVER, "Whether to disable the join message for players with the right password")
MACRO_CONFIG_STR(SvQuietJoinPassword, sv_quiet_join_password, 128, "", CFGFLAG_SERVER, "Password if QuietJoin is enabled")

// Quads
// MACRO_CONFIG_INT(SvMovingTiles, sv_moving_tiles, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether to use moving tiles using quads or not")

MACRO_CONFIG_INT(SvMovingTilesStopTime, sv_moving_tiles_stop_time, 0, 0, 1, CFGFLAG_SERVER, "Stops every quad")

// MACRO_CONFIG_INT(SvTeleGrenade, sv_tele_grenade, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether to use tele grenade")

MACRO_CONFIG_INT(SvInstantCoreUpdate, sv_instant_core_update, 0, 0, 1, CFGFLAG_SERVER, "Sends Info about a player every tick, even if not doing anything")
MACRO_CONFIG_INT(SvQStopaGivesDj, sv_qstopa_gives_dj, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether the QStopa quad should give dj back")

// Debug Id Pool
MACRO_CONFIG_INT(SvDebugIdPool, sv_debug_id_pool, 0, 0, 1, CFGFLAG_SERVER, "Debug Id allocation")

// Abilities
MACRO_CONFIG_INT(SvNoAuthCooldown, sv_no_auth_cooldown, 0, 0, 1, CFGFLAG_SERVER, "whether theres a cooldown for abilities on authed players")

// Weapon Drops
MACRO_CONFIG_INT(SvAllowWeaponDrops, sv_allow_weapon_drops, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Turns on functionality for /weapondrop")
MACRO_CONFIG_INT(SvDropWeaponVoteNo, sv_drop_weapon_vote_no, 1, 0, 1, CFGFLAG_SERVER, "requires sv_allow_weapon_drops, drop weapons using f4 (vote no)")
MACRO_CONFIG_INT(SvResetDropsOnLeave, sv_reset_drops_on_leave, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "If a player leaves while he was weapons dropped, they get reset")
MACRO_CONFIG_INT(SvDropWeaponOnDeath, sv_drop_weapon_on_death, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Requires sv_allow_weapon_drops")
MACRO_CONFIG_INT(SvDropsInFreezeFloat, sv_drops_in_freeze_float, 0, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Make Weapon Drops in freeze float up")
MACRO_CONFIG_INT(SvDropsHammerable, sv_drops_hammerable, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether drops can be hammered")
MACRO_CONFIG_INT(SvDropsMaxPerPlayer, sv_drops_max_per_player, 7, 0, 100, CFGFLAG_SERVER | CFGFLAG_GAME, "How many weapons a player can have dropped")

// PowerUps
MACRO_CONFIG_INT(SvSpawnPowerUps, sv_spawn_powerups, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether to spawn powerups randomly in the map")

// Solo on Spawn
MACRO_CONFIG_INT(SvSoloOnSpawn, sv_solo_on_spawn, 0, 0, 15, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether Players Should be solod on spawn + how long in seconds")

// Flags
MACRO_CONFIG_INT(SvAllowZoom, sv_allow_zoom, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether to allow zoom or not")
MACRO_CONFIG_INT(SvAllowHookColl, sv_allow_hook_coll, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether to allow hook coll or not")
MACRO_CONFIG_INT(SvAllowEyeWheel, sv_allow_eye_wheel, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether to allow eye wheel or not")
MACRO_CONFIG_INT(SvAllowDummy, sv_allow_dummy, 1, 0, 1, CFGFLAG_SERVER | CFGFLAG_GAME, "Whether to allow dummy or not (only applies for new players)")

// Cosmetics
MACRO_CONFIG_INT(SvCosmetics, sv_cosmetics, 1, 0, 1, CFGFLAG_SERVER, "Whether to allow cosmetics")
MACRO_CONFIG_INT(SvCosmeticLimit, sv_cosmetic_limit, 6, 0, 25, CFGFLAG_SERVER, "How many cosmetics a player can have at a time")
MACRO_CONFIG_INT(SvCorruptPickupPet, sv_corrupt_pickup_pet, 0, 0, 1, CFGFLAG_SERVER, "Just use c_pickup_pet and do some stuff :)")

// Dummies
MACRO_CONFIG_INT(SvAddDummies, add_dummies, 0, 0, SERVER_MAX_CLIENTS, CFGFLAG_SERVER, "Add debug dummies to server")

// Multimap
MACRO_CONFIG_INT(SvMultimap, sv_multimaps, 0, 0, 1, CFGFLAG_SERVER, "Whether to enable multimap functionality")
MACRO_CONFIG_INT(SvMultimapAllowInteraction, sv_multimap_allow_interaction, 0, 0, 1, CFGFLAG_SERVER, "Allow entities from different maps to interact with each other")
MACRO_CONFIG_INT(SvMultimapShowOthers, sv_multimap_show_others, 0, 0, 1, CFGFLAG_SERVER, "Allow players to see entities from different maps")

// Roulette
MACRO_CONFIG_INT(SvRouletteLength, sv_roulette_length, 140, 0, 500, CFGFLAG_SERVER | CFGFLAG_GAME, "Length of the roulette spinner")

// Ban Syncing
MACRO_CONFIG_INT(SvBanSyncing, sv_ban_syncing, 0, 0, 1, CFGFLAG_SERVER, "Whether to Sync bans every fs_ban_syncing_delay mins across servers")
MACRO_CONFIG_INT(SvBanSyncingDelay, sv_ban_syncing_delay, 15, 1, 500, CFGFLAG_SERVER, "How long the server waits between syncs")
MACRO_CONFIG_INT(SvQuietBanExpire, sv_quiet_ban_expire, 0, 0, 1, CFGFLAG_SERVER, "Whether the server should log ban expirations in the console")

// Lissajous
MACRO_CONFIG_INT(SvLissajousA, sv_lissajous_a, 2, 0, 15, CFGFLAG_SERVER, "Lissajous A")
MACRO_CONFIG_INT(SvLissajousB, sv_lissajous_b, 3, 0, 15, CFGFLAG_SERVER, "Lissajous B")

// Social
MACRO_CONFIG_STR(SvGithubRepo, sv_github_repo, 128, "github.com/FoxNet-DDNet/FoxNet", CFGFLAG_SERVER, "GitHub repository URL")
MACRO_CONFIG_STR(SvDiscordLink, sv_discord_link, 128, "", CFGFLAG_SERVER, "Discord Server Link")
// Force
MACRO_CONFIG_STR(SvForceSkin, sv_force_skin, 128, "", CFGFLAG_SERVER, "Force skin for all players (Leave empty to disable)")

// Discord Webhooks
MACRO_CONFIG_STR(DcReportsWebhookUrl, dc_reports_webhook_url, 256, "", CFGFLAG_SERVER, "What webhook reports get sent to")
MACRO_CONFIG_STR(DcBansWebhookUrl, dc_bans_webhook_url, 256, "", CFGFLAG_SERVER, "What webhook automated-bans get sent to")

MACRO_CONFIG_INT(SvReportsMinAccountAge, sv_reports_min_account_age, 30, 0, 1000, CFGFLAG_SERVER, "Minimum account age in Minutes required to report")
MACRO_CONFIG_INT(SvReportsDelay, sv_reports_delay, 60, 0, 10000, CFGFLAG_SERVER, "Minimum account age in Seconds required to report")
MACRO_CONFIG_INT(SvReportsPlaytimeBypass, sv_reports_playtime_bypass, 1, 0, 1, CFGFLAG_SERVER, "Whether to bypass playtime requirement for reporting")
MACRO_CONFIG_INT(SvReportsMinPlaytimeForBypass, sv_reports_min_playtime_for_bypass, 1, 0, 1000, CFGFLAG_SERVER, "Minimum playtime required to bypass reporting in Hours")

// Logging
MACRO_CONFIG_INT(SvLogWhispers, sv_log_whispers, 0, 0, 1, CFGFLAG_SERVER, "Whether to enable logging of whispers")
MACRO_CONFIG_INT(SvLogExtra, sv_log_extra, 0, 0, 2, CFGFLAG_SERVER, "Whether to enable extra logging")

MACRO_CONFIG_INT(SvExecBasedOnPort, sv_execute_based_on_port, 0, 0, 1, CFGFLAG_SERVER, "Executes a file based on Port, if port=8303 -> execs /port/8303.cfg")

// Random Stuff
MACRO_CONFIG_INT(SvTeeCursor, sv_tee_cursor, 0, 0, 1, CFGFLAG_SERVER, "Display everyones position at their cursor")
MACRO_CONFIG_INT(SvNoVel, sv_no_vel, 0, 0, 1, CFGFLAG_SERVER, "Set everyones snapping velocity to 0 (disables interpolation on the client)")


// Hide and Seek
MACRO_CONFIG_INT(SvMinigamesSameIp, sv_minigames_same_ip, 0, 0, 1, CFGFLAG_SERVER, "Whether to allow the same ip in minigames")

MACRO_CONFIG_INT(SvHideSeekGiveXp, sv_hide_seek_give_xp, 1, 0, 1, CFGFLAG_SERVER, "Whether the minigame should give xp on game finish")
MACRO_CONFIG_INT(SvHideSeekWarmupTime, sv_hide_seek_warmup_time, 10, 1, 60, CFGFLAG_SERVER, "Delay in seconds before the game starts")
MACRO_CONFIG_INT(SvHideSeekFreezeDuration, sv_hide_seek_freeze_duration, 10, 1, 30, CFGFLAG_SERVER, "How long seekers are frozen on game start")
MACRO_CONFIG_INT(SvHideSeekSeekersTime, sv_hide_seek_seekers_time, 80, 1, 500, CFGFLAG_SERVER, "How much time seekers have to find all hiders")
MACRO_CONFIG_INT(SvHideSeekSeekersGunCooldown, sv_hide_seek_seekers_gun_cooldown, 5000, 1, 10000, CFGFLAG_SERVER, "How long the cooldown for the seekers gun is (like normal weapons)")
MACRO_CONFIG_INT(SvHideSeekSeekersGunFreeze, sv_hide_seek_seekers_gun_freeze, 0, 0, 10000, CFGFLAG_SERVER, "Whether the gun should freeze hit hiders and how long (in ticks, 50 ticks in a second)")
MACRO_CONFIG_INT(SvHideSeekSeekersHammerDelay, sv_hide_seek_seekers_hammer_delay, 275, 1, 10000, CFGFLAG_SERVER, "How long the cooldown for the seekers hammer is (like normal weapons)")
MACRO_CONFIG_INT(SvHideSeekHidersGhostDuration, sv_hide_seek_hiders_ghost_duration, 35, 1, 1000, CFGFLAG_SERVER, "How long hiders remain in ghost mode in second Seconds (30 -> 3 seconds)")
MACRO_CONFIG_INT(SvHideSeekHidersGhostCooldown, sv_hide_seek_hiders_ghost_cooldown, 55, 1, 1000, CFGFLAG_SERVER, "Cooldown time for hiders' ghost mode in second Seconds (30 -> 3 seconds)")

// Scripting

// Notes:
// If a script bans a player, sv_script_player_bans will not be executed.
// ^ Applies to all scripts, scripts do not trigger other scripts automatically to avoid infinite loops.
// So if you want some fifo server wide banning or something like that, you have to do it in every script manually, sv_script_player_bans will not be called
MACRO_CONFIG_STR(SvScriptStartup, sv_script_startup, 128, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Script that gets executed on server start")
MACRO_CONFIG_STR(SvScriptShutdown, sv_script_shutdown, 128, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Script that gets executed on server shutdown")
MACRO_CONFIG_STR(SvScriptPlayerConnect, sv_script_player_connect, 128, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Script that gets executed when a player connects")
MACRO_CONFIG_STR(SvScriptPlayerDisconnect, sv_script_player_disconnect, 128, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Script that gets executed when a player disconnects")
MACRO_CONFIG_STR(SvScriptPlayerBans, sv_script_player_bans, 128, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Script that gets executed after an ip gets banned/unbanned")
MACRO_CONFIG_STR(SvScriptPlayerKicks, sv_script_player_kicks, 128, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Script that gets executed after a player gets kicked")
MACRO_CONFIG_STR(SvScriptPlayerMutes, sv_script_player_mutes, 128, "", CFGFLAG_SERVER | CFGFLAG_GAME, "Script that gets executed after a player gets muted")
