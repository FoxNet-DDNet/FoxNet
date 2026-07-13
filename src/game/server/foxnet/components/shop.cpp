#include "shop.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/foxnet/components/accounts/accounts.h>
#include <game/server/foxnet/item_registry.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <cinttypes>
#include <vector>

void CShop::ConListItems(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	pSelf->ListItems();
}

void CShop::ConEditItem(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	const char *pItem = pResult->GetString(0);
	int Price = pResult->GetInteger(1);
	int MinLevel = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : -1;

	pSelf->EditItem(pItem, Price, MinLevel);
}

void CShop::ConReset(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	pSelf->ResetItems();
}

void CShop::ConBuyItem(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;
	const char *pItem = pResult->GetString(0);
	pSelf->BuyItem(ClientId, pItem);
}

void CShop::ConGiveItem(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	char aFrom[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aFrom, pSelf->Server()->ClientName(ClientId));

	const char *pItemName = pResult->GetString(1);
	pSelf->GiveItem(ClientId, pItemName, -2, aFrom);
}

void CShop::ConGiveItemDays(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	const int Days = pResult->GetInteger(1);
	const char *pItemName = pResult->GetString(2);

	char aFrom[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aFrom, pSelf->Server()->ClientName(ClientId));

	pSelf->GiveItem(ClientId, pItemName, Days, aFrom);
}

void CShop::ConGiveItemForever(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	char aFrom[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aFrom, pSelf->Server()->ClientName(ClientId));

	const char *pItemName = pResult->GetString(1);
	pSelf->GiveItem(ClientId, pItemName, ForeverDays, aFrom);
}

void CShop::ConRemoveItem(IConsole::IResult *pResult, void *pUserData)
{
	CShop *pSelf = (CShop *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;

	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	char aBy[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aBy, pSelf->Server()->ClientName(ClientId));

	const char *pItemName = pResult->GetString(1);
	pSelf->RemoveItem(ClientId, pItemName, aBy);
}

void CShop::AddItems()
{
	m_Registry.Init();
}

void CShop::ResetItems()
{
	m_Registry = CItemRegistry();

	AddItems();
	ListItems();
}

void CShop::ListItems()
{
	char Separator[128] = "";
	for(int Length = 0; Length < 56; Length++)
		str_append(Separator, "-");

	log_info("shop", "%s", Separator);
	for(const auto &kv : m_Registry.Map())
	{
		const CItemConfig &item = kv.second;
		log_info("shop", "%s | Price: %ld | MinLevel: %d", item.m_pName, item.m_Price, item.m_MinLevel);
	}
	log_info("shop", "%s", Separator);
}

void CShop::EditItem(const char *pName, int Price, int MinLevel)
{
	char aBuf[128];

	if(Price < -1)
	{
		str_format(aBuf, sizeof(aBuf), "Invalid price (%d) for \"%s\"", Price, pName);
		log_info("shop", "%s", aBuf);
		return;
	}

	CItemConfig *pItem = m_Registry.FindMutableByName(pName);
	if(!pItem)
	{
		str_format(aBuf, sizeof(aBuf), "Couldn't find \"%s\"", pName);
		log_info("shop", "%s", aBuf);
		return;
	}

	pItem->m_Price = Price;
	bool LevelChanged = MinLevel >= 0;
	if(LevelChanged)
		pItem->m_MinLevel = MinLevel;

	if(LevelChanged)
		str_format(aBuf, sizeof(aBuf), "Set price of \"%s\" to %ld and Min Level to %d", pName, pItem->m_Price, pItem->m_MinLevel);
	else
		str_format(aBuf, sizeof(aBuf), "Set price of \"%s\" to %ld", pName, pItem->m_Price);

	log_info("shop", "%s", aBuf);
}

bool CShop::BuyItem(int ClientId, const char *pName)
{
	char aBuf[256];

	const CItemConfig *pCfg = FindItem(pName);
	if(!pCfg)
		return false;

	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];

	if(!g_Config.m_SvAccounts)
	{
		GameServer()->SendChatTarget(ClientId, "Accounts are disabled.");
		return false;
	}

	if(!Acc.m_LoggedIn)
	{
		GameServer()->SendChatTarget(ClientId, "You aren't logged in.");

		return false;
	}

	if(pCfg->m_Price <= 0)
	{
		GameServer()->SendChatTarget(ClientId, "Invalid Item.");
		return false;
	}

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	if(!pPlayer->CanUseMoney())
	{
		GameServer()->SendChatTarget(ClientId, "You cannot use Money right now");
		GameServer()->SendChatTarget(ClientId, "Try again later");
		return false;
	}
	if(Acc.m_Level < pCfg->m_MinLevel)
	{
		str_format(aBuf, sizeof(aBuf), "You need atleast Level %d to buy %s", pCfg->m_MinLevel, pCfg->m_pName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "You are currently Level %" PRId64, Acc.m_Level);
		GameServer()->SendChatTarget(ClientId, aBuf);
		return false;
	}

	int Price = pPlayer->GetDiscountedPrice(pCfg->m_Price);

	if(Acc.m_Money < Price)
	{
		str_format(aBuf, sizeof(aBuf), "You don't have enough Money to buy %s", pCfg->m_pName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "You need atleast %d%s", Price, g_Config.m_SvCurrencyName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		return false;
	}

	if(pCfg->m_Id == EItemId::MaxCosmeticsUpgrade)
	{
		auto &Entry = Acc.m_Inventory.Entry(pCfg->m_pName);
		bool OwnsTooManyCosmeticUpgrades = Entry.m_Quantity >= g_Config.m_SvMaxCosmeticUpgrades;
		if(OwnsTooManyCosmeticUpgrades)
		{
			str_format(aBuf, sizeof(aBuf), "You can only own %d of '%s'", g_Config.m_SvMaxCosmeticUpgrades, pCfg->m_pName);
			GameServer()->SendChatTarget(ClientId, aBuf);
			return false;
		}
	}

	pPlayer->TakeMoney(Price, true);
	GiveItem(ClientId, pCfg, pCfg->m_DefaultDays, "Shop");

	str_format(aBuf, sizeof(aBuf), "Successfully bought Item '%s'", pCfg->m_pName);
	GameServer()->SendChatTarget(ClientId, aBuf);

	if(pCfg->m_Group == EExclusiveGroup::Hat)
	{
		int TypeHat = (int)pPlayer->Cosmetics()->m_HatType;
		if(TypeHat <= (int)EHatType::Ninja && TypeHat > (int)EHatType::None)
			GameServer()->SendChatTarget(ClientId, "Hats can be rotated! Head to the settings section to change the rotation");
	}

	return true;
}

bool CShop::GiveItem(int ClientId, const CItemConfig *pItem, int Days, const char *pFrom)
{
	if(!g_Config.m_SvAccounts)
	{
		GameServer()->SendChatTarget(ClientId, "Accounts are disabled.");
		return false;
	}

	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	if(!Acc.m_LoggedIn)
	{
		log_info("shop", "ClientId %d isn't logged in", ClientId);
		return false;
	}

	auto &Entry = Acc.m_Inventory.Entry(pItem->m_pName);
	const bool Owned = Acc.m_Inventory.Owns(pItem->m_pName);

	int64_t Now = time(0);
	if(!Owned)
		Entry.m_AcquiredAt = Now;

	if(HasFlag(pItem->m_Flags, EItemFlag::Stackable))
	{
		Entry.m_Quantity += 1;
		Entry.m_ExpiresAt = ForeverDays;
	}
	else
	{
		int EffectiveDays = (Days < 0) ? pItem->m_DefaultDays : Days;
		int64_t Duration = int64_t(EffectiveDays) * 86400;
		if(Owned)
			Entry.m_ExpiresAt += Duration;
		else
			Entry.m_ExpiresAt = Now + Duration;
		if(Days == ForeverDays || EffectiveDays == ForeverDays)
			Entry.m_ExpiresAt = ForeverDays;
		Entry.m_Quantity = 1;
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, Acc);
	return true;
}

bool CShop::GiveItem(int ClientId, const char *pName, int Days, const char *pFrom)
{
	if(!g_Config.m_SvAccounts)
	{
		GameServer()->SendChatTarget(ClientId, "Accounts are disabled.");
		return false;
	}

	const CItemConfig *pCfg = FindItem(pName);
	if(!pCfg)
	{
		log_info("shop", "Attempted to give non-existing item \"%s\" to ClientId %d", pName, ClientId);
		return false;
	}

	return GiveItem(ClientId, pCfg, Days, pFrom);
}

bool CShop::RemoveItem(int ClientId, const char *pItemName, const char *pByName)
{
	if(!g_Config.m_SvAccounts)
	{
		GameServer()->SendChatTarget(ClientId, "Accounts are disabled.");
		return false;
	}

	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	if(!Acc.m_LoggedIn)
		return false;
	auto &Entry = Acc.m_Inventory.Entry(pItemName);
	if(!Acc.m_Inventory.Owns(pItemName))
		return false;
	Entry.m_Quantity = 0;
	Entry.m_ExpiresAt = 0;
	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, Acc);
	return true;
}

