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
	m_Items.push_back(new CItems("Rainbow Feet", TYPE_RAINBOW, 1750, "Makes your body Rainbow", 2));
	m_Items.push_back(new CItems("Rainbow Body", TYPE_RAINBOW, 2450, "Makes your feet Rainbow", 4));
	m_Items.push_back(new CItems("Rainbow Hook", TYPE_RAINBOW, 6500, "Anyone you hook becomes Rainbow!", 5));

	m_Items.push_back(new CItems("Emoticon Gun", TYPE_GUN, 5500, "Shoot emotions at people", 10));
	// m_Items.push_back(new CItems("Confetti Gun", TYPE_GUN,  2000,"Party gun!", 10));
	m_Items.push_back(new CItems("Phase Gun", TYPE_GUN, 3250, "Your bullets defy physics", 5));

	m_Items.push_back(new CItems("Clockwise Indicator", TYPE_INDICATOR, 5500, "Gun Hit -> turns Clockwise", 5));
	m_Items.push_back(new CItems("Counter Clockwise Indicator", TYPE_INDICATOR, 5500, "Gun Hit -> turns Counter-Clockwise", 5));
	m_Items.push_back(new CItems("Inward Turning Indicator", TYPE_INDICATOR, 10000, "Gun Hit -> turns Inward", 15));
	m_Items.push_back(new CItems("Outward Turning Indicator", TYPE_INDICATOR, 10000, "Gun Hit -> turns Outward", 15));
	m_Items.push_back(new CItems("Line Indicator", TYPE_INDICATOR, 7500, "Gun Hit -> goes in a Line", 10));
	m_Items.push_back(new CItems("Criss Cross Indicator", TYPE_INDICATOR, 7500, "Gun Hit -> goes in a Criss Cross pattern", 10));

	m_Items.push_back(new CItems("Explosive Death", TYPE_DEATHS, 5250, "Go out with a Boom!", 5));
	m_Items.push_back(new CItems("Hammer Hit Death", TYPE_DEATHS, 5250, "Get Bonked on death!", 5));
	m_Items.push_back(new CItems("Indicator Death", TYPE_DEATHS, 7750, "Creates an octagon of damage indicators", 10));
	m_Items.push_back(new CItems("Laser Death", TYPE_DEATHS, 7750, "Become wizard and summon lasers on death", 10));

	m_Items.push_back(new CItems("Star Trail", TYPE_TRAIL, 10500, "The Stars follow you", 7));
	m_Items.push_back(new CItems("Dot Trail", TYPE_TRAIL, 10500, "A trail made out of small dots", 7));

	m_Items.push_back(new CItems("Sparkle", TYPE_OTHER, 2500, "Makes you sparkle", 5));
	m_Items.push_back(new CItems("Heart Hat", TYPE_OTHER, 15000, "A hat of hearts?", 10));
	m_Items.push_back(new CItems("Inverse Aim", TYPE_OTHER, 50000, "Shows your aim backwards to others!", 20));
	m_Items.push_back(new CItems("Lovely", TYPE_OTHER, 15750, "Spreading love huh?", 15));
	m_Items.push_back(new CItems("Rotating Ball", TYPE_OTHER, 15500, "Ball rotate - life good", 15));
	// m_Items.push_back(new CItems("Epic Circle", TYPE_OTHER, 3500, "Really epic circle", 20));
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
	for(CItems *pItem : m_Items)
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

	for(CItems *pItem : m_Items)
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

int CShop::GetItemPrice(const char *pName)
{
	for(CItems *pItem : m_Items)
	{
		if(!str_comp(pItem->Name(), ""))
			continue;

		if(!str_comp_nocase(pName, pItem->Name()) || !str_comp_nocase(ShortcutToName(pName), pItem->Name()))
		{
			return pItem->Price();
		}
	}

	return -1;
}

int CShop::GetItemMinLevel(const char *pName)
{
	for(CItems *pItem : m_Items)
	{
		if(!str_comp(pItem->Name(), ""))
			continue;

		if(!str_comp_nocase(pName, pItem->Name()) || !str_comp_nocase(ShortcutToName(pName), pItem->Name()))
		{
			return pItem->MinLevel();
		}
	}

	return -1;
}

const char *CShop::NameToShortcut(const char *pName)
{
	int Index = 0;
	for(const char *pItem : Items)
	{
		if(!str_comp_nocase(pItem, pName))
		{
			return ItemShortcuts[Index];
		}
		Index++;
	}

	return "";
}

const char *CShop::ShortcutToName(const char *pShortcut)
{
	int Index = 0;
	for(const char *pItemShortcut : ItemShortcuts)
	{
		if(!str_comp_nocase(pItemShortcut, pShortcut))
		{
			return Items[Index];
		}
		Index++;
	}

	return "";
}

void CShop::BuyItem(int ClientId, const char *pName)
{
	CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	int Price = GetItemPrice(pName);
	int MinLevel = GetItemMinLevel(pName);

	if(!pAcc->m_LoggedIn)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ You aren't logged in");
		GameServer()->SendChatTarget(ClientId, "│ 1 - /register <Username> <Pw> <Pw>");
		GameServer()->SendChatTarget(ClientId, "│ 2 - /Login <Username> <Pw>");
		GameServer()->SendChatTarget(ClientId, "╰─────────────────────────────");
		return;
	}
	else if(GameServer()->m_apPlayers[ClientId]->OwnsItem(pName))
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ You already own that Item!");
		GameServer()->SendChatTarget(ClientId, "│ No refunds");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────────");
		return;
	}
	else if(Price == -1)
	{
		// This is used to completely disable an Item, also if it has already been bought
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ Invalid Item!");
		GameServer()->SendChatTarget(ClientId, "│ Check out the Voting Menu");
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
		GameServer()->SendChatTarget(ClientId, "│ Our systems are dying");
		GameServer()->SendChatTarget(ClientId, "│ Try again in a sec");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return;
	}

	CPlayer *pPl = GameServer()->m_apPlayers[ClientId];
	if(!pPl)
		return;

	char aBuf[256];

	char ItemName[64];
	str_copy(ItemName, pName);
	if(str_comp(ShortcutToName(ItemName), "") != 0)
		str_copy(ItemName, ShortcutToName(ItemName));

	if(pAcc->m_Money < Price)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		str_format(aBuf, sizeof(aBuf), "│ You don't have enough Money to buy %s", ItemName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ You need atleast %d%s", Price, g_Config.m_SvCurrencyName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return;
	}
	if(pAcc->m_Level < MinLevel)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		str_format(aBuf, sizeof(aBuf), "│ You need atleast Level %d to buy %s", MinLevel, ItemName);
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

	pAcc->m_Inventory.SetOwnedIndex(CInventory::IndexOf(ItemName), true);

	GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
	str_format(aBuf, sizeof(aBuf), "│ You bought \"%s\" for %d%s", ItemName, Price, g_Config.m_SvCurrencyName);
	GameServer()->SendChatTarget(ClientId, aBuf);
	str_format(aBuf, sizeof(aBuf), "│ You now have: %ld%s", pAcc->m_Money, g_Config.m_SvCurrencyName);
	GameServer()->SendChatTarget(ClientId, aBuf);
	GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, GameServer()->m_aAccounts[ClientId]);
}