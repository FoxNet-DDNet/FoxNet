#ifndef GAME_SERVER_FOXNET_COSMETICHANDLER_H
#define GAME_SERVER_FOXNET_COSMETICHANDLER_H

#include <base/system.h>

#include <vector>
#include "item_registry.h"

class CGameContext;
class IServer;

class CShop
{
	CGameContext *m_pGameServer = nullptr;
	CItemRegistry m_Registry;
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

public:
	void Init(CGameContext *pGameServer);

	const CItemRegistry &Registry() const { return m_Registry; }

	void AddItems();
	void ResetItems();
	void ListItems();

	void EditItem(const char *pName, int Price, int MinLevel);

	const CItemConfig *FindItem(const char *pName) const { return m_Registry.FindByName(pName); }
	const CItemConfig *FindItem(EItemId Id) const { return m_Registry.FindById(Id); }

	bool BuyItem(int ClientId, const char *pName);
	bool GiveItem(int ClientId, const CItemConfig *pItem, int Days = -1, const char *pFrom = "Server");
	bool GiveItem(int ClientId, const char *pName, int Days = -1, const char *pFrom = "Server");

	bool GiveItemForever(int ClientId, const CItemConfig *pItem, const char *pFrom = "Server");
	bool GiveItemForever(int ClientId, const char *pName, const char *pFrom = "Server");

	bool RemoveItem(int ClientId, const char *pName, const char *pBy = "Server");

	const CItemConfig *RandomItemByRarity(EItemRarity Rarity, bool AllowAny);

	const char *GetItemName(EItemId Id) const;
};

#endif // GAME_SERVER_FOXNET_COSMETICHANDLER_H