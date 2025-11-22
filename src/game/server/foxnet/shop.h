#ifndef GAME_SERVER_FOXNET_COSMETICHANDLER_H
#define GAME_SERVER_FOXNET_COSMETICHANDLER_H

#include <base/system.h>

#include <vector>

class CGameContext;
class IServer;

constexpr int MAX_ITEM_STARS = 5;

enum ShopItems
{
	RAINBOW_FEET = 0,
	RAINBOW_BODY,
	RAINBOW_HOOK,
	EMOTICON_GUN,
	PHASE_GUN,
	HEART_GUN,
	MIXED_GUN,
	LASER_GUN,
	INDICATOR_CLOCKWISE,
	INDICATOR_COUNTERCLOCKWISE,
	INDICATOR_INWARD_TURNING,
	INDICATOR_OUTWARD_TURNING,
	INDICATOR_LINE,
	INDICATOR_CRISSCROSS,
	DEATH_EXPLOSIVE,
	DEATH_HAMMERHIT,
	DEATH_INDICATOR,
	DEATH_LASER,
	TRAIL_STAR,
	TRAIL_DOT,
	HAT_HAMMER,
	HAT_GUN,
	HAT_SHOTGUN,
	HAT_GRENADE,
	HAT_LASER,
	HAT_NINJA,
	HAT_HEART,
	OTHER_SPARKLE,
	OTHER_INVERSEAIM,
	OTHER_LOVELY,
	OTHER_ROTATINGBALL,
	VIP,
	MVP,
	LOOT_CASE_COMMON,
	LOOT_CASE_UNCOMMON,
	LOOT_CASE_RARE,
	LOOT_CASE_EPIC,
	LOOT_CASE_EXOTIC,
	NUM_ITEMS
};

constexpr const char *Items[NUM_ITEMS] = {
	"Rainbow Feet",
	"Rainbow Body",
	"Rainbow Hook",

	"Emoticon Gun",
	"Phase Gun",
	"Heart Gun",
	"Mixed Gun",
	"Laser Gun",

	"Clockwise Indicator",
	"Counter Clockwise Indicator",
	"Inward Turning Indicator",
	"Outward Turning Indicator",
	"Line Indicator",
	"Criss Cross Indicator",

	"Explosive Death",
	"Hammer Hit Death",
	"Indicator Death",
	"Laser Death",

	"Star Trail",
	"Dot Trail",

	"Hammer Hat",
	"Gun Hat",
	"Shotgun Hat",
	"Grenade Hat",
	"Laser Hat",
	"Ninja Hat",
	"Heart Hat",

	"Sparkle",
	"Inverse Aim",
	"Lovely",
	"Rotating Ball",

	"VIP",
	"MVP",

	"Loot Case (Common)",
	"Loot Case (Uncommon)",
	"Loot Case (Rare)",
	"Loot Case (Epic)",
	"Loot Case (Exotic)"
};

enum ItemTypes
{
	ITEMTYPE_ROLES,
	ITEMTYPE_CASES,
	ITEMTYPE_RAINBOW,
	ITEMTYPE_GUN,
	ITEMTYPE_INDICATOR,
	ITEMTYPE_DEATHS,
	ITEMTYPE_TRAIL,
	ITEMTYPE_HAT,
	ITEMTYPE_EFFECTS,
	NUM_ITEMTYPES
};

enum ItemSubTypes
{
	SUBTYPE_NONE = 0,
	SUBTYPE_GUN,
	SUBTYPE_IND,
	SUBTYPE_DEATH,
	SUBTYPE_TRAIL,
	SUBTYPE_HAT,
	NUM_SUBTYPES
};

enum ItemRarity
{
	RARITY_COMMON = 0,
	RARITY_UNCOMMON,
	RARITY_RARE,
	RARITY_EPIC,
	RARITY_MYTHIC,
	RARITY_LEGENDARY,
	NUM_RARITIES
};

class CItem
{
	char m_aItem[32] = "";
	char m_aShortcut[32] = "";
	char m_aDescription[60] = "";
	int m_Type = 0;
	int m_SubType = 0;
	int m_Price = 0;
	int m_MinLevel = 0;

	int m_Rarity = 0;
	int m_Stars = 0;

	bool m_Toggleable = true;
	bool m_OneTimeUse = false;

public:
	CItem(const char *pShopItem, const char *pShortcut, int Rarity, int Stars, int ItemType, int Price, const char *pDesc, int MinLevel, int ItemSubType)
	{
		str_copy(m_aItem, pShopItem);
		str_copy(m_aShortcut, pShortcut);
		str_copy(m_aDescription, pDesc);
		m_Type = ItemType;
		m_SubType = ItemSubType;
		m_Price = Price;
		m_MinLevel = MinLevel;
		m_Rarity = Rarity;
		m_Stars = Stars;
	}
	CItem(const char *pShopItem, const char *pShortcut, int Rarity, int Stars, int ItemType, int Price, const char *pDesc, int MinLevel, int ItemSubType, bool Toggleable)
	{
		str_copy(m_aItem, pShopItem);
		str_copy(m_aShortcut, pShortcut);
		str_copy(m_aDescription, pDesc);
		m_Type = ItemType;
		m_SubType = ItemSubType;
		m_Price = Price;
		m_MinLevel = MinLevel;
		m_Rarity = Rarity;
		m_Stars = Stars;
		m_Toggleable = Toggleable;
		if(ItemType == ITEMTYPE_CASES)
			m_OneTimeUse = true;
	}

	const char *Name() const { return m_aItem; }
	const char *Shortcut() const { return m_aShortcut; }
	const char *Description() const { return m_aDescription; }
	int Type() const { return m_Type; }
	int SubType() const { return m_SubType; }

	int Price() const { return m_Price; }
	int MinLevel() const { return m_MinLevel; }

	int Rarity() const { return m_Rarity; }
	int Stars() const { return m_Stars; }

	const char *StarChar() const
	{
		// ★✪✦✹✵✷
		static char StarBuf[16] = "";
		StarBuf[0] = '\0';
		for(int Num = 0; Num < m_Stars; Num++)
			str_append(StarBuf, "★", sizeof(StarBuf));
		for(int Num = m_Stars; Num < MAX_ITEM_STARS; Num++)
			str_append(StarBuf, "☆", sizeof(StarBuf));
		return StarBuf;
	}

	bool IsToggleable() const { return m_Toggleable; }
	bool IsOneTimeUse() const { return m_OneTimeUse; }

	void SetPrice(int Price) { m_Price = Price; }
	void SetMinLevel(int MinLevel) { m_MinLevel = MinLevel; }

	bool operator==(const CItem &Other) const
	{
		bool NameMatch = !str_comp(Name(), Other.Name()) && str_comp(Name(), "") != 0;
		bool ShortcutMatch = !str_comp(Shortcut(), Other.Shortcut()) && str_comp(Shortcut(), "") != 0;
		return NameMatch && ShortcutMatch;
	}
};

class CShop
{
	CGameContext *m_pGameServer = nullptr;
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	void AddItems();

public:


	void ResetItems();

	void ListItems();

	void EditItem(const char *pName, int Price, int MinLevel = -1);

	void BuyItem(int ClientId, const char *pName);
	bool GiveItem(int ClientId, const char *pItemName, bool Bought = true, const char *pFrom = "Server");
	bool GiveItem(int ClientId, const char *pItemName, int Days, const char *pFrom = "Server");
	void RemoveItem(int ClientId, const char *pItemName, const char *pByName = "Server");

	std::vector<CItem *> m_Items;
	CItem *FindItem(const char *pName);

	void Init(CGameContext *pGameServer);

	const CItem *GetRandomItemOfRarity(int Rarity);
};

#endif // GAME_SERVER_FOXNET_COSMETICHANDLER_H