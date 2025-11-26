#ifndef GAME_SERVER_FOXNET_ITEM_REGISTRY_H
#define GAME_SERVER_FOXNET_ITEM_REGISTRY_H

#include <functional>
#include <string>
#include <unordered_map>

constexpr int MAX_ITEM_STARS = 5;

enum class EItemId : uint16_t
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
	Sparkle,
	InverseAim,
	Lovely,
	RotatingBall,
	VIP,
	MVP,
	LootCaseCommon,
	LootCaseUncommon,
	LootCaseRare,
	LootCaseExotic
};

enum class EItemType : uint16_t
{
	Role,
	Hat,
	Gun,
	Trail,
	Effect,
	Death,
	Indicator,
	Rainbow,
	Case,
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

enum class EItemFlag : uint32_t
{
	None = 0,
	Equippable = 1 << 0,
	Consumable = 1 << 1,
	LootCase = 1 << 2
};
inline EItemFlag operator|(EItemFlag a, EItemFlag b) { return (EItemFlag)((uint32_t)a | (uint32_t)b); }
inline bool HasFlag(EItemFlag f, EItemFlag test) { return ((uint32_t)f & (uint32_t)test) != 0; }

inline std::string StarsString(int Stars)
{
	std::string s;
	for(int i = 0; i < Stars; i++)
		s += "★";
	for(int i = Stars; i < MAX_ITEM_STARS; i++)
		s += "☆";
	return s;
}

inline const char *ItemTypeToName(EItemType Type)
{
	switch(Type)
	{
	case EItemType::Role: return "Rᴏʟᴇs";
	case EItemType::Hat: return "Hᴀᴛs";
	case EItemType::Gun: return "Gᴜɴs";
	case EItemType::Trail: return "Tʀᴀɪʟs";
	case EItemType::Effect: return "Eғғᴇᴄᴛs";
	case EItemType::Death: return "Dᴇᴀᴛʜ Eғғᴇᴄᴛs";
	case EItemType::Indicator: return "Gᴜɴ Hɪᴛ Eғғᴇᴄᴛs";
	case EItemType::Rainbow: return "Rᴀɪɴʙᴏᴡ Eғғᴇᴄᴛs";
	case EItemType::Case: return "Cᴀsᴇs";
	default: return "Oᴛʜᴇʀ";
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
	const char *m_Name;
	const char *m_Shortcut;
	EItemFlag m_Flags;
	EExclusiveGroup m_Group;
	int m_Price;
	int m_MinLevel;
	int m_Stars;
	EItemRarity m_Rarity;
	const char *m_Description;
	std::function<void(class CPlayer &, const CItemConfig &, int)> m_Apply;
	std::function<void(class CPlayer &, const CItemConfig &, int)> m_Remove;
	int m_DefaultDays = 30;

	// Explicit constructor to support brace initialization reliably
	CItemConfig(EItemId id,
		EItemType type,
		const char *name,
		const char *shortcut,
		EItemFlag flags,
		EExclusiveGroup group,
		int price,
		int minLevel,
		int stars,
		EItemRarity rarity,
		const char *description,
		std::function<void(class CPlayer &, const CItemConfig &, int)> apply,
		std::function<void(class CPlayer &, const CItemConfig &, int)> remove,
		int defaultDays = 30) :
		m_Id(id), m_Type(type),
		m_Name(name), m_Shortcut(shortcut),
		m_Flags(flags), m_Group(group),
		m_Price(price), m_MinLevel(minLevel), m_Stars(stars), m_Rarity(rarity), m_Description(description),
		m_Apply(std::move(apply)), m_Remove(std::move(remove)), m_DefaultDays(defaultDays) {}
};

class CItemRegistry
{
	std::unordered_map<std::string, CItemConfig> m_Map;

public:
	void Init();
	const CItemConfig *FindByName(const char *pName) const;
	const CItemConfig *FindById(EItemId Id) const;

	CItemConfig *FindMutableByName(const char *pName);

	// Accessors
	const std::unordered_map<std::string, CItemConfig> &Map() const { return m_Map; }
	std::unordered_map<std::string, CItemConfig> &Map() { return m_Map; }

	template<typename Fn>
	void ForEachInGroup(EExclusiveGroup Group, Fn &&f) const
	{
		if(Group == EExclusiveGroup::None)
			return;
		for(const auto &kv : m_Map)
		{
			const CItemConfig &Cfg = kv.second;
			if(Cfg.m_Group == Group)
				f(Cfg);
		}
	}
};

#endif // GAME_SERVER_FOXNET_ITEM_REGISTRY_H
