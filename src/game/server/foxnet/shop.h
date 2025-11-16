#ifndef GAME_SERVER_FOXNET_COSMETICHANDLER_H
#define GAME_SERVER_FOXNET_COSMETICHANDLER_H

#include <base/system.h>

#include <vector>

class CGameContext;
class IServer;

enum Cosmetics
{
	RAINBOW_FEET = 0,
	RAINBOW_BODY,
	RAINBOW_HOOK,
	GUN_EMOTICON,
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
};

enum ItemTypes
{
	TYPE_RAINBOW = 0,
	TYPE_GUN,
	TYPE_INDICATOR,
	TYPE_DEATHS,
	TYPE_TRAIL,
	TYPE_HAT,
	TYPE_OTHER,
	NUM_TYPES
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

class CItem
{
	char m_aItem[32] = "";
	char m_aShortcut[32] = "";
	char m_aDescription[60] = "";
	int m_Type = 0;
	int m_SubType = 0;
	int m_Price = 0;
	int m_MinLevel = 0;

public:
	CItem(const char *pShopItem, const char *pShortcut, int pItemType, int pPrice, const char *pDesc, int pMinLevel = 0, int pItemSubType = 0)
	{
		str_copy(m_aItem, pShopItem);
		str_copy(m_aShortcut, pShortcut);
		str_copy(m_aDescription, pDesc);
		m_Type = pItemType;
		m_SubType = pItemSubType;
		m_Price = pPrice;
		m_MinLevel = pMinLevel;
	}

	const char *Name() const { return m_aItem; }
	const char *Shortcut() const { return m_aShortcut; }
	const char *Description() const { return m_aDescription; }
	int Type() const { return m_Type; }
	int SubType() const { return m_SubType; }

	int Price() const { return m_Price; }
	int MinLevel() const { return m_MinLevel; }

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
	void GiveItem(int ClientId, const char *pItemName, bool Bought = true, int FromId = -1);
	void GiveItem(int ClientId, const char *pItemName, int Days);
	void RemoveItem(int ClientId, const char *pItemName, int ById);

	std::vector<CItem *> m_Items;
	CItem *FindItem(const char *pName);

	void Init(CGameContext *pGameServer);
};

#endif // GAME_SERVER_FOXNET_COSMETICHANDLER_H