const CItemConfig *CShop::RandomItemByRarity(EItemRarity Rarity, bool AllowAny)
{
	std::vector<const CItemConfig *> pool;
	for(const auto &kv : m_Registry.Map())
	{
		const auto &cfg = kv.second;
		if(HasFlag(cfg.m_Flags, EItemFlag::LootCase))
			continue;
		if(AllowAny || cfg.m_Rarity == Rarity)
			pool.push_back(&cfg);
	}
	if(pool.empty())
		return nullptr;
	std::mt19937 gen(Server()->Tick());
	std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
	return pool[dist(gen)];
}

const char *CShop::GetItemName(EItemId Id) const
{
	const CItemConfig *pItem = FindItem(Id);
	if(pItem)
		return pItem->m_pName;
	return "Unknown";
}

void CShop::OnInit()
{
	if(m_Registry.Map().empty())
		AddItems();
}

void CShop::OnConsoleInit()
{
	Console()->Register("shop_edit_item", "s[Name] i[Price] ?i[Minimum Level]", CFGFLAG_SERVER, ConEditItem, this, "Edit a shop item");
	Console()->Register("shop_list_items", "", CFGFLAG_SERVER, ConListItems, this, "Lists all shop items");
	Console()->Register("shop_reset", "", CFGFLAG_SERVER, ConReset, this, "Resets all prices in the shop");

	Console()->Register("remove_item", "v[id] r[item]", CFGFLAG_SERVER, ConRemoveItem, this, "remove an item from player (id)");
	Console()->Register("give_item", "v[id] r[item]", CFGFLAG_SERVER, ConGiveItem, this, "Give player (id) an item");
	Console()->Register("give_item_days", "v[id] i[days] r[item]", CFGFLAG_SERVER, ConGiveItemDays, this, "Give player (id) an item for x days");
	Console()->Register("give_item_forever", "v[id] r[item]", CFGFLAG_SERVER, ConGiveItemForever, this, "Give player (id) an item forever");

	Console()->Register("buyitem", "?r[item]", CFGFLAG_CHAT, ConBuyItem, this, "Buy an item from the shop");
}
