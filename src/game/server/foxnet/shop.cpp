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
#include "item_registry.h"

IServer *CShop::Server() const { return GameServer()->Server(); }

void CShop::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	if(m_Registry.Map().empty())
		AddItems();
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
		log_info("shop", "%s | Price: %d | MinLevel: %d", item.m_Name, item.m_Price, item.m_MinLevel);
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
		str_format(aBuf, sizeof(aBuf), "Set price of \"%s\" to %d and Min Level to %d", pName, pItem->m_Price, pItem->m_MinLevel);
	else
		str_format(aBuf, sizeof(aBuf), "Set price of \"%s\" to %d", pName, pItem->m_Price);

	log_info("shop", "%s", aBuf);
}

bool CShop::BuyItem(int ClientId, const char *pName)
{
	char aBuf[256];

	const CItemConfig *Cfg = FindItem(pName);
	if(!Cfg)
		return false;

	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	if(!Acc.m_LoggedIn)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ You aren't logged in");
		GameServer()->SendChatTarget(ClientId, "│ 1 - /register <Username> <Pw> <Pw>");
		GameServer()->SendChatTarget(ClientId, "│ 2 - /Login <Username> <Pw>");
		GameServer()->SendChatTarget(ClientId, "╰─────────────────────────────");
		
		return false;
	}

	if(Cfg->m_Price <= 0)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ Invalid Item.");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────────");
		
		return false;
	}

	CPlayer *pPl = GameServer()->m_apPlayers[ClientId];
	if(!pPl)
		return false;

	if(!pPl->CanUseMoney())
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		GameServer()->SendChatTarget(ClientId, "│ You cannot use Money right now");
		GameServer()->SendChatTarget(ClientId, "│ Try again later");
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return false;
	}
	if(Acc.m_Level < Cfg->m_MinLevel)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		str_format(aBuf, sizeof(aBuf), "│ You need atleast Level %d to buy %s", Cfg->m_MinLevel, Cfg->m_Name);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ You are currently Level %ld", Acc.m_Level);
		GameServer()->SendChatTarget(ClientId, aBuf);
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return false;
	}
	if(Acc.m_Money < Cfg->m_Price)
	{
		GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
		str_format(aBuf, sizeof(aBuf), "│ You don't have enough Money to buy %s", Cfg->m_Name);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ You need atleast %d%s", Cfg->m_Price, g_Config.m_SvCurrencyName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
		return false;
	}

	pPl->TakeMoney(Cfg->m_Price);
	GiveItem(ClientId, Cfg, -1, "Shop");

	GameServer()->SendChatTarget(ClientId, "╭──────     Sʜᴏᴘ");
	str_format(aBuf, sizeof(aBuf), "│ successfully bought item '%s'", Cfg->m_Name);
	GameServer()->SendChatTarget(ClientId, aBuf);
	GameServer()->SendChatTarget(ClientId, "╰───────────────────────");

	if(Cfg->m_Group == EExclusiveGroup::Hat)
		GameServer()->SendChatTarget(ClientId, "Hats can be rotated! Head to the settings section to change the rotation");
	return true;
}

bool CShop::GiveItem(int ClientId, const CItemConfig *pItem, int Days, const char *pFrom)
{
	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	if(!Acc.m_LoggedIn)
		return false;

	auto &Entry = Acc.m_Inventory.Entry(pItem->m_Name);
	const bool Owned = Acc.m_Inventory.Owns(pItem->m_Name);

	int64_t Now = time(0);
	if(!Owned)
		Entry.m_AcquiredAt = Now;

	if(HasFlag(pItem->m_Flags, EItemFlag::Consumable))
	{
		Entry.m_Quantity += 1;
		Entry.m_ExpiresAt = -1;
	}
	else
	{
		int EffectiveDays = (Days < 0) ? pItem->m_DefaultDays : Days;
		int64_t Duration = int64_t(EffectiveDays) * 86400;
		if(Owned)
			Entry.m_ExpiresAt += Duration;
		else
			Entry.m_ExpiresAt = Now + Duration;
		Entry.m_Quantity = 1;
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, Acc);
	return true;
}
bool CShop::GiveItemByName(int ClientId, const char *pName, int Days, const char *pFrom)
{
	const CItemConfig *Cfg = FindItem(pName);
	if(!Cfg)
		return false;

	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	if(!Acc.m_LoggedIn)
		return false;

	auto &Entry = Acc.m_Inventory.Entry(Cfg->m_Name);
	const bool Owned = Acc.m_Inventory.Owns(Cfg->m_Name);

	int64_t Now = time(0);
	if(!Owned)
		Entry.m_AcquiredAt = Now;

	if(HasFlag(Cfg->m_Flags, EItemFlag::Consumable))
	{
		Entry.m_Quantity += 1;
		Entry.m_ExpiresAt = -1;
	}
	else
	{
		int EffectiveDays = (Days < 0) ? Cfg->m_DefaultDays : Days;
		int64_t Duration = int64_t(EffectiveDays) * 86400;
		if(Owned)
			Entry.m_ExpiresAt += Duration;
		else
			Entry.m_ExpiresAt = Now + Duration;
		Entry.m_Quantity = 1;
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(ClientId, Acc);
	return true;
}

bool CShop::RemoveItem(int ClientId, const char *pItemName, const char *pByName)
{
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
		return pItem->m_Name;
	return "Unknown";
}