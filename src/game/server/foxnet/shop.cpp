#include "shop.h"

#include "accounts.h"
#include "game/server/gamecontext.h"
#include "game/server/player.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <vector>

IServer *CShop::Server() const { return GameServer()->Server(); }

void CShop::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	if(m_Items.empty())
		AddItems();
}

void CShop::AddItems()
{
	m_Items.push_back(new CItem("Rainbow Feet", "R_F", TYPE_RAINBOW, 1250, "Makes your body Rainbow", 1));
	m_Items.push_back(new CItem("Rainbow Body", "R_B", TYPE_RAINBOW, 2000, "Makes your feet Rainbow", 4));
	m_Items.push_back(new CItem("Rainbow Hook", "R_H", TYPE_RAINBOW, 6500, "Anyone you hook becomes Rainbow!", 5));

	m_Items.push_back(new CItem("Emoticon Gun", "G_E", TYPE_GUN, 3500, "Shoot emotions at people", 10));
	m_Items.push_back(new CItem("Phase Gun", "G_P", TYPE_GUN, 2250, "Your bullets defy physics", 5));
	m_Items.push_back(new CItem("Heart Gun", "G_H", TYPE_GUN, 20000, "Shoot bullets full of love", 15, SUBTYPE_GUN));
	m_Items.push_back(new CItem("Mixed Gun", "G_M", TYPE_GUN, 25000, "Shoots Hearts and Shields", 25, SUBTYPE_GUN));
	m_Items.push_back(new CItem("Laser Gun", "G_L", TYPE_GUN, 35000, "Lasertag in DDNet?", 25, SUBTYPE_GUN));

	m_Items.push_back(new CItem("Clockwise Indicator", "I_C", TYPE_INDICATOR, 4500, "Gun Hit -> turns Clockwise", 5, SUBTYPE_IND));
	m_Items.push_back(new CItem("Counter Clockwise Indicator", "I_CC", TYPE_INDICATOR, 4500, "Gun Hit -> turns Counter-Clockwise", 5, SUBTYPE_IND));
	m_Items.push_back(new CItem("Inward Turning Indicator", "I_IT", TYPE_INDICATOR, 8000, "Gun Hit -> turns Inward", 15, SUBTYPE_IND));
	m_Items.push_back(new CItem("Outward Turning Indicator", "I_OT", TYPE_INDICATOR, 8000, "Gun Hit -> turns Outward", 15, SUBTYPE_IND));
	m_Items.push_back(new CItem("Line Indicator", "I_L", TYPE_INDICATOR, 6500, "Gun Hit -> goes in a Line", 10, SUBTYPE_IND));
	m_Items.push_back(new CItem("Criss Cross Indicator", "I_CrCs", TYPE_INDICATOR, 6500, "Gun Hit -> goes in a Criss Cross pattern", 10, SUBTYPE_IND));

	m_Items.push_back(new CItem("Explosive Death", "D_E", TYPE_DEATHS, 3250, "Go out with a Boom!", 5, SUBTYPE_DEATH));
	m_Items.push_back(new CItem("HammerHit Death", "D_H", TYPE_DEATHS, 3250, "Get Bonked on death!", 5, SUBTYPE_DEATH));
	m_Items.push_back(new CItem("Indicator Death", "D_I", TYPE_DEATHS, 7500, "Creates an octagon of damage indicators", 10, SUBTYPE_DEATH));
	m_Items.push_back(new CItem("Laser Death", "D_L", TYPE_DEATHS, 7500, "Become wizard and summon lasers on death!", 10, SUBTYPE_DEATH));

	m_Items.push_back(new CItem("Star Trail", "T_S", TYPE_TRAIL, 8000, "The Stars shall follow you", 7, SUBTYPE_TRAIL));
	m_Items.push_back(new CItem("Dot Trail", "T_D", TYPE_TRAIL, 8000, "A trail made out of small dots", 7, SUBTYPE_TRAIL));

	m_Items.push_back(new CItem("Sparkle", "O_S", TYPE_OTHER, 1500, "Makes you sparkle", 5));
	m_Items.push_back(new CItem("Heart Hat", "O_H", TYPE_OTHER, 12000, "A hat of hearts?", 10));
	m_Items.push_back(new CItem("Inverse Aim", "O_I", TYPE_OTHER, 50000, "Shows your aim backwards for others!", 35));
	m_Items.push_back(new CItem("Lovely", "O_L", TYPE_OTHER, 12500, "Spreading love huh?", 15));
	m_Items.push_back(new CItem("Rotating Ball", "O_R", TYPE_OTHER, 12500, "Ball rotate - life good", 15));
}

void CShop::ResetItems()
{
	m_Items.clear();
	AddItems();
	ListItems();
}

void CShop::ListItems()
{
	char Seperator[128] = "";
	for(int Length = 0; Length < 56; Length++)
		str_append(Seperator, "-");

	log_info("shop", "%s", Seperator);
	for(CItem *pItem : m_Items)
	{
		if(!str_comp(pItem->Name(), ""))
			continue;
		log_info("shop", "%s | Price: %d | MinLevel: %d", pItem->Name(), pItem->Price(), pItem->MinLevel());
	}
	log_info("shop", "%s", Seperator);
}

void CShop::EditItem(const char *pName, int Price, int MinLevel)
{
	char aBuf[128];
	bool Found = false;

	for(CItem *pItem : m_Items)
	{
		if(str_comp_nocase(pItem->Name(), pName) == 0)
		{
			pItem->SetPrice(Price);
			if(MinLevel >= 0)
				pItem->SetMinLevel(MinLevel);

			if(MinLevel >= 0)
				str_format(aBuf, sizeof(aBuf), "Set price of \"%s\" to %d", pName, Price);
			else
				str_format(aBuf, sizeof(aBuf), "Set price of \"%s\" to %d and Min Level to %d", pName, Price, MinLevel);

			Found = true;
			break;
		}
	}

	if(!Found)
		str_format(aBuf, sizeof(aBuf), "Couldn't find \"%s\"", pName);

	log_info("Shop", "%s", aBuf);
}

void CShop::BuyItem(int ClientId, const char *pName)
{
	CItem *pItem = FindItem(pName);
	if(!pItem)
		return;

	CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	int Price = pItem->Price();
	int MinLevel = pItem->MinLevel();

	if(!pAcc->m_LoggedIn)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ You aren't logged in");
		GameServer()->SendChatTarget(ClientId, "│ 1 - /register <Username> <Pw> <Pw>");
		GameServer()->SendChatTarget(ClientId, "│ 2 - /Login <Username> <Pw>");
		GameServer()->SendChatTarget(ClientId, "╰─────────────────────────────");
		return;
	}
	else if(Price == -1)
	{
		// This is used to completely disable an Item, also if it has already been bought
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ Invalid Item!");
		GameServer()->SendChatTarget(ClientId, "│ Check out the Vote Menu");
		GameServer()->SendChatTarget(ClientId, "│ to see all available Items");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────────");
		return;
	}
	else if(Price == 0)
	{
		// Can be used for seasonal Items, Items can still be toggled
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ Item is out of stock!");
		GameServer()->SendChatTarget(ClientId, "│ Ask an Admin to add it");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return;
	}
	else if(Price < -1)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ Something is wrong with the item.");
		GameServer()->SendChatTarget(ClientId, "╰─────────────────────────");
		return;
	}

	CPlayer *pPl = GameServer()->m_apPlayers[ClientId];
	if(!pPl)
		return;

	char aBuf[256];

	if(pAcc->m_Money < Price)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		str_format(aBuf, sizeof(aBuf), "│ You don't have enough Money to buy %s", pItem->Name());
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ You need atleast %d%s", Price, g_Config.m_SvCurrencyName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return;
	}
	if(pAcc->m_Level < MinLevel)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		str_format(aBuf, sizeof(aBuf), "│ You need atleast Level %d to buy %s", MinLevel, pItem->Name());
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ You are currently Level %ld", pAcc->m_Level);
		GameServer()->SendChatTarget(ClientId, aBuf);
		GameServer()->SendChatTarget(ClientId, "│");
		GameServer()->SendChatTarget(ClientId, "│ Level up by playing");
		GameServer()->SendChatTarget(ClientId, "│ or finishing Maps");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return;
	}

	pPl->TakeMoney(Price);
	GiveItem(ClientId, pItem->Name());

	GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
	str_format(aBuf, sizeof(aBuf), "│ You bought \"%s\" for %d%s", pItem->Name(), Price, g_Config.m_SvCurrencyName);
	GameServer()->SendChatTarget(ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "│ You now have: %ld%s", pAcc->m_Money, g_Config.m_SvCurrencyName);
	GameServer()->SendChatTarget(ClientId, aBuf);
	GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
}

void CShop::GiveItem(int ClientId, const char *pItemName, bool Bought, int FromId)
{
	CItem *pItem = FindItem(pItemName);
	if(!pItem)
	{
		log_info("shop", "Tried to give non-existing item '%s' to ClientId %d", pItemName, ClientId);
		return;
	}

	const char *pName = pItem->Name();

	CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	if(!pAcc->m_LoggedIn)
	{
		log_info("shop", "Tried to give item '%s' to non-logged-in ClientId %d", pName, ClientId);
		return;
	}
	const char *ClientIdName = Server()->ClientName(ClientId);
	if(Bought)
	{
		log_info("shop", "%s (%d) Bought Item '%s'", ClientIdName, ClientId, pName);
	}
	else if(FromId == -1)
	{
		log_info("shop", "%s (%d) Received Item '%s'", ClientIdName, ClientId, pName);
	}
	else if(FromId >= 0)
	{
		const char *FromName = Server()->ClientName(FromId);
		log_info("shop", "%s (%d) Gave Item '%s' to %s (%d)", FromName, FromId, pName, ClientIdName, ClientId);
	}
	int Index = CInventory::IndexOfName(pName);

	int64_t Now = time(0);
	const int64_t days = int64_t(30) * 86400; // 30 days

	if(GameServer()->m_apPlayers[ClientId]->OwnsItem(pName))
	{
		pAcc->m_Inventory.AddToExpiry(Index, days);
	}
	else
	{
		pAcc->m_Inventory.SetAcquiredAt(Index, Now);
		pAcc->m_Inventory.SetExpiresAt(Index, Now + days);
	}
	pAcc->m_Inventory.SetOwnedIndex(Index, true);

	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, GameServer()->m_aAccounts[ClientId]);
}

void CShop::RemoveItem(int ClientId, const char *pItemName, int ById)
{
	CPlayer *pPl = GameServer()->m_apPlayers[ClientId];
	if(!pPl)
		return;

	CItem *pItem = FindItem(pItemName);
	if(!pItem)
	{
		log_info("shop", "Tried to remove non-existing item '%s' from ClientId %d", pItemName, ClientId);
		return;
	}
	const char *pName = pItem->Name();

	CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	if(!pAcc->m_LoggedIn)
	{
		log_info("shop", "Tried to remove item '%s' to non-logged-in ClientId %d", pName, ClientId);
		return;
	}
	const char *ClientIdName = Server()->ClientName(ClientId);
	if(ById < 0)
	{
		log_info("shop", "%s (%d) removed Item '%s'", ClientIdName, ClientId, pName);
	}
	else
	{
		const char *FromName = Server()->ClientName(ById);
		log_info("shop", "%s (%d) removed Item '%s' from %s (%d)", FromName, ById, pName, ClientIdName, ClientId);
	}

	int ItemIndex = CInventory::IndexOfName(pName);
	pAcc->m_Inventory.SetEquippedIndex(ItemIndex, false);
	pAcc->m_Inventory.SetOwnedIndex(ItemIndex, false);
	pAcc->m_Inventory.SetAcquiredAt(ItemIndex, 0);
	pAcc->m_Inventory.SetExpiresAt(ItemIndex, 0);
	pPl->ToggleItem(Items[ItemIndex], false); // Disable Item
	GameServer()->m_AccountManager.RemoveItem(pAcc->m_aUsername, Items[ItemIndex]);
}

CItem *CShop::FindItem(const char *pName)
{
	for(CItem *pItem : m_Items)
	{
		if(!str_comp_nocase(pItem->Name(), pName) || !str_comp_nocase(pItem->Shortcut(), pName))
			return pItem;
	}
	return nullptr;
}