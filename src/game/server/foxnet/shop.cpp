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
	m_Items.push_back(new CItem(Items[RAINBOW_FEET], "R_F", RARITY_COMMON, 1, ITEMTYPE_RAINBOW, 1250, "Makes your body Rainbow", 1, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[RAINBOW_BODY], "R_B", RARITY_COMMON, 2, ITEMTYPE_RAINBOW, 2000, "Makes your feet Rainbow", 4, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[RAINBOW_HOOK], "R_H", RARITY_UNCOMMON, 1, ITEMTYPE_RAINBOW, 6500, "Anyone you hook becomes Rainbow!", 5, SUBTYPE_NONE));
	
	m_Items.push_back(new CItem(Items[OTHER_SPARKLE], "O_S", RARITY_COMMON, 1, ITEMTYPE_EFFECTS, 1500, "Makes you sparkle", 5, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[OTHER_LOVELY], "O_L", RARITY_RARE, 2, ITEMTYPE_EFFECTS, 12500, "Spreading love huh?", 15, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[OTHER_INVERSEAIM], "O_I", RARITY_LEGENDARY, 1, ITEMTYPE_EFFECTS, 50000, "Shows your aim backwards for others!", 35, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[OTHER_ROTATINGBALL], "O_R", RARITY_RARE, 2, ITEMTYPE_EFFECTS, 12500, "Ball rotate - life good", 15, SUBTYPE_NONE));

	m_Items.push_back(new CItem(Items[EMOTICON_GUN], "G_E", RARITY_UNCOMMON, 4, ITEMTYPE_GUN, 7500, "Shoot emotions at people", 10, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[PHASE_GUN], "G_P", RARITY_UNCOMMON, 2, ITEMTYPE_GUN, 3250, "Your bullets defy physics", 5, SUBTYPE_NONE));
	m_Items.push_back(new CItem(Items[HEART_GUN], "G_H", RARITY_EPIC, 1, ITEMTYPE_GUN, 20000, "Shoot bullets full of love", 15, SUBTYPE_GUN));
	m_Items.push_back(new CItem(Items[MIXED_GUN], "G_M", RARITY_EPIC, 2, ITEMTYPE_GUN, 25000, "Shoots Hearts and Shields", 25, SUBTYPE_GUN));
	m_Items.push_back(new CItem(Items[LASER_GUN], "G_L", RARITY_EPIC, 5, ITEMTYPE_GUN, 35000, "Lasertag in DDNet?", 25, SUBTYPE_GUN));

	m_Items.push_back(new CItem(Items[INDICATOR_CLOCKWISE], "I_C", RARITY_COMMON, 5, ITEMTYPE_INDICATOR, 4500, "Gun Hit -> turns Clockwise", 5, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_COUNTERCLOCKWISE], "I_CC", RARITY_COMMON, 5, ITEMTYPE_INDICATOR, 4500, "Gun Hit -> turns Counter-Clockwise", 5, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_INWARD_TURNING], "I_IT", RARITY_UNCOMMON, 4, ITEMTYPE_INDICATOR, 8000, "Gun Hit -> turns Inward", 15, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_OUTWARD_TURNING], "I_OT", RARITY_UNCOMMON, 4, ITEMTYPE_INDICATOR, 8000, "Gun Hit -> turns Outward", 15, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_LINE], "I_L", RARITY_UNCOMMON, 3, ITEMTYPE_INDICATOR, 6500, "Gun Hit -> goes in a Line", 10, SUBTYPE_IND));
	m_Items.push_back(new CItem(Items[INDICATOR_CRISSCROSS], "I_CrCs", RARITY_UNCOMMON, 4, ITEMTYPE_INDICATOR, 6500, "Gun Hit -> goes in a Criss Cross pattern", 10, SUBTYPE_IND));

	m_Items.push_back(new CItem(Items[DEATH_EXPLOSIVE], "D_E", RARITY_UNCOMMON, 2, ITEMTYPE_DEATHS, 3250, "Go out with a Boom!", 5, SUBTYPE_DEATH));
	m_Items.push_back(new CItem(Items[DEATH_HAMMERHIT], "D_H", RARITY_UNCOMMON, 2, ITEMTYPE_DEATHS, 3250, "Get Bonked on death!", 5, SUBTYPE_DEATH));
	m_Items.push_back(new CItem(Items[DEATH_INDICATOR], "D_I", RARITY_UNCOMMON, 4, ITEMTYPE_DEATHS, 7500, "Creates an octagon of damage indicators", 10, SUBTYPE_DEATH));
	m_Items.push_back(new CItem(Items[DEATH_LASER], "D_L", RARITY_UNCOMMON, 4, ITEMTYPE_DEATHS, 7500, "Become wizard and summon lasers on death!", 10, SUBTYPE_DEATH));

	m_Items.push_back(new CItem(Items[TRAIL_STAR], "T_S", RARITY_UNCOMMON, 4, ITEMTYPE_TRAIL, 8000, "The Stars shall follow you", 7, SUBTYPE_TRAIL));
	m_Items.push_back(new CItem(Items[TRAIL_DOT], "T_D", RARITY_UNCOMMON, 4, ITEMTYPE_TRAIL, 8000, "A trail made out of small dots", 7, SUBTYPE_TRAIL));

	m_Items.push_back(new CItem(Items[HAT_HAMMER], "H_O", RARITY_COMMON, 5, ITEMTYPE_HAT, 4000, "Hammer above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_GUN], "H_G", RARITY_COMMON, 5, ITEMTYPE_HAT, 4000, "Gun above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_SHOTGUN], "H_SG", RARITY_COMMON, 5, ITEMTYPE_HAT, 4000, "Shotgun above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_GRENADE], "H_GR", RARITY_COMMON, 5, ITEMTYPE_HAT, 4000, "Grenade above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_LASER], "H_L", RARITY_COMMON, 5, ITEMTYPE_HAT, 4000, "Laser above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_NINJA], "H_N", RARITY_COMMON, 5, ITEMTYPE_HAT, 4000, "Ninja weapon above your head", 5, SUBTYPE_HAT));
	m_Items.push_back(new CItem(Items[HAT_HEART], "H_O", RARITY_RARE, 3, ITEMTYPE_HAT, 15000, "A hat of Hearts", 12, SUBTYPE_HAT));

	m_Items.push_back(new CItem(Items[VIP], "VIP", RARITY_MYTHIC, 2, ITEMTYPE_ROLES, 200000, "VIP Role grants a 2.5x boost on xp/money", 40, 0, false));
	m_Items.push_back(new CItem(Items[MVP], "MVP", RARITY_LEGENDARY, 2, ITEMTYPE_ROLES, 650000, "MVP Role grants a 3.5x boost on xp/money", 65, 0, false));

	m_Items.push_back(new CItem(Items[LOOT_CASE_COMMON], "LC_C", RARITY_COMMON, 5, ITEMTYPE_CASES, 5000, "Gives you a random common item!", 10, SUBTYPE_NONE, false));
	m_Items.push_back(new CItem(Items[LOOT_CASE_UNCOMMON], "LC_UC", RARITY_UNCOMMON, 5, ITEMTYPE_CASES, 10000, "Gives you a random uncommon item!", 15, SUBTYPE_NONE, false));
	m_Items.push_back(new CItem(Items[LOOT_CASE_RARE], "LC_R", RARITY_RARE, 5, ITEMTYPE_CASES, 20000, "Gives you a random rare item!", 25, SUBTYPE_NONE, false));
	m_Items.push_back(new CItem(Items[LOOT_CASE_EPIC], "LC_E", RARITY_EPIC, 5, ITEMTYPE_CASES, 40000, "Gives you a random epic item!", 35, SUBTYPE_NONE, false));
	m_Items.push_back(new CItem(Items[LOOT_CASE_EXOTIC], "LC_Ex", RARITY_LEGENDARY, 5, ITEMTYPE_CASES, 250000, "Gives you a random item of any type!", 40, SUBTYPE_NONE, false));
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

