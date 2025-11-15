### FoxNet, a DDNet server modification

# Features
- Accounts
- Cosmetics
- Shop
- Inventory
- Custom vote menu
- UFO
- Snake
- Moving tiles using quads (Fully KoG compatible!)
- Tele grenade (KoG Feature)
- Custom weapons
- Dropping weapons
- And much more!

# Commands/Configs

|Configs|Description|
|-|-|
|sv_random_map_vote_on_start|Changes the map vote to a random map vote on server start|
|sv_vote_menu_flags|Toggle Specific pages in the vote menu using flags|
|sv_vote_skip_prefix|Whether to skip "│" in vote menu descriptions|
|sv_accounts|Whether accounts are enabled or not|
|sv_currency_name|What name the currency should be|
|sv_levelup_money|How much money a player gets on level up|
|sv_playtime_money|How much money a player gets as playtime bonus|
|sv_anti_ad_bot|whether to ban and hide bot client advertisement mesasges|
|sv_prediction_test|Used for ufos visuals so the lasers are ontop of the player|
|sv_snake_auto_move|Whether the snake can stand still or not|
|sv_snake_speed|How fast the snake should move|
|sv_snake_diagonal|Whether the snake can move diagonally|
|sv_snake_smooth|Whether the snake moves smoothly|
|sv_snake_tee_pickup|Whether the snake can pick up other players|
|sv_snake_collision|Whether the snake should collide with blocks|
|sv_ufo_max_speed|Ufos max speed|
|sv_ufo_friction|How fast the Ufo slows down|
|sv_ufo_accel|Ufos accel in any direction|
|sv_ufo_translate_vel|Whether to use characters velocity|
|sv_ufo_disable_freeze|Whether a frozen player can move or not|
|sv_auto_ufo|Whether everyone always has an UFO|
|sv_ufo_laser_type|Ufos laser type|
|sv_ufo_hide_hook_coll|Whether to hide hook coll while flying down|
|sv_ufo_brakes|Whether pressing every button should stop the ufo completely|
|sv_moving_tiles|Whether to use moving tiles using quads or not|
|sv_moving_tiles_stop_time|Whether to have quads move or not|
|sv_tele_grenade|Whether to use tele grenade (KoG Feature)|
|sv_instant_core_update|Sends an update to clients instantly, always|
|sv_debug_quad_pos|Shows a laser on every top left corner of a valid interactable quad|
|sv_qstopa_gives_dj|Whether standing on a Quad Stopa gives back double jump|
|sv_no_auth_cooldown|Whether authed players have a cooldown on abilities|
|sv_allow_weapon_drops|Whether the /dropweapon command is allowed|
|sv_drop_weapon_vote_no|Whether players can drop weapons using vote no (f4)|
|sv_reset_drops_on_leave|Whether to reset dropped weapons when a player leaves|
|sv_drop_weapon_on_death|Whether all custom weapons are dropped on death|
|sv_drops_in_freeze_float|Whether dropped weapons should float up inside freeze|
|sv_spawn_powerups|Whether to spawn random Powerups (gives xp) on the map|
|sv_solo_on_spawn|Whether to spawn players in solo for a set seconds (value is used as delay in seconds)|
|sv_allow_zoom|Whether to allow zoom or not|
|sv_allow_hook_coll|Whether to allow hook coll or not|
|sv_allow_eye_wheel|Whether to allow eye wheel or not|
|sv_cosmetics|Whether to allow cosmetics or not|
|sv_cosmetic_limit|How many cosmetics a player can have at a time|
|sv_corrupt_pickup_pet|Just use c_pickup_pet and do some stuff :)|
|add_dummies|Add debug dummies to server|
|sv_roulette_length|Length of the roulette spinner|
|sv_ban_syncing|Whether to automatically exec and save bans across multiple servers|
|sv_ban_syncing_delay|How long to wait before syncing bans|
|sv_quiet_ban_expire|Whether the server should log ban expirations in the console|
|sv_lissajous_a|Use c_lissajous and try some combinations|
|sv_lissajous_b|Use c_lissajous and try some combinations|
|sv_github_repo|Github repo link that will be used everywhere|
|sv_force_skin|Force a skin for all players (Leave empty to disable)|

|Commands|Description|
|-|-|
|lasertext|Write Text using Lasers|
|projectiletext|Write text using hammer hit projectiles|
|chat_string_add|Adds a string to the chat detection list|
|chat_string_remove|Remove a string from the chat list|
|chat_strings_list|List all chat strings|
|chat_string_clear|Clears all strings from the chat list|
|name_string_add|Adds a string to the name detection list|
|name_string_remove|Remove a string from the name list|
|name_strings_list|List all name strings|
|name_string_clear|Clears all strings from the name list|
|force_login|Log into any account|
|force_logout|Logout any account that is logged in on this port|
|acc_edit|Edit any logged out account|
|snake|Toggle Snake|
|ufo|Toggle UFO|
|set_name|Set a clients name|
|set_clan|Set a clients clan|
|set_skin|Set a clients skin|
|set_color|Set a clients color|
|set_custom_color|Set whether a client uses custom color|
|set_color_body|Set players custom body color|
|set_color_feet|Set players custom feet color|
|set_afk|Set players afk sate|
|set_ability|Set a players ability|
|ignore_gamelayer|Makes a player able to go beyond the kill border|
|invisible|Hides a players character from everyone|
|vanish|Make a player seem offline|
|include_serverinfo|Whether to include a player in the serverinfo|
|redirect|Redirect a player to another server|
|passive|Make a player unable to interact with other players|
|hittable|Makes this player unhittable|
|hookable|Makes this player unable to be hooked by other players|
|collidable|Makes this player unable to be moved with other player collisions|
|set_tune_override|Set a players tune override|
|telekinesis_immunity|Make a player immune to telekinesis|
|telekinesis|Toggle telekinesis for a player|
|heartgun|Toggle heartgun for a player|
|lightsaber|Toggle heartgun for a player|
|portalgun|Toggle heartgun for a player|
|obfuscate|Makes a players name obfuscated
|spider_hook|Toggle spider hook for a player|
|spazzing|Makes a players character spazz around|
|fake_message|Send a message as a player that doesn't exist|
|map_vote_lock|Lock changing maps trough normal votes|
|cleanup_pickupdrops|Deletes all dropped weapons|
|hide_cosmetics|Hides all cosmetics for a player|
|hide_powerups|Hides all powerups for a player|
|record_insert|insert a new record for that name on the given map with given time|
|record_remove|remove all records a name has on the given map|
|record_remove_time|remove records a name has on given map with given time|
|record_remove_all|remove all records a name has|

|Chat Commands|Description|
|-|-|
|register|Register an account|
|login|Log into your account|
|logout|Log out of your account|
|password|Change your password|
|profile|View anyones profile|
|top5money|show top 5 accounts with most money|
|top5level|show top 5 accounts with highest level|
|top5playtime|show top 5 accounts with highest playtime|
|bet|Used to set your wager on the roulette|
|buyitem|Buy an Item from the shop|
|toggleitem|Toggle any item you own|
|dropweapon|Drop the weapon you're holding|
|repredict|Use this command along side your prediction margin to better predict cosmetics|

|Cosmetic Commands|
|-|
|c_lovely|
|c_staff_ind|
|c_epic_circle|
|c_rotating_ball|
|c_bloody|
|c_strongbloody|
|c_sparkle|
|c_inverse_aim|
|c_heart_hat|
|c_hookpower|
|c_pickuppet|
|c_star_trail|
|c_dot_trail|
|c_rainbow_body|
|c_rainbow_feet|
|c_rainbow_speed|
|c_phase_gun|
|c_emote_gun|
|c_confetti_gun|
|c_death_type|
|c_damageind_type|
|c_gun_type|