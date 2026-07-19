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
	CPlayer *pPlayer = pSelf->GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const char *pItem = pResult->GetString(0);
	pSelf->BuyItem(pPlayer, pItem);
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
	pSelf->GiveItem(pPlayer, pItemName, -2, aFrom);
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

	pSelf->GiveItem(pPlayer, pItemName, Days, aFrom);
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
	pSelf->GiveItem(pPlayer, pItemName, ForeverDays, aFrom);
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
	pSelf->RemoveItem(pPlayer, pItemName, aBy);
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

bool CShop::BuyItem(CPlayer *pPlayer, const char *pName)
{
	if(!pPlayer)
		return false;

	const CItemConfig *pCfg = FindItem(pName);
	if(!pCfg)
		return false;

	CAccountSession *pAcc = pPlayer->Acc();

	if(!g_Config.m_SvAccounts)
	{
		pPlayer->SendChat("Accounts are disabled.");
		return false;
	}

	if(!pAcc->m_LoggedIn)
	{
		pPlayer->SendChat("You aren't logged in.");

		return false;
	}

	if(pCfg->m_Price <= 0)
	{
		pPlayer->SendChat("Invalid Item.");
		return false;
	}

	if(!pPlayer->CanUseMoney())
	{
		pPlayer->SendChat("You cannot use Money right now");
		pPlayer->SendChat("Try again later");
		return false;
	}
	if(pAcc->m_Level < pCfg->m_MinLevel)
	{
		pPlayer->SendChatFmt("You need atleast Level %d to buy %s", pCfg->m_MinLevel, pCfg->m_pName);
		pPlayer->SendChatFmt("You are currently Level %" PRId64, pAcc->m_Level);
		return false;
	}

	int Price = pPlayer->GetDiscountedPrice(pCfg->m_Price);

	if(pAcc->m_Money < Price)
	{
		pPlayer->SendChatFmt("You don't have enough Money to buy %s", pCfg->m_pName);
		pPlayer->SendChatFmt("You need atleast %d%s", Price, g_Config.m_SvCurrencyName);
		return false;
	}

	const int MaxOfThisType = pCfg->m_MaxOfThisType;
	if(MaxOfThisType > 0)
	{
		auto &Entry = pAcc->m_Inventory.Entry(pCfg->m_pName);
		bool OwnsTooManyCosmeticUpgrades = Entry.m_Quantity >= MaxOfThisType;
		if(OwnsTooManyCosmeticUpgrades)
		{
			if(MaxOfThisType > 1)
				pPlayer->SendChatFmt("You can only own %d of '%s'", MaxOfThisType, pCfg->m_pName);
			return false;
		}
	}

	pPlayer->TakeMoney(Price, true);
	GiveItem(pPlayer, pCfg, pCfg->m_DefaultDays, "Shop", true);

	pPlayer->SendChatFmt("Successfully bought Item '%s'", pCfg->m_pName);

	if(pCfg->m_Group == EExclusiveGroup::Hat)
	{
		int TypeHat = (int)pPlayer->Cosmetics()->m_HatType;
		if(TypeHat <= (int)EHatType::Ninja && TypeHat > (int)EHatType::None)
			pPlayer->SendChat("Hats can be rotated! Head to the settings section to change the rotation");
	}

	return true;
}

bool CShop::GiveItem(CPlayer *pPlayer, const CItemConfig *pItem, int Days, const char *pFrom, bool AutoActivate)
{
	if(!g_Config.m_SvAccounts)
	{
		pPlayer->SendChat("Accounts are disabled.");
		return false;
	}

	CAccountSession *pAcc = pPlayer->Acc();
	if(!pAcc->m_LoggedIn)
	{
		log_info("shop", "ClientId %d isn't logged in", pPlayer->GetCid());
		return false;
	}

	auto &Entry = pAcc->m_Inventory.Entry(pItem->m_pName);
	const bool Owned = pAcc->m_Inventory.Owns(pItem->m_pName);

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

	if(AutoActivate && !pPlayer->ReachedItemLimit(pItem) && (HasFlag(pItem->m_Flags, EItemFlag::Equippable)))
		pPlayer->UseItem(pItem, -1, false);

	GameServer()->m_AccountManager.SaveAccountsInfo(pPlayer->GetCid(), *pAcc);
	return true;
}

bool CShop::GiveItem(CPlayer *pPlayer, const char *pName, int Days, const char *pFrom, bool AutoActivate)
{
	if(!g_Config.m_SvAccounts)
	{
		pPlayer->SendChat("Accounts are disabled.");
		return false;
	}

	const CItemConfig *pCfg = FindItem(pName);
	if(!pCfg)
	{
		log_info("shop", "Attempted to give non-existing item \"%s\" to ClientId %d", pName, pPlayer->GetCid());
		return false;
	}

	return GiveItem(pPlayer, pCfg, Days, pFrom, AutoActivate);
}

bool CShop::RemoveItem(CPlayer *pPlayer, const char *pItemName, const char *pByName)
{
	if(!g_Config.m_SvAccounts)
	{
		pPlayer->SendChat("Accounts are disabled.");
		return false;
	}

	CAccountSession *pAcc = pPlayer->Acc();
	if(!pAcc->m_LoggedIn)
		return false;
	auto &Entry = pAcc->m_Inventory.Entry(pItemName);
	if(!pAcc->m_Inventory.Owns(pItemName))
		return false;
	Entry.m_Quantity = 0;
	Entry.m_ExpiresAt = 0;
	GameServer()->m_AccountManager.SaveAccountsInfo(pPlayer->GetCid(), *pAcc);
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
