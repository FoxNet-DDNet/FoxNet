#ifndef GAME_SERVER_FOXNET_ITEM_REGISTRY_H
#define GAME_SERVER_FOXNET_ITEM_REGISTRY_H

#include <algorithm>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include "fontconvert.h"

constexpr int MaxItemStars = 5;

constexpr int UnbuyablePrice = -1;
constexpr int ForeverDays = -1;

constexpr const char *ITEM_NAME_PROTECTION = "Name Protection";

enum class EItemId
{
	RainbowFeet,
	RainbowBody,
	RainbowHook,
	EmoticonGun,
	PhaseGun,
	HeartGun,
	MixedGun,
	LaserGun,
	IndicatorClockwise,
	IndicatorCounterclockwise,
	IndicatorInwardTurning,
	IndicatorOutwardTurning,
	IndicatorLine,
	IndicatorCrisscross,
	DeathExplosive,
	DeathHammerHit,
	DeathIndicator,
	DeathLaser,
	TrailStar,
	TrailDot,
	HammerHat,
	GunHat,
	ShotgunHat,
	GrenadeHat,
	LaserHat,
	NinjaHat,
	HeartHat,
	PartyHat,
	TophatHat,
	AntennaeHat,
	Sparkle,
	InverseAim,
	Lovely,
	RotatingBall,
	Halo,
	NameProtection,
	Booster,
	VIP,
	MVP,
	SuperUser,
	LootCaseCommon,
	LootCaseUncommon,
	LootCaseRare,
	LootCaseExotic,
	MaxCosmeticsUpgrade,
};

enum class EItemType
{
	Role,
	Case,
	Hat,
	Gun,
	Trail,
	Effect,
	Death,
	Indicator,
	Rainbow,
	Other,
	COUNT
};

enum class EItemRarity
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Mythic,
	Legendary
};

enum class EItemFlag
{
	None = 0,
	Equippable = 1 << 0,
	Consumable = 1 << 1,
	LootCase = 1 << 2,
	Upgrade = 1 << 3,
	Stackable = 1 << 4,
};

constexpr EItemFlag operator|(EItemFlag a, EItemFlag b)
{
	using U = std::underlying_type_t<EItemFlag>;
	return static_cast<EItemFlag>(static_cast<U>(a) | static_cast<U>(b));
}

constexpr bool HasFlag(EItemFlag f, EItemFlag Test)
{
	using U = std::underlying_type_t<EItemFlag>;
	return (static_cast<U>(f) & static_cast<U>(Test)) != 0;
}

inline std::string StarsString(int Stars)
{
	std::string s;
	for(int i = 0; i < Stars; i++)
		s += "★";
	for(int i = Stars; i < MaxItemStars; i++)
		s += "☆";
	return s;
}

inline const char *ItemTypeToName(EItemType Type)
{
	switch(Type)
	{
	case EItemType::Role: return ConvertToSmallCaps("Roles");
	case EItemType::Case: return ConvertToSmallCaps("Cases");
	case EItemType::Hat: return ConvertToSmallCaps("Hats");
	case EItemType::Gun: return ConvertToSmallCaps("Guns");
	case EItemType::Trail: return ConvertToSmallCaps("Trails");
	case EItemType::Effect: return ConvertToSmallCaps("Effects");
	case EItemType::Death: return ConvertToSmallCaps("Death Effects");
	case EItemType::Indicator: return ConvertToSmallCaps("Gun Hit Effects");
	case EItemType::Rainbow: return ConvertToSmallCaps("Rainbow Effects");
	case EItemType::Other: return ConvertToSmallCaps("Other");
	default: return ConvertToSmallCaps("Unknown");
	}
}

inline const char *RarityToName(EItemRarity Type)
{
	switch(Type)
	{
	case EItemRarity::Common:
		return "Common";
	case EItemRarity::Uncommon:
		return "Uncommon";
	case EItemRarity::Rare:
		return "Rare";
	case EItemRarity::Epic:
		return "Epic";
	case EItemRarity::Mythic:
		return "Mythic";
	case EItemRarity::Legendary:
		return "Legendary";
	default:
		return "Unknown";
	}
}

static constexpr int PriceMinCommon = 1;
static constexpr int PriceMaxCommon = 4500;
static constexpr int PriceMinUncommon = PriceMaxCommon + 1;
static constexpr int PriceMaxUncommon = 10000;
static constexpr int PriceMinRare = PriceMaxUncommon + 1;
static constexpr int PriceMaxRare = 25000;
static constexpr int PriceMinEpic = PriceMaxRare + 1;
static constexpr int PriceMaxEpic = 125000;
static constexpr int PriceMinMythic = PriceMaxEpic + 1;
static constexpr int PriceMaxMythic = 300000;
static constexpr int PriceMinLegendary = PriceMaxMythic + 1;
static constexpr int PriceMaxLegendary = 1000000;

