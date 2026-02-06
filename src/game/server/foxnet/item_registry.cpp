#include "item_registry.h"

#include <base/str.h>

#include <generated/protocol.h>

#include <game/server/player.h>

#include <algorithm>
#include <utility>
#include <string>
#include <vector>

void CItemRegistry::Init()
{
	auto add = [&](CItemConfig cfg) {
		for(const auto &kv : m_Map)
		{
			dbg_assert(str_comp_nocase(kv.second.m_Name, cfg.m_Name) != 0, "duplicate item name '%s'", cfg.m_Name);
			dbg_assert(str_comp_nocase(kv.second.m_Shortcut, cfg.m_Shortcut) != 0, "duplicate shortcut '%s'", cfg.m_Shortcut);
		}
		m_Map.emplace(std::string(cfg.m_Name), std::move(cfg));
	};

	// Rainbow Set
	add({EItemId::RainbowFeet, EItemType::Rainbow,
		"Rainbow Feet", "R_F",
		EItemFlag::Equippable, EExclusiveGroup::None,
		1250, 1, 1, EItemRarity::Common, "Makes your feet rainbow",
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowFeet = true; },
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowFeet = false; },
		30}); // Default Days

	add({EItemId::RainbowBody, EItemType::Rainbow,
		"Rainbow Body", "R_B",
		EItemFlag::Equippable, EExclusiveGroup::None,
		2000, 4, 1, EItemRarity::Common, "Makes your body rainbow",
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowBody = true; },
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowBody = false; },
		30});

	add({EItemId::RainbowHook, EItemType::Rainbow,
		"Rainbow Hook", "R_H",
		EItemFlag::Equippable, EExclusiveGroup::None,
		6500, 5, 1, EItemRarity::Uncommon, "Anyone you hook becomes rainbow!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_HookPower = HOOKTYPE_RAINBOW; },
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_HookPower = HOOKTYPE_NORMAL; },
		30});

	// Effects
	add({EItemId::Sparkle, EItemType::Effect,
		"Sparkle", "O_S",
		EItemFlag::Equippable, EExclusiveGroup::None,
		1500, 5, 1, EItemRarity::Common, "Makes you sparkle",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetSparkle(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetSparkle(false); },
		30});

	add({EItemId::Lovely, EItemType::Effect,
		"Lovely", "O_L",
		EItemFlag::Equippable, EExclusiveGroup::None,
		12500, 15, 2, EItemRarity::Rare, "Spreading love huh?",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetLovely(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetLovely(false); },
		30});

	add({EItemId::InverseAim, EItemType::Effect,
		"Inverse Aim", "O_I",
		EItemFlag::Equippable, EExclusiveGroup::None,
		50000, 35, 1, EItemRarity::Legendary, "Shows your aim backwards for others!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetInverseAim(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetInverseAim(false); },
		30});

	add({EItemId::RotatingBall, EItemType::Effect,
		"Rotating Ball", "O_R",
		EItemFlag::Equippable, EExclusiveGroup::None,
		12500, 15, 2, EItemRarity::Rare, "Ball rotate - life good",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetRotatingBall(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetRotatingBall(false); },
		30});

	// Guns (value-based + types)
	add({EItemId::EmoticonGun, EItemType::Gun,
		"Emoticon Gun", "G_E",
		EItemFlag::Equippable, EExclusiveGroup::None, // can be combined with type guns in original
		7500, 10, 4, EItemRarity::Uncommon, "Shoot emotions at people",
		[](CPlayer &pl, const CItemConfig &, int overrideValue) {
			if(overrideValue < 0) // toggle
			{
				int cur = pl.Cosmetics()->m_EmoticonGun;
				pl.SetEmoticonGun(cur ? 0 : 1);
			}
			else
			{
				int maxIdx = std::max(1, NUM_EMOTICONS - 1);
				int v = overrideValue;
				if(v < 0)
					v = 0;
				if(v > maxIdx)
					v = maxIdx;
				pl.SetEmoticonGun(v);
			}
		},
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetEmoticonGun(0); },
		30});

	add({EItemId::PhaseGun, EItemType::Gun,
		"Phase Gun", "G_P",
		EItemFlag::Equippable, EExclusiveGroup::None,
		3250, 5, 2, EItemRarity::Uncommon, "Your bullets defy physics",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetPhaseGun(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetPhaseGun(false); },
		30});

	add({EItemId::HeartGun, EItemType::Gun,
		"Heart Gun", "G_H",
		EItemFlag::Equippable, EExclusiveGroup::Gun,
		20000, 15, 1, EItemRarity::Epic, "Shoot bullets full of love",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetGunType(GUNTYPE_HEART); },
		[](CPlayer &pl, const CItemConfig &, int) {
			if(pl.Cosmetics()->m_GunType == GUNTYPE_HEART)
				pl.SetGunType(GUNTYPE_NONE);
		},
		30});

	add({EItemId::MixedGun, EItemType::Gun,
		"Mixed Gun", "G_M",
		EItemFlag::Equippable, EExclusiveGroup::Gun,
		25000, 25, 2, EItemRarity::Epic, "Shoots Hearts and Shields",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetGunType(GUNTYPE_MIXED); },
		[](CPlayer &pl, const CItemConfig &, int) {
			if(pl.Cosmetics()->m_GunType == GUNTYPE_MIXED)
				pl.SetGunType(GUNTYPE_NONE);
		},
		30});

	add({EItemId::LaserGun, EItemType::Gun, 
		"Laser Gun", "G_L",
		EItemFlag::Equippable, EExclusiveGroup::Gun,
		35000, 25, 5, EItemRarity::Epic, "Lasertag in DDNet?",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetGunType(GUNTYPE_LASER); },
		[](CPlayer &pl, const CItemConfig &, int) {
			if(pl.Cosmetics()->m_GunType == GUNTYPE_LASER)
				pl.SetGunType(GUNTYPE_NONE);
		},
		30});

	// Indicators
	add({EItemId::IndicatorClockwise, EItemType::Indicator,
		"Clockwise Indicator", "I_C",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		4500, 5, 5, EItemRarity::Common, "Gun Hit -> turns Clockwise",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_CLOCKWISE); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_CLOCKWISE) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	add({EItemId::IndicatorCounterclockwise, EItemType::Indicator,
		"Counter Clockwise Indicator", "I_CC",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		4500, 5, 5, EItemRarity::Common, "Gun Hit -> turns Counter-Clockwise",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_COUNTERWISE); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_COUNTERWISE) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	add({EItemId::IndicatorInwardTurning, EItemType::Indicator,
		"Inward Turning Indicator", "I_IT",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		8000, 15, 4, EItemRarity::Uncommon, "Gun Hit -> turns Inward",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_INWARD); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_INWARD) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	add({EItemId::IndicatorOutwardTurning, EItemType::Indicator,
		"Outward Turning Indicator", "I_OT",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		8000, 15, 4, EItemRarity::Uncommon, "Gun Hit -> turns Outward",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_OUTWARD); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_OUTWARD) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	add({EItemId::IndicatorLine, EItemType::Indicator,
		"Line Indicator", "I_L",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		6500, 10, 3, EItemRarity::Uncommon, "Gun Hit -> goes in a Line",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_LINE); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_LINE) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	add({EItemId::IndicatorCrisscross, EItemType::Indicator,
		"Criss Cross Indicator", "I_CrCs",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		6500, 10, 4, EItemRarity::Uncommon, "Gun Hit -> goes in a Criss Cross pattern",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_CRISSCROSS); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_CRISSCROSS) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	// Death effects
	add({EItemId::DeathExplosive, EItemType::Death,
		"Explosive Death", "D_E",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		3250, 5, 2, EItemRarity::Uncommon, "Go out with a Boom!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_EXPLOSION); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_EXPLOSION) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	add({EItemId::DeathHammerHit, EItemType::Death,
		"Hammer Hit Death", "D_H",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		3250, 5, 2, EItemRarity::Uncommon, "Get Bonked on death!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_HAMMERHIT); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_HAMMERHIT) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	add({EItemId::DeathIndicator, EItemType::Death,
		"Indicator Death", "D_I",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		7500, 10, 4, EItemRarity::Uncommon, "Creates an octagon of damage indicators",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_DAMAGEIND); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_DAMAGEIND) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	add({EItemId::DeathLaser, EItemType::Death,
		"Laser Death", "D_L",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		7500, 10, 4, EItemRarity::Uncommon, "Become wizard and summon lasers on death!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_LASER); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_LASER) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	// Trails
	add({EItemId::TrailStar, EItemType::Trail,
		"Star Trail", "T_S",
		EItemFlag::Equippable, EExclusiveGroup::Trail,
		8000, 7, 4, EItemRarity::Uncommon, "The Stars shall follow you",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetTrail(TRAILTYPE_STAR); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_Trail==TRAILTYPE_STAR) pl.SetTrail(TRAILTYPE_NONE); },
		30});

	add({EItemId::TrailDot, EItemType::Trail,
		"Dot Trail", "T_D",
		EItemFlag::Equippable, EExclusiveGroup::Trail,
		8000, 7, 4, EItemRarity::Uncommon, "A trail made out of small dots",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetTrail(TRAILTYPE_DOT); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_Trail==TRAILTYPE_DOT) pl.SetTrail(TRAILTYPE_NONE); },
		30});

	// Hats
	add({EItemId::HammerHat, EItemType::Hat,
		"Hammer Hat", "Hm_H",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Hammer above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(HatType::Hammer); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == HatType::Hammer) pl.SetHatType(HatType::None); },
		30});

	add({EItemId::GunHat, EItemType::Hat,
		"Gun Hat", "H_G",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Gun above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(HatType::Gun); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == HatType::Gun) pl.SetHatType(HatType::None); },
		30});

	add({EItemId::ShotgunHat, EItemType::Hat,
		"Shotgun Hat", "H_SG",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Shotgun above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(HatType::Shotgun); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == HatType::Shotgun) pl.SetHatType(HatType::None); },
		30});

	add({EItemId::GrenadeHat, EItemType::Hat,
		"Grenade Hat", "H_GR",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Grenade above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(HatType::Grenade); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == HatType::Grenade) pl.SetHatType(HatType::None); },
		30});

	add({EItemId::LaserHat, EItemType::Hat,
		"Laser Hat", "H_L",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Laser above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(HatType::Laser); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == HatType::Laser) pl.SetHatType(HatType::None); },
		30});

	add({EItemId::NinjaHat, EItemType::Hat,
		"Ninja Hat", "H_N",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Ninja weapon above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(HatType::Ninja); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == HatType::Ninja) pl.SetHatType(HatType::None); },
		30});

	add({EItemId::PartyHat, EItemType::Hat,
		"Party Hat", "H_P",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		// Price, MinLevel, Stars, Rarity
		18000, 10, 5, EItemRarity::Rare, "Throwing a Party?",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(HatType::Party); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == HatType::Party) pl.SetHatType(HatType::None); },
		30});

	add({EItemId::HeartHat, EItemType::Hat,
		"Heart Hat", "H_H",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		15000, 12, 3, EItemRarity::Rare, "A hat of Hearts",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHeartHat(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHeartHat(false); },
		30});

	// Roles (not toggleable)
	add({EItemId::BOOSTER, EItemType::Role,
		"Server Booster", "BOOST",
		EItemFlag::None, EExclusiveGroup::None,
		125000, 25, 2, EItemRarity::Epic, "Grants a 1.5x boost on XP/Money for everyone",
		nullptr, nullptr, 30});

	add({EItemId::VIP, EItemType::Role,
		"VIP", "VIP",
		EItemFlag::None, EExclusiveGroup::None,
		250000, 40, 4, EItemRarity::Mythic, "Grants a 2.5x boost on XP/Money\nand a discount on all Items",
		nullptr, nullptr, 30});

	add({EItemId::MVP, EItemType::Role,
		"MVP", "MVP",
		EItemFlag::None, EExclusiveGroup::None,
		600000, 65, 4, EItemRarity::Legendary, "Grants a 3.5x boost on XP/Money\nand a discount on all Items",
		nullptr, nullptr, 30});

	// Loot cases
	add({EItemId::LootCaseCommon, EItemType::Case,
		"Loot Case (Common)", "LC_C",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		4000, 10, 5, EItemRarity::Common, "Gives you a random common item!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	add({EItemId::LootCaseUncommon, EItemType::Case,
		"Loot Case (Uncommon)", "LC_UC",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		8000, 15, 5, EItemRarity::Uncommon, "Gives you a random uncommon item!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	add({EItemId::LootCaseRare, EItemType::Case,
		"Loot Case (Rare)", "LC_R",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		16000, 25, 5, EItemRarity::Rare, "Gives you a random rare item!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	//add({EItemId::LootCaseEpic, "Loot Case (Epic)", "LC_E",
	//	EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
	//	40000, 35, 5, EItemRarity::Epic, "Gives you a random epic item!",
	//	[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	add({EItemId::LootCaseExotic, EItemType::Case,
		"Loot Case (Exotic)", "LC_Ex",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		160000, 40, 5, EItemRarity::Legendary, "Gives you a random item of any type!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});
}

const CItemConfig *CItemRegistry::FindByName(const char *pName) const
{
	auto it = m_Map.find(std::string(pName));
	if(it != m_Map.end())
		return &it->second;
	for(const auto &kv : m_Map)
		if(!str_comp_nocase(kv.first.c_str(), pName))
			return &kv.second;
	return nullptr;
}

const CItemConfig *CItemRegistry::FindById(EItemId Id) const
{
	for(const auto &kv : m_Map)
	{
		if(kv.second.m_Id == Id)
			return &kv.second;
	}
	return nullptr;
}

CItemConfig *CItemRegistry::FindMutableByName(const char *pName)
{
	auto it = m_Map.find(std::string(pName));
	if(it != m_Map.end())
		return &it->second;
	for(auto &kv : m_Map)
		if(!str_comp_nocase(kv.first.c_str(), pName))
			return &kv.second;
	return nullptr;
}