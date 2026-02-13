#ifndef GAME_SERVER_FOXNET_ITEM_REGISTRY_H
#define GAME_SERVER_FOXNET_ITEM_REGISTRY_H

#include <functional>
#include <string>
#include <unordered_map>
#include <type_traits>

constexpr int MAX_ITEM_STARS = 5;

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
	PartyHat,
	HeartHat,
	Sparkle,
	InverseAim,
	Lovely,
	RotatingBall,
	BOOSTER,
	VIP,
	MVP,
	LootCaseCommon,
	LootCaseUncommon,
	LootCaseRare,
	LootCaseExotic
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
	LootCase = 1 << 2
};

inline constexpr EItemFlag operator|(EItemFlag a, EItemFlag b)
{
	using U = std::underlying_type_t<EItemFlag>;
	return static_cast<EItemFlag>(static_cast<U>(a) | static_cast<U>(b));
}

inline constexpr bool HasFlag(EItemFlag f, EItemFlag test)
{
	using U = std::underlying_type_t<EItemFlag>;
	return (static_cast<U>(f) & static_cast<U>(test)) != 0;
}

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
	case EItemType::Case: return "Cᴀsᴇs";
	case EItemType::Hat: return "Hᴀᴛs";
	case EItemType::Gun: return "Gᴜɴs";
	case EItemType::Trail: return "Tʀᴀɪʟs";
	case EItemType::Effect: return "Eғғᴇᴄᴛs";
	case EItemType::Death: return "Dᴇᴀᴛʜ Eғғᴇᴄᴛs";
	case EItemType::Indicator: return "Gᴜɴ Hɪᴛ Eғғᴇᴄᴛs";
	case EItemType::Rainbow: return "Rᴀɪɴʙᴏᴡ Eғғᴇᴄᴛs";
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
		m_Id(Id), m_Type(Type),
		m_pName(pName), m_pShortcut(pShortcut),
		m_Flags(Flags), m_Group(Group),
		m_Price(Price), m_MinLevel(MinLevel), m_Stars(Stars), m_Rarity(Rarity), m_pDescription(pDescription),
		m_Apply(std::move(Apply)), m_Remove(std::move(Remove)), m_DefaultDays(DefaultDays) {}
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