bool CShop::GiveItem(int ClientId, const char *pItemName, bool Bought, const char *pFrom)
{
	CItem *pItem = FindItem(pItemName);
	if(!pItem)
	{
		log_info("shop", "Tried to give non-existing item '%s' to ClientId %d", pItemName, ClientId);
		return false;
	}

	const char *pName = pItem->Name();

	CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	if(!pAcc->m_LoggedIn)
	{
		log_info("shop", "Tried to give item '%s' to non-logged-in ClientId %d", pName, ClientId);
		return false;
	}
	const char *ClientIdName = Server()->ClientName(ClientId);
	if(Bought)
		log_info("shop", "%s (%d) Bought Item '%s'", ClientIdName, ClientId, pName);
	else
		log_info("shop", "%s (%d) Received Item '%s' (30 days) from: %s", ClientIdName, ClientId, pName, pFrom);

	int Index = CInventory::IndexOfName(pName);
	if(Index == -1)
		return false;

	int64_t Now = time(0);

	if(!GameServer()->m_apPlayers[ClientId]->OwnsItem(pName))
		pAcc->m_Inventory.SetAcquiredAt(Index, Now);

	if(pItem->IsOneTimeUse())
	{
		pAcc->m_Inventory.m_aQuantity[Index] += 1;
		pAcc->m_Inventory.SetExpiresAt(Index, -1); // Never expires
	}
	else
	{
		const int64_t days = int64_t(30) * 86400; // 30 days
		if(GameServer()->m_apPlayers[ClientId]->OwnsItem(pName))
			pAcc->m_Inventory.AddToExpiry(Index, days);
		else
			pAcc->m_Inventory.SetExpiresAt(Index, Now + days);
		pAcc->m_Inventory.SetQuantityIndex(Index, 1); // Can only have 1 of these
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, GameServer()->m_aAccounts[ClientId]);
	return true;
}

