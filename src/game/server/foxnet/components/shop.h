#ifndef GAME_SERVER_FOXNET_COMPONENTS_SHOP_H
#define GAME_SERVER_FOXNET_COMPONENTS_SHOP_H

#include <engine/console.h>

#include <game/server/foxnet/component.h>
#include <game/server/foxnet/item_registry.h>

class CGameContext;
class IServer;

class CShop : public CServerComponent
{
	CItemRegistry m_Registry;

	static void ConListItems(IConsole::IResult *pResult, void *pUserData);
	static void ConEditItem(IConsole::IResult *pResult, void *pUserData);
	static void ConReset(IConsole::IResult *pResult, void *pUserData);

	static void ConGiveItem(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveItemDays(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveItemForever(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveItem(IConsole::IResult *pResult, void *pUserData);

	static void ConBuyItem(IConsole::IResult *pResult, void *pUserData);

public:
	const CItemRegistry &Registry() const { return m_Registry; }

	void AddItems();
	void ResetItems();
	void ListItems();

	void EditItem(const char *pName, int Price, int MinLevel);

	const CItemConfig *FindItem(const char *pName) const { return m_Registry.FindByName(pName); }
	const CItemConfig *FindItem(EItemId Id) const { return m_Registry.FindById(Id); }

	bool BuyItem(CPlayer *pPlayer, const char *pName);
	bool GiveItem(CPlayer *pPlayer, const CItemConfig *pItem, int Days = -1, const char *pFrom = "Server", bool AutoActivate = false);
	bool GiveItem(CPlayer *pPlayer, const char *pName, int Days = -1, const char *pFrom = "Server", bool AutoActivate = false);

	bool RemoveItem(CPlayer *pPlayer, const char *pName, const char *pBy = "Server");

	const CItemConfig *RandomItemByRarity(EItemRarity Rarity, bool AllowAny);

	const char *GetItemName(EItemId Id) const;

	void OnInit() override;
	void OnConsoleInit() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_SHOP_H