inline EItemRarity SuggestRarityFromPrice(long Price)
{
	if(Price < 0)
		return EItemRarity::Legendary;
	if(Price <= PriceMaxCommon)
		return EItemRarity::Common;
	if(Price <= PriceMaxUncommon)
		return EItemRarity::Uncommon;
	if(Price <= PriceMaxRare)
		return EItemRarity::Rare;
	if(Price <= PriceMaxEpic)
		return EItemRarity::Epic;
	if(Price <= PriceMaxMythic)
		return EItemRarity::Mythic;
	return EItemRarity::Legendary;
}

inline int SuggestStarsFromPrice(long Price)
{
	if(Price < 0)
		return 5;

	EItemRarity Rarity = SuggestRarityFromPrice(Price);
	long MinPrice, MaxPrice;

	switch(Rarity)
	{
	case EItemRarity::Common:
		MinPrice = PriceMinCommon;
		MaxPrice = PriceMaxCommon;
		break;
	case EItemRarity::Uncommon:
		MinPrice = PriceMinUncommon;
		MaxPrice = PriceMaxUncommon;
		break;
	case EItemRarity::Rare:
		MinPrice = PriceMinRare;
		MaxPrice = PriceMaxRare;
		break;
	case EItemRarity::Epic:
		MinPrice = PriceMinEpic;
		MaxPrice = PriceMaxEpic;
		break;
	case EItemRarity::Mythic:
		MinPrice = PriceMinMythic;
		MaxPrice = PriceMaxMythic;
		break;
	case EItemRarity::Legendary:
		MinPrice = PriceMinLegendary;
		MaxPrice = PriceMaxLegendary;
		break;
	default:
		return 3;
	}

	float Ratio = (float)(Price - MinPrice) / (float)(MaxPrice - MinPrice);
	Ratio = std::clamp(Ratio, 0.0f, 1.0f);

	int Stars = 1 + (int)(Ratio * 4.0f);
	return std::clamp(Stars, 1, 5);
}

void ValidateItemPricing(long Price, int Stars, EItemRarity Rarity, const char *pItemName);

enum class EExclusiveGroup
{
	None,
	Hat,
	Trail,
	Gun,
	DamageIndicator,
	DeathEffect
};

class CItemConfig
{
public:
	EItemId m_Id;
	EItemType m_Type;
	const char *m_pName;
	const char *m_pShortcut;
	EItemFlag m_Flags;
	EExclusiveGroup m_Group;
	long m_Price;
	int m_MinLevel;
	int m_Stars;
	EItemRarity m_Rarity;
	const char *m_pDescription;
	std::function<void(class CPlayer &, const CItemConfig &, int)> m_Apply;
	std::function<void(class CPlayer &, const CItemConfig &, int)> m_Remove;
	int m_DefaultDays = 30;

	CItemConfig(EItemId Id,
		EItemType Type,
		const char *pName,
		const char *pShortcut,
		EItemFlag Flags,
		EExclusiveGroup Group,
		int Price,
		int MinLevel,
		int Stars,
		EItemRarity Rarity,
		const char *pDescription,
		std::function<void(class CPlayer &, const CItemConfig &, int)> Apply,
		std::function<void(class CPlayer &, const CItemConfig &, int)> Remove,
		int DefaultDays = 30) :
		m_Id(Id), m_Type(Type), m_pName(pName), m_pShortcut(pShortcut), m_Flags(Flags), m_Group(Group), m_Price(Price), m_MinLevel(MinLevel), m_Stars(Stars), m_Rarity(Rarity), m_pDescription(pDescription), m_Apply(std::move(Apply)), m_Remove(std::move(Remove)), m_DefaultDays(DefaultDays) {}
};

class CItemRegistry
{
	std::unordered_map<std::string, CItemConfig> m_Map;
	std::vector<const CItemConfig *> m_vSortedMap;

public:
	void Init();
	const CItemConfig *FindByName(const char *pName) const;
	const CItemConfig *FindById(EItemId Id) const;

	CItemConfig *FindMutableByName(const char *pName);
	// Accessors
	const std::unordered_map<std::string, CItemConfig> &Map() const { return m_Map; }
	std::unordered_map<std::string, CItemConfig> &Map() { return m_Map; }

	const std::vector<const CItemConfig *> &SortedMap() const { return m_vSortedMap; }

	template<typename Fn>
	void ForEachInGroup(EExclusiveGroup Group, Fn &&f) const
	{
		if(Group == EExclusiveGroup::None)
			return;
		for(const auto &Item : m_Map)
		{
			const CItemConfig &Cfg = Item.second;
			if(Cfg.m_Group == Group)
				f(Cfg);
		}
	}
};

#endif // GAME_SERVER_FOXNET_ITEM_REGISTRY_H
