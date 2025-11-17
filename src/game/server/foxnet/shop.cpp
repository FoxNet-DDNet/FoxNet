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
	m_Items.push_back(new CItem(Items[RAINBOW_FEET], "R_F", RARITY_COMMON, 1, TYPE_RAINBOW, 1250, "Makes your body Rainbow", 1, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[RAINBOW_BODY], "R_B", RARITY_COMMON, 2, TYPE_RAINBOW, 2000, "Makes your feet Rainbow", 4, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[RAINBOW_HOOK], "R_H", RARITY_COMMON, 4, TYPE_RAINBOW, 6500, "Anyone you hook becomes Rainbow!", 5, SUBTYPE_NONE));

	m_Items.push_back(new CItem(Items[EMOTICON_GUN], "G_E", RARITY_UNCOMMON, 2, TYPE_GUN, 3500, "Shoot emotions at people", 10, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[PHASE_GUN], "G_P", RARITY_UNCOMMON, 1, TYPE_GUN, 2250, "Your bullets defy physics", 5, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[HEART_GUN], "G_H", RARITY_EPIC, 1, TYPE_GUN, 20000, "Shoot bullets full of love", 15, SUBTYPE_GUN));
	m_Items.push_back(new CItem(Items[GUNTYPE_MIXED], "G_M", RARITY_EPIC, 2, TYPE_GUN, 25000, "Shoots Hearts and Shields", 25, SUBTYPE_GUN));
	m_Items.push_back(new CItem(Items[GUNTYPE_LASER], "G_L", RARITY_EPIC, 5, TYPE_GUN, 35000, "Lasertag in DDNet?", 25, SUBTYPE_GUN));

	m_Items.push_back(new CItem(Items[INDICATOR_CLOCKWISE], "I_C", RARITY_COMMON, 5, TYPE_INDICATOR, 4500, "Gun Hit -> turns Clockwise", 5, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_COUNTERCLOCKWISE], "I_CC", RARITY_COMMON, 5, TYPE_INDICATOR, 4500, "Gun Hit -> turns Counter-Clockwise", 5, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_INWARD_TURNING], "I_IT", RARITY_UNCOMMON, 4, TYPE_INDICATOR, 8000, "Gun Hit -> turns Inward", 15, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_OUTWARD_TURNING], "I_OT", RARITY_UNCOMMON, 4, TYPE_INDICATOR, 8000, "Gun Hit -> turns Outward", 15, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_LINE], "I_L", RARITY_UNCOMMON, 3, TYPE_INDICATOR, 6500, "Gun Hit -> goes in a Line", 10, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_CRISSCROSS], "I_CrCs", RARITY_UNCOMMON, 4, TYPE_INDICATOR, 6500, "Gun Hit -> goes in a Criss Cross pattern", 10, SUBTYPE_IND));

	m_Items.push_back(new CItem(Items[DEATH_EXPLOSIVE], "D_E", RARITY_UNCOMMON, 2, TYPE_DEATHS, 3250, "Go out with a Boom!", 5, SUBTYPE_DEATH));
	m_Items.push_back(new CItem(Items[DEATH_HAMMERHIT], "D_H", RARITY_UNCOMMON, 2, TYPE_DEATHS, 3250, "Get Bonked on death!", 5, SUBTYPE_DEATH));
	m_Items.push_back(new CItem(Items[DEATHTYPE_DAMAGEIND], "D_I", RARITY_UNCOMMON, 4, TYPE_DEATHS, 7500, "Creates an octagon of damage indicators", 10, SUBTYPE_DEATH));
	m_Items.push_back(new CItem(Items[DEATH_LASER], "D_L", RARITY_EPIC, 4, TYPE_DEATHS, 7500, "Become wizard and summon lasers on death!", 10, SUBTYPE_DEATH));

	m_Items.push_back(new CItem(Items[TRAIL_STAR], "T_S", RARITY_UNCOMMON, 4, TYPE_TRAIL, 8000, "The Stars shall follow you", 7, SUBTYPE_TRAIL));
	m_Items.push_back(new CItem(Items[TRAIL_DOT], "T_D", RARITY_UNCOMMON, 4, TYPE_TRAIL, 8000, "A trail made out of small dots", 7, SUBTYPE_TRAIL));

	m_Items.push_back(new CItem(Items[HAT_HAMMER], "H_O", RARITY_COMMON, 5, TYPE_HAT, 4000, "Hammer above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_GUN], "H_G", RARITY_COMMON, 5, TYPE_HAT, 4000, "Gun above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_SHOTGUN], "H_SG", RARITY_COMMON, 5, TYPE_HAT, 4000, "Shotgun above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_GRENADE], "H_GR", RARITY_COMMON, 5, TYPE_HAT, 4000, "Grenade above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_LASER], "H_L", RARITY_COMMON, 5, TYPE_HAT, 4000, "Laser above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_NINJA], "H_N", RARITY_COMMON, 5, TYPE_HAT, 4000, "Ninja weapon above your head", 5, SUBTYPE_HAT));

	m_Items.push_back(new CItem(Items[HAT_HEART], "H_O", RARITY_EPIC, 2, TYPE_HAT, 25000, "A hat of Hearts", 12, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[OTHER_SPARKLE], "O_S", RARITY_COMMON, 1, TYPE_OTHER, 1500, "Makes you sparkle", 5, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[OTHER_INVERSEAIM], "O_I", RARITY_LEGENDARY, 1, TYPE_OTHER, 50000, "Shows your aim backwards for others!", 35, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[OTHER_LOVELY], "O_L", RARITY_RARE, 3, TYPE_OTHER, 12500, "Spreading love huh?", 15, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[OTHER_ROTATINGBALL], "O_R", RARITY_UNCOMMON, 2, TYPE_OTHER, 12500, "Ball rotate - life good", 15, SUBTYPE_NONE));

	m_Items.push_back(new CItem(Items[VIP], "VIP", RARITY_MYTHIC, 3, TYPE_ROLES, 250000, "VIP Role grants 2.5x boost on xp/money", 40, 0, false));
	m_Items.push_back(new CItem(Items[MVP], "MVP", RARITY_LEGENDARY, 3, TYPE_ROLES, 750000, "MVP Role grants 3.5x boost on xp/money", 65, 0, false));
}

void CShop::ResetItems()
{
	m_Items.clear();
	AddItems();
	ListItems();
}

void CShop::ListItems()
{
	char Separator[128] = "";
	for(int Length = 0; Length < 56; Length++)
		str_append(Separator, "-");

	log_info("shop", "%s", Separator);
	for(CItem *pItem : m_Items)
	{
		if(!str_comp(pItem->Name(), ""))
			continue;
		log_info("shop", "%s | Price: %d | MinLevel: %d", pItem->Name(), pItem->Price(), pItem->MinLevel());
	}
	log_info("shop", "%s", Separator);
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
	if(!pPl->CanUseMoney())
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ You cannot use Money right now");
		GameServer()->SendChatTarget(ClientId, "│ Try again later");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return;
	}

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

	if(pItem->SubType() == SUBTYPE_HAT)
		GameServer()->SendChatTarget(ClientId, "Hats can be rotated! Head to the settings section to change the rotation");
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

void CShop::GiveItem(int ClientId, const char *pItemName, int Days)
{
	CItem *pItem = FindItem(pItemName);
	CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	if(!pItem)
	{
		log_info("shop", "Tried to give non-existing item '%s' to ClientId %d", pItemName, ClientId);
		return;
	}
	const char *pName = pItem->Name();
	if(!pAcc->m_LoggedIn)
	{
		log_info("shop", "Tried to give item '%s' to non-logged-in ClientId %d", pName, ClientId);
		return;
	}
	int Index = CInventory::IndexOfName(pName);

	int64_t Now = time(0);
	const int64_t NumDays = int64_t(Days) * 86400; // 30 days

	if(GameServer()->m_apPlayers[ClientId]->OwnsItem(pName))
	{
		pAcc->m_Inventory.AddToExpiry(Index, NumDays);
	}
	else
	{
		pAcc->m_Inventory.SetAcquiredAt(Index, Now);
		pAcc->m_Inventory.SetExpiresAt(Index, Now + NumDays);
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