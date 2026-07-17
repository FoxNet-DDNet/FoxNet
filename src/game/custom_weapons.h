// This file is intentionally included multiple times.

#ifndef MACRO_CUSTOM_WEAPON
#error "Define MACRO_CUSTOM_WEAPON before including custom_weapons.h"
#endif

// Id, display name, snapped vanilla weapon, default fire delay (ms), autofire, help text
MACRO_CUSTOM_WEAPON(TELEKINESIS, "Telekinesis", WEAPON_GUN, 125, false, "Move another player using the cursor")
MACRO_CUSTOM_WEAPON(HEARTGUN, "Heart Gun", WEAPON_GUN, 90, false, "Fire heart projectiles")
MACRO_CUSTOM_WEAPON(LIGHTSABER, "Lightsaber", WEAPON_GUN, 125, false, "Attack using a lightsaber")
MACRO_CUSTOM_WEAPON(PORTALGUN, "Portal Gun", WEAPON_LASER, 100, false, "Place linked portals")
MACRO_CUSTOM_WEAPON(METEOR, "Meteor Gun", WEAPON_GRENADE, 125, true, "Summon a meteor at the cursor")
