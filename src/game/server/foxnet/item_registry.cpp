#include "item_registry.h"

#include <base/str.h>
#include <base/system.h>

#include <generated/protocol.h>

#include <game/server/player.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

void ValidateItemPricing(long Price, int Stars, EItemRarity Rarity, const char *pItemName)
{
	EItemRarity SuggestedRarity = SuggestRarityFromPrice(Price);
	int SuggestedStars = SuggestStarsFromPrice(Price);

	if(SuggestedRarity != Rarity)
	{
		dbg_msg("item_validation", "Warning: '%s' price %ld has rarity %s -> %s %d Star",
			pItemName, Price, RarityToName(Rarity), RarityToName(SuggestedRarity), SuggestedStars);
	}

	if(abs(SuggestedStars - Stars) > 1)
	{
		dbg_msg("item_validation", "Warning: '%s' price %ld has %d stars -> %d Stars",
			pItemName, Price, Stars, SuggestedStars);
	}
}

void CItemRegistry::Init()
{
	auto Add = [&](CItemConfig Cfg) {
		for(const auto &Item : m_Map)
		{
			dbg_assert(str_comp_nocase(Item.second.m_pName, Cfg.m_pName) != 0, "duplicate item name '%s'", Cfg.m_pName);
			dbg_assert(str_comp_nocase(Item.second.m_pShortcut, Cfg.m_pShortcut) != 0, "duplicate shortcut '%s'", Cfg.m_pShortcut);
		}
		if(Cfg.m_Price >= 0 && Cfg.m_Type != EItemType::Case)
			ValidateItemPricing(Cfg.m_Price, Cfg.m_Stars, Cfg.m_Rarity, Cfg.m_pName);
		m_Map.emplace(std::string(Cfg.m_pName), std::move(Cfg));
	};

	// Rainbow Set
	Add({EItemId::RainbowFeet, EItemType::Rainbow,
		"Rainbow Feet", "R_F",
		EItemFlag::Equippable, EExclusiveGroup::None,
		1250, 1, 1, EItemRarity::Common, "Makes your feet rainbow",
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowFeet = true; },
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowFeet = false; },
		30}); // Default Days

	Add({EItemId::RainbowBody, EItemType::Rainbow,
		"Rainbow Body", "R_B",
		EItemFlag::Equippable, EExclusiveGroup::None,
		2000, 4, 1, EItemRarity::Common, "Makes your body rainbow",
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowBody = true; },
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_RainbowBody = false; },
		30});

	Add({EItemId::RainbowHook, EItemType::Rainbow,
		"Rainbow Hook", "R_H",
		EItemFlag::Equippable, EExclusiveGroup::None,
		15000, 3, 1, EItemRarity::Rare, "Anyone you hook becomes rainbow!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_HookPower = HOOKTYPE_RAINBOW; },
		[](CPlayer &pl, const CItemConfig &, int) { pl.Cosmetics()->m_HookPower = HOOKTYPE_NORMAL; },
		30});

	// Effects
	Add({EItemId::Sparkle, EItemType::Effect,
		"Sparkle", "E_S",
		EItemFlag::Equippable, EExclusiveGroup::None,
		1500, 5, 1, EItemRarity::Common, "Makes you sparkle",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetSparkle(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetSparkle(false); },
		30});

	Add({EItemId::Lovely, EItemType::Effect,
		"Lovely", "E_L",
		EItemFlag::Equippable, EExclusiveGroup::None,
		18000, 15, 4, EItemRarity::Rare, "Spreading love huh?",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetLovely(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetLovely(false); },
		30});

	Add({EItemId::InverseAim, EItemType::Effect,
		"Inverse Aim", "E_I",
		EItemFlag::Equippable, EExclusiveGroup::None,
		600000, 65, 3, EItemRarity::Legendary, "Shows your aim backwards for others!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetInverseAim(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetInverseAim(false); },
		30});

	Add({EItemId::RotatingBall, EItemType::Effect,
		"Rotating Ball", "E_R",
		EItemFlag::Equippable, EExclusiveGroup::None,
		32500, 15, 2, EItemRarity::Epic, "Ball rotate - life good",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetRotatingBall(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetRotatingBall(false); },
		30});

	Add({EItemId::Halo, EItemType::Effect,
		"Halo", "E_H",
		EItemFlag::Equippable, EExclusiveGroup::None,
		75000, 35, 3, EItemRarity::Epic, "an intertwining halo floating above you",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHalo(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHalo(false); },
		30});

	// Guns (value-based + types)
	Add({EItemId::EmoticonGun, EItemType::Gun,
		"Emoticon Gun", "G_E",
		EItemFlag::Equippable, EExclusiveGroup::None, // can be combined with type guns in original
		16500, 10, 2, EItemRarity::Rare, "Shoot emotions at people",
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

	Add({EItemId::PhaseGun, EItemType::Gun,
		"Phase Gun", "G_P",
		EItemFlag::Equippable, EExclusiveGroup::None,
		8250, 5, 2, EItemRarity::Uncommon, "Your bullets defy physics",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetPhaseGun(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetPhaseGun(false); },
		30});

	Add({EItemId::HeartGun, EItemType::Gun,
		"Heart Gun", "G_H",
		EItemFlag::Equippable, EExclusiveGroup::Gun,
		55000, 15, 1, EItemRarity::Epic, "Shoot bullets full of love",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetGunType(EGunType::Heart); },
		[](CPlayer &pl, const CItemConfig &, int) {
			if(pl.Cosmetics()->m_GunType == EGunType::Heart)
				pl.SetGunType(EGunType::None);
		},
		30});

	Add({EItemId::MixedGun, EItemType::Gun,
		"Mixed Gun", "G_M",
		EItemFlag::Equippable, EExclusiveGroup::Gun,
		80000, 25, 2, EItemRarity::Epic, "Shoots Hearts and Shields",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetGunType(EGunType::Mixed); },
		[](CPlayer &pl, const CItemConfig &, int) {
			if(pl.Cosmetics()->m_GunType == EGunType::Mixed)
				pl.SetGunType(EGunType::None);
		},
		30});

	Add({EItemId::LaserGun, EItemType::Gun,
		"Laser Gun", "G_L",
		EItemFlag::Equippable, EExclusiveGroup::Gun,
		40000, 25, 2, EItemRarity::Epic, "Lasertag in DDNet?",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetGunType(EGunType::Laser); },
		[](CPlayer &pl, const CItemConfig &, int) {
			if(pl.Cosmetics()->m_GunType == EGunType::Laser)
				pl.SetGunType(EGunType::None);
		},
		30});

	// Indicators
	Add({EItemId::IndicatorClockwise, EItemType::Indicator,
		"Clockwise Indicator", "I_C",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		4500, 5, 5, EItemRarity::Common, "Gun Hit -> turns Clockwise",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_CLOCKWISE); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_CLOCKWISE) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	Add({EItemId::IndicatorCounterclockwise, EItemType::Indicator,
		"Counter Clockwise Indicator", "I_CC",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		4500, 5, 5, EItemRarity::Common, "Gun Hit -> turns Counter-Clockwise",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_COUNTERWISE); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_COUNTERWISE) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	Add({EItemId::IndicatorInwardTurning, EItemType::Indicator,
		"Inward Turning Indicator", "I_IT",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		20000, 15, 3, EItemRarity::Rare, "Gun Hit -> turns Inward",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_INWARD); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_INWARD) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	Add({EItemId::IndicatorOutwardTurning, EItemType::Indicator,
		"Outward Turning Indicator", "I_OT",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		20000, 15, 3, EItemRarity::Rare, "Gun Hit -> turns Outward",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_OUTWARD); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_OUTWARD) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	Add({EItemId::IndicatorLine, EItemType::Indicator,
		"Line Indicator", "I_L",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		15000, 10, 2, EItemRarity::Rare, "Gun Hit -> goes in a Line",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_LINE); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_LINE) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	Add({EItemId::IndicatorCrisscross, EItemType::Indicator,
		"Criss Cross Indicator", "I_CrCs",
		EItemFlag::Equippable, EExclusiveGroup::DamageIndicator,
		10000, 10, 4, EItemRarity::Uncommon, "Gun Hit -> goes in a Criss Cross pattern",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDamageIndType(INDTYPE_CRISSCROSS); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DamageIndType==INDTYPE_CRISSCROSS) pl.SetDamageIndType(INDTYPE_NONE); },
		30});

	// Death effects
	Add({EItemId::DeathExplosive, EItemType::Death,
		"Explosive Death", "D_E",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		3250, 2, 3, EItemRarity::Common, "Go out with a Boom!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_EXPLOSION); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_EXPLOSION) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	Add({EItemId::DeathHammerHit, EItemType::Death,
		"Hammer Hit Death", "D_H",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		3250, 2, 3, EItemRarity::Common, "Get Bonked on death!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_HAMMERHIT); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_HAMMERHIT) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	Add({EItemId::DeathIndicator, EItemType::Death,
		"Indicator Death", "D_I",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		7500, 10, 4, EItemRarity::Uncommon, "Creates an octagon of damage indicators",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_DAMAGEIND); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_DAMAGEIND) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	Add({EItemId::DeathLaser, EItemType::Death,
		"Laser Death", "D_L",
		EItemFlag::Equippable, EExclusiveGroup::DeathEffect,
		17500, 10, 2, EItemRarity::Rare, "Become wizard and summon lasers on death!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetDeathEffect(DEATHTYPE_LASER); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_DeathEffect==DEATHTYPE_LASER) pl.SetDeathEffect(DEATHTYPE_NONE); },
		30});

	// Trails
	Add({EItemId::TrailStar, EItemType::Trail,
		"Star Trail", "T_S",
		EItemFlag::Equippable, EExclusiveGroup::Trail,
		8000, 7, 4, EItemRarity::Uncommon, "The Stars shall follow you",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetTrail(TRAILTYPE_STAR); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_Trail==TRAILTYPE_STAR) pl.SetTrail(TRAILTYPE_NONE); },
		30});

	Add({EItemId::TrailDot, EItemType::Trail,
		"Dot Trail", "T_D",
		EItemFlag::Equippable, EExclusiveGroup::Trail,
		8000, 7, 4, EItemRarity::Uncommon, "A trail made out of small dots",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetTrail(TRAILTYPE_DOT); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_Trail==TRAILTYPE_DOT) pl.SetTrail(TRAILTYPE_NONE); },
		30});

	// Hats
	Add({EItemId::HammerHat, EItemType::Hat,
		"Hammer Hat", "Hm_H",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Hammer above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Hammer); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Hammer) pl.SetHatType(EHatType::None); },
		30});

	Add({EItemId::GunHat, EItemType::Hat,
		"Gun Hat", "H_G",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Gun above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Gun); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Gun) pl.SetHatType(EHatType::None); },
		30});

	Add({EItemId::ShotgunHat, EItemType::Hat,
		"Shotgun Hat", "H_SG",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Shotgun above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Shotgun); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Shotgun) pl.SetHatType(EHatType::None); },
		30});

	Add({EItemId::GrenadeHat, EItemType::Hat,
		"Grenade Hat", "H_GR",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Grenade above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Grenade); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Grenade) pl.SetHatType(EHatType::None); },
		30});

	Add({EItemId::LaserHat, EItemType::Hat,
		"Laser Hat", "H_L",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Laser above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Laser); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Laser) pl.SetHatType(EHatType::None); },
		30});

	Add({EItemId::NinjaHat, EItemType::Hat,
		"Ninja Hat", "H_N",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		4000, 5, 5, EItemRarity::Common, "Ninja weapon above your head",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Ninja); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Ninja) pl.SetHatType(EHatType::None); },
		30});

	Add({EItemId::HeartHat, EItemType::Hat,
		"Heart Hat", "H_H",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		15000, 12, 3, EItemRarity::Rare, "A hat of Hearts",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHeartHat(true); },
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHeartHat(false); },
		30});

	Add({EItemId::PartyHat, EItemType::Hat,
		"Party Hat", "H_P",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		// Price, MinLevel, Stars, Rarity
		24000, 10, 5, EItemRarity::Rare, "Throwing a Party?",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Party); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Party) pl.SetHatType(EHatType::None); },
		30});

	Add({EItemId::TophatHat, EItemType::Hat,
		"Top hat", "H_T",
		EItemFlag::Equippable, EExclusiveGroup::Hat,
		// Price, MinLevel, Stars, Rarity
		75000, 20, 3, EItemRarity::Epic, "Only missing a monocle now!",
		[](CPlayer &pl, const CItemConfig &, int) { pl.SetHatType(EHatType::Tophat); },
		[](CPlayer &pl, const CItemConfig &, int) { if(pl.Cosmetics()->m_HatType == EHatType::Tophat) pl.SetHatType(EHatType::None); },
		30});

	// Roles (not toggleable)
	Add({EItemId::SuperUser, EItemType::Role,
		"Super User", "SU",
		EItemFlag::Upgrade, EExclusiveGroup::None,
		UnbuyablePrice, 0, 5, EItemRarity::Legendary, "Extra Permissions",
		nullptr, nullptr, ForeverDays});

	Add({EItemId::Booster, EItemType::Role,
		"Server Booster", "BOOST",
		EItemFlag::Upgrade, EExclusiveGroup::None,
		125000, 25, 5, EItemRarity::Epic, "Grants a 1.5x boost on XP/Money for everyone",
		nullptr, nullptr, 30});

	Add({EItemId::VIP, EItemType::Role,
		"VIP", "VIP",
		EItemFlag::Upgrade, EExclusiveGroup::None,
		300000, 40, 5, EItemRarity::Mythic, "Grants a 2.5x boost on XP/Money\nand a discount on all Items",
		nullptr, nullptr, 30});

	Add({EItemId::MVP, EItemType::Role,
		"MVP", "MVP",
		EItemFlag::Upgrade, EExclusiveGroup::None,
		650000, 65, 2, EItemRarity::Legendary, "Grants a 3.5x boost on XP/Money\nand a discount on all Items",
		nullptr, nullptr, 30});

	// Loot cases

	Add({EItemId::LootCaseCommon, EItemType::Case,
		"Loot Case (Common)", "LC_C",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		2000, 10, 5, EItemRarity::Common, "Gives you a random common item!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	Add({EItemId::LootCaseUncommon, EItemType::Case,
		"Loot Case (Uncommon)", "LC_UC",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		6000, 15, 5, EItemRarity::Uncommon, "Gives you a random uncommon item!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	Add({EItemId::LootCaseRare, EItemType::Case,
		"Loot Case (Rare)", "LC_R",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		14000, 25, 5, EItemRarity::Rare, "Gives you a random rare item!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	//Add({EItemId::LootCaseEpic, "Loot Case (Epic)", "LC_E",
	//	EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
	//	40000, 35, 5, EItemRarity::Epic, "Gives you a random epic item!",
	//	[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});

	Add({EItemId::LootCaseExotic, EItemType::Case,
		"Loot Case (Exotic)", "LC_Ex",
		EItemFlag::Consumable | EItemFlag::LootCase, EExclusiveGroup::None,
		160000, 40, 5, EItemRarity::Legendary, "Gives you a random item of any type!",
		[](CPlayer &, const CItemConfig &, int) {}, nullptr, 0});


	// Sorted Map

	for(const auto &Item : m_Map)
		m_vSortedMap.push_back(&Item.second);

	std::sort(m_vSortedMap.begin(), m_vSortedMap.end(), [](const CItemConfig *pA, const CItemConfig *pB) {
		if(pA->m_Type != pB->m_Type)
			return pA->m_Type < pB->m_Type;
		return pA->m_Id < pB->m_Id;
	});
}

const CItemConfig *CItemRegistry::FindByName(const char *pName) const
{
	auto It = m_Map.find(std::string(pName));
	if(It != m_Map.end())
		return &It->second;
	for(const auto &Item : m_Map)
		if(!str_comp_nocase(Item.first.c_str(), pName))
			return &Item.second;
	return nullptr;
}

const CItemConfig *CItemRegistry::FindById(EItemId Id) const
{
	for(const auto &Item : m_Map)
	{
		if(Item.second.m_Id == Id)
			return &Item.second;
	}
	return nullptr;
}

CItemConfig *CItemRegistry::FindMutableByName(const char *pName)
{
	auto It = m_Map.find(std::string(pName));
	if(It != m_Map.end())
		return &It->second;
	for(auto &Item : m_Map)
		if(!str_comp_nocase(Item.first.c_str(), pName))
			return &Item.second;
	return nullptr;
}