bool CShop::GiveItem(int ClientId, const char *pItemName, int Days, const char *pFrom)
{
	CItem *pItem = FindItem(pItemName);
	CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	if(!pItem)
	{
		log_info("shop", "Tried to give non-existing item '%s' to ClientId %d", pItemName, ClientId);
		return false;
	}
	const char *pName = pItem->Name();
	if(!pAcc->m_LoggedIn)
	{
		log_info("shop", "Tried to give item '%s' to non-logged-in ClientId %d", pName, ClientId);
		return false;
	}
	log_info("shop", "%s (%d) Received Item '%s' (%d days) from: %s", Server()->ClientName(ClientId), ClientId, pName, Days, pFrom);

	int Index = CInventory::IndexOfName(pName);

	int64_t Now = time(0);
	const int64_t NumDays = int64_t(Days) * 86400; // n days

	if(GameServer()->m_apPlayers[ClientId]->OwnsItem(pName))
	{
		pAcc->m_Inventory.AddToExpiry(Index, NumDays);
	}
	else
	{
		pAcc->m_Inventory.SetAcquiredAt(Index, Now);
		pAcc->m_Inventory.SetExpiresAt(Index, Now + NumDays);
	}
	pAcc->m_Inventory.SetQuantityIndex(Index, true);

	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, GameServer()->m_aAccounts[ClientId]);
	return true;
}

void CShop::RemoveItem(int ClientId, const char *pItemName, const char *pByName)
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
	log_info("shop", "%s removed Item '%s' from %s (%d)", pByName, pName, ClientIdName, ClientId);

	int ItemIndex = CInventory::IndexOfName(pName);
	pAcc->m_Inventory.SetEquippedIndex(ItemIndex, false);
	pAcc->m_Inventory.SetQuantityIndex(ItemIndex, false);
	pAcc->m_Inventory.SetAcquiredAt(ItemIndex, 0);
	pAcc->m_Inventory.SetExpiresAt(ItemIndex, 0);
	pPl->UseItem(Items[ItemIndex], false); // Disable Item
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

const CItem *CShop::GetRandomItemOfRarity(int Rarity)
{
	std::vector<const CItem *> ItemsOfRarity;
	for(const CItem *pItem : m_Items)
	{
		if(pItem->Type() == ITEMTYPE_CASES)
			continue;

		if(pItem->Rarity() != Rarity && Rarity != NUM_RARITIES)
			continue;

		ItemsOfRarity.push_back(pItem);
	}

	if(ItemsOfRarity.empty())
		return nullptr;

	std::mt19937 RandGen(Server()->Tick());
	std::uniform_int_distribution<size_t> Dist(0, ItemsOfRarity.size() - 1);
	return ItemsOfRarity[Dist(RandGen)];
}