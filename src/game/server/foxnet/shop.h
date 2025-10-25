#ifndef GAME_SERVER_FOXNET_COSMETICHANDLER_H
#define GAME_SERVER_FOXNET_COSMETICHANDLER_H

#include <base/system.h>

#include <vector>

class CGameContext;
class IServer;

constexpr int NUM_ITEMS = 25;
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

	"Sparkle",
	"Heart Hat",
	"Inverse Aim",
	"Lovely",
	"Rotating Ball",
};

enum Cosmetics
{
	C_RAINBOW_FEET = 0,
	C_RAINBOW_BODY,
	C_RAINBOW_HOOK,
	C_GUN_EMOTICON,
	C_PHASE_GUN,
	C_HEART_GUN,
	C_MIXED_GUN,
	C_LASER_GUN,
	C_INDICATOR_CLOCKWISE,
	C_INDICATOR_COUNTERCLOCKWISE,
	C_INDICATOR_INWARD_TURNING,
	C_INDICATOR_OUTWARD_TURNING,
	C_INDICATOR_LINE,
	C_INDICATOR_CRISSCROSS,
	C_DEATH_EXPLOSIVE,
	C_DEATH_HAMMERHIT,
	C_DEATH_INDICATOR,
	C_DEATH_LASER,
	C_TRAIL_STAR,
	C_TRAIL_DOT,
	C_OTHER_SPARKLE,
	C_OTHER_HEARTHAT,
	C_OTHER_INVERSEAIM,
	C_OTHER_LOVELY,
	C_OTHER_ROTATINGBALL,
	NUM_COSMETICS
};

enum ItemTypes
{
	TYPE_RAINBOW = 0,
	TYPE_GUN,
	TYPE_INDICATOR,
	TYPE_DEATHS,
	TYPE_TRAIL,
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
	void RemoveItem(int ClientId, const char *pItemName, int ById);

	std::vector<CItem *> m_Items;
	CItem *FindItem(const char *pName);

	void Init(CGameContext *pGameServer);
};

#endif // GAME_SERVER_FOXNET_COSMETICHANDLER_H