#include "accounts.h"
#include "cosmetics/dot_trail.h"
#include "cosmetics/epic_circle.h"
#include "cosmetics/halo.h"
#include "cosmetics/headitem.h"
#include "cosmetics/heart_hat.h"
#include "cosmetics/lissajous.h"
#include "cosmetics/lovely.h"
#include "cosmetics/pickup_pet.h"
#include "cosmetics/rotating_ball.h"
#include "cosmetics/staff_ind.h"
#include "entities/text/text.h"
#include "item_registry.h"
#include "shop.h"

#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <random>
#include <string>
#include <vector>

CAccountSession *CPlayer::Acc() { return &GameServer()->m_aAccounts[m_ClientId]; }
CInventory *CPlayer::Inv() { return &Acc()->m_Inventory; }
CCosmetics *CPlayer::Cosmetics() { return &Acc()->m_Inventory.m_Cosmetics; }

void CPlayer::FoxNetTick()
{
	if(m_LootBoxData.m_Opening)
		LootBoxTick();

	RainbowTick();
	if(Server()->Tick() % (Server()->TickSpeed() * 60) == 0) // Check every minute
		ExpireItems();

	if(m_BetAmount > Acc()->m_Money)
		m_BetAmount = -1; // Invalid bet, reset it

	if(Acc()->m_LoggedIn)
	{
		if(!IsAfk() && (Server()->Tick() - Acc()->m_LoginTick) % (Server()->TickSpeed() * 60) == 0 && Acc()->m_LoginTick != Server()->Tick())
		{
			GivePlaytime(1);
			int XP = 1;
			GiveXP(XP, "");
		}
	}
}

void CPlayer::LootBoxTick()
{
	// The item that the player will get is already in the inventory, this is just the animation

	if(!Acc()->m_LoggedIn)
		return;
	if(!m_LootBoxData.m_Opening)
		return;
	if(!m_LootBoxData.m_pLootBox || !m_LootBoxData.m_pGotItem)
		return;

	char aBuf[256];
	if(m_LootBoxData.m_Ticks > 1)
	{
		m_LootBoxData.m_Ticks--;
	}
	else
	{
		// Final message
		const CItemConfig *pItem = m_LootBoxData.m_pGotItem;
		str_format(aBuf, sizeof(aBuf), "You got '%s' [%s] for %d days!",
			pItem->m_pName,
			StarsString(pItem->m_Stars).c_str(),
			m_LootBoxData.m_Days);

		SendBroadcast(aBuf);

		str_format(aBuf, sizeof(aBuf), "'%s' opened a %s and got %s for %d days!",
			Server()->ClientName(m_ClientId),
			m_LootBoxData.m_pLootBox->m_pName,
			pItem->m_pName,
			m_LootBoxData.m_Days);

		GameServer()->SendChat(-1, TEAM_ALL, aBuf);

		m_LootBoxData.m_Opening = false;
		m_LootBoxData.m_Ticks = 0;
		return;
	}

	int Speed = std::clamp(LootBoxOpeningTicks / m_LootBoxData.m_Ticks * 3, 1, 25); // The higher the ticks the slower the speed

	if(m_LootBoxData.m_Ticks % Speed != 0)
		return;

	EItemId ItemId = m_LootBoxData.m_pLootBox->m_Id;
	EItemRarity Rarity = m_LootBoxData.m_pLootBox->m_Rarity;
	bool IsExotic = ItemId == EItemId::LootCaseExotic;

	const CItemConfig *pItem = GameServer()->m_Shop.RandomItemByRarity(Rarity, IsExotic);

	str_format(aBuf, sizeof(aBuf), "Opening %s\n[%s %s] %s",
		m_LootBoxData.m_pLootBox->m_pName,
		RarityToName(pItem->m_Rarity),
		StarsString(pItem->m_Stars).c_str(),
		pItem->m_pName);

	if(GetCharacter())
		GameServer()->CreateDeath(GetCharacter()->GetPos(), m_ClientId, GetCharacter()->TeamMask());

	GameServer()->CreateSound(GetCharacter()->GetPos(), SOUND_WEAPON_SWITCH);

	SendBroadcast(aBuf);
}

void CPlayer::ExpireItems()
{
	if(!Acc()->m_LoggedIn)
		return;
	int64_t now = time(0);
	for(auto &kv : Inv()->m_Map)
	{
		CInventoryEntry &Entry = kv.second;
		if(Entry.m_ExpiresAt <= 0 || Entry.m_ExpiresAt == -1)
			continue;
		if(Entry.m_ExpiresAt <= now)
		{
			const char *name = kv.first.c_str();
			if(Entry.m_Value != 0)
			{
				const CItemConfig *cfg = GameServer()->m_Shop.FindItem(name);
				if(cfg && cfg->m_Remove)
					cfg->m_Remove(*this, *cfg, -1);
			}
			Entry = CInventoryEntry();
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Item '%s' has expired!", name);
			GameServer()->m_AccountManager.RemoveItem(Acc()->m_aUsername, name);
			GameServer()->SendChatTarget(GetCid(), aBuf);
		}
	}
}
void CPlayer::FoxNetReset()
{
	m_MapOverridden = false;
	m_LastReport = 0;

	m_AccLoginAttempts = 0;
	m_AccRegisters = 0;

	m_IncludeServerInfo = true;

	m_ExtraPing = false;
	m_Vanish = false;
	m_IgnoreGamelayer = false;
	m_TelekinesisImmunity = false;

	m_Obfuscated = false;
	m_SpiderHook = false;
	m_Spazzing = false;

	m_Area = 0;

	Repredict(10); // Default PredMargin set by DDNet Client

	Acc()->m_Inventory = CInventory();
	m_vPickupDrops.clear();

	if(GameServer()->m_apPersistentData[GetCid()])
	{
		GameServer()->m_apPersistentData[GetCid()]->Load(this);
		delete GameServer()->m_apPersistentData[GetCid()];
		GameServer()->m_apPersistentData[GetCid()] = nullptr;
	}
}

void CPlayer::GivePlaytime(long Amount)
{
	if(!Acc()->m_LoggedIn)
		return;

	Acc()->m_Playtime++;
	if(Acc()->m_Playtime % 60 == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "+%d%s for reaching %ld Hours of Playtime!", g_Config.m_SvPlaytimeMoney, g_Config.m_SvCurrencyName, Acc()->m_Playtime / 60);
		GameServer()->SendChatTarget(m_ClientId, aBuf);
		GiveMoney(g_Config.m_SvPlaytimeMoney, false);
	}
}

void CPlayer::GiveXP(long Amount, const char *pMessage, bool Multiplier)
{
	if(!Acc()->m_LoggedIn)
		return;

	if(Multiplier)
		Amount = (long)(Amount * StatMultiplier());

	Acc()->m_XP += Amount;

	char aBuf[256];

	if(pMessage[0])
	{
		str_format(aBuf, sizeof(aBuf), "+%ld XP %s", Amount, pMessage);
		GameServer()->SendChatTarget(m_ClientId, aBuf);
	}

	CheckLevelUp(Amount);
}

bool CPlayer::CheckLevelUp(long Amount, bool Silent)
{
	bool LeveledUp = false;
	char aBuf[256];

	std::random_device rd;
	std::mt19937 gen(rd());

	// Level up as long as we have enough XP for the current level
	while(true)
	{
		const int NeededXp = GameServer()->m_AccountManager.NeededXP((int)Acc()->m_Level);
		if(Acc()->m_XP < NeededXp)
			break;

		Acc()->m_Level++;
		Acc()->m_XP -= NeededXp;

		GiveMoney(g_Config.m_SvLevelUpMoney, false);
		LeveledUp = true;

		const int Days = 7;
		char aCmd[256];
		char aCmdName[128];
		char aSubject[128];
		if(Acc()->m_Level == 5)
		{
			std::vector<const CItemConfig *> CommonItems;
			for(const auto &kv : GameServer()->m_Shop.Registry().Map())
			{
				const CItemConfig &Item = kv.second;
				if(Item.m_Rarity == EItemRarity::Common && Item.m_Price > 0)
					CommonItems.push_back(&Item);
			}

			if(!CommonItems.empty())
			{
				std::uniform_int_distribution<> dis(0, CommonItems.size() - 1);
				const CItemConfig *Reward = CommonItems[dis(gen)];

				std::uniform_int_distribution<> moneyDis(10, 100);
				int MoneyReward = moneyDis(gen) * 100; // Between 1.000 and 10.000
				// Command keeps a literal %d for the client id placeholder
				str_format(aCmd, sizeof(aCmd), "give_item_days %s %d %s;give_money %s %d", "%d", Days, Reward->m_pName, "%d", MoneyReward);
				str_format(aCmdName, sizeof(aCmdName), "%s for %d days\n %d%s", Reward->m_pName, Days, MoneyReward, g_Config.m_SvCurrencyName);
				str_copy(aSubject, "Congratulations on reaching level 5!");

				GameServer()->m_AccountManager.NewMail(m_ClientId, aSubject, "You have been given a random Common level Item and some Money!", aCmdName, aCmd);
			}
		}

		if(Acc()->m_Level % 15 == 0)
		{
			std::vector<const CItemConfig *> Items;
			for(const auto &kv : GameServer()->m_Shop.Registry().Map())
			{
				const CItemConfig &Item = kv.second;
				if((Item.m_Rarity == EItemRarity::Common || Item.m_Rarity == EItemRarity::Uncommon || Item.m_Rarity == EItemRarity::Rare) && Item.m_Price > 0)
					Items.push_back(&Item);
			}

			if(!Items.empty())
			{
				std::uniform_int_distribution<> dis(0, Items.size() - 1);
				const CItemConfig *Reward = Items[dis(gen)];
				const CItemConfig *Reward2 = Items[dis(gen)];

				std::uniform_int_distribution<> moneyDis(10, 250);
				int MoneyReward = moneyDis(gen) * 100; // Between 1.000 and 25.000
				// Three commands with a literal %d placeholder each
				str_format(aCmd, sizeof(aCmd), "give_item_days %s %d %s;give_item_days %s %d %s;give_money %s %d", "%d", Days, Reward->m_pName, "%d", Days, Reward2->m_pName, "%d", MoneyReward);
				str_format(aCmdName, sizeof(aCmdName), "%s for %d days\n %s for %d days\n %d%s", Reward->m_pName, Days, Reward2->m_pName, Days, MoneyReward, g_Config.m_SvCurrencyName);
				str_format(aSubject, sizeof(aSubject), "Congratulations on reaching level %ld!", Acc()->m_Level);

				GameServer()->m_AccountManager.NewMail(m_ClientId, aSubject, "You have been given a random Item and some Money!", aCmdName, aCmd);
			}
		}
	}

	if(LeveledUp && !Silent)
	{
		str_format(aBuf, sizeof(aBuf), "You are now level %ld!", Acc()->m_Level);
		GameServer()->SendChatTarget(m_ClientId, aBuf);

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && i != m_ClientId)
			{
				str_format(aBuf, sizeof(aBuf), "'%s' leveled up to level %ld!", Server()->ClientName(m_ClientId), Acc()->m_Level);
				GameServer()->SendChatTarget(i, aBuf);
			}
		}

		GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());

		if(GetCharacter())
			GameServer()->CreateBirthdayEffect(GetCharacter()->GetPos(), GetCharacter()->TeamMask());
	}

	return LeveledUp;
}

void CPlayer::GiveMoney(long Amount, bool Multiplier, bool Silent)
{
	if(!Acc()->m_LoggedIn)
		return;

	if(Multiplier)
		Amount = (long)(Amount * StatMultiplier());

	Acc()->m_Money += Amount;

	const char PlusMinus = Amount >= 0 ? '+' : '-';

	CCharacter *pChr = GetCharacter();
	if(!Silent && pChr)
	{
		const vec2 Pos = pChr->m_Pos + vec2(0, -74);
		char aText[24];
		str_format(aText, sizeof(aText), "%c%ld", PlusMinus, std::abs(Amount));
		new CProjectileText(pChr->GameWorld(), GetCid(), Pos, 100, aText, WEAPON_HAMMER);
		if(Amount >= 0)
			pChr->SetEmote(Amount >= 0 ? EMOTE_HAPPY : EMOTE_PAIN, Server()->Tick() + 75);
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());
}

long CPlayer::GetDiscountedPrice(long Price)
{
	float Discount = 0.0f;

	if(OwnsItem(EItemId::VIP))
		Discount = 0.10f;
	if(OwnsItem(EItemId::MVP))
		Discount = 0.25f;

	return (long)(Price * (1.0f - Discount));
}

bool CPlayer::CanUseMoney()
{
	if(!Acc()->m_LoggedIn)
		return false;
	if(GameServer()->m_pRoulette && GameServer()->m_pRoulette->ClientBetting(GetCid()))
		return false;

	return true;
}

bool CPlayer::OwnsItem(const char *pItemName)
{
	if(!Acc()->m_LoggedIn)
		return false;

	return Acc()->m_Inventory.Owns(pItemName);
}

bool CPlayer::OwnsItem(EItemId ItemId)
{
	if(!Acc()->m_LoggedIn)
		return false;

	const CItemConfig *cfg = GameServer()->m_Shop.Registry().FindById(ItemId);
	return cfg && Acc()->m_Inventory.Owns(cfg->m_pName);
}

bool CPlayer::ItemEnabled(const char *pItemName)
{
	if(!Acc()->m_LoggedIn)
		return false;
	auto it = Inv()->m_Map.find(std::string(pItemName));
	return it != Inv()->m_Map.end() && it->second.m_Value != 0;
}

bool CPlayer::ReachedItemLimit(const CItemConfig *Cfg)
{
	if(Server()->GetAuthedState(GetCid()) >= AUTHED_MOD || !Cfg)
		return false;
	int Amount = 0;
	for(const auto &kv : GameServer()->m_Shop.Registry().Map())
	{
		const CItemConfig &Other = kv.second;
		if(Other.m_Group != EExclusiveGroup::None && Other.m_Group == Cfg->m_Group)
			continue;
		if(HasFlag(Other.m_Flags, EItemFlag::LootCase))
			continue;
		if(Cfg == &Other)
			continue;

		auto mit = Inv()->m_Map.find(Other.m_pName);
		if(mit != Inv()->m_Map.end() && mit->second.m_Value > 0)
			Amount++;
	}

	return Amount >= g_Config.m_SvCosmeticLimit;
}

void CPlayer::UnequipExclusiveGroup(EExclusiveGroup Group, const CItemConfig *pExcept)
{
	if(Group == EExclusiveGroup::None)
		return;

	// Iterate all items that belong to the same exclusive group
	GameServer()->m_Shop.Registry().ForEachInGroup(Group, [&](const CItemConfig &Other) {
		if(pExcept && &Other == pExcept)
			return;
		auto it = Inv()->m_Map.find(Other.m_pName);
		if(it == Inv()->m_Map.end())
			return;
		CInventoryEntry &Entry = it->second;
		if(Entry.m_Value <= 0)
			return;
		if(Other.m_Remove)
			Other.m_Remove(*this, Other, -1);
		Entry.m_Value = 0;
	});
}

bool CPlayer::UseItem(const char *pName, int OverrideValue, bool Force)
{
	const CItemConfig *cfg = GameServer()->m_Shop.Registry().FindByName(pName);
	if(!cfg)
		return false;
	if(!Acc()->m_LoggedIn && !Force)
		return false;

	// Consumables (loot cases)
	if(HasFlag(cfg->m_Flags, EItemFlag::LootCase))
	{
		OpenLootCase(*cfg);
		return true;
	}

	CInventoryEntry &Entry = Inv()->Entry(cfg->m_pName);
	const bool CurrentlyEquipped = Entry.m_Value;

	int Equip = OverrideValue >= 0 ? OverrideValue : !CurrentlyEquipped;

	if(ReachedItemLimit(cfg) && Equip != 0 && !Force)
	{
		GameServer()->SendChatTarget(GetCid(), "You have reached the limit of equipped cosmetics. Unequip some other items first.");
		return false;
	}

	if(Equip != 0 && cfg->m_Group != EExclusiveGroup::None)
	{
		if(!CurrentlyEquipped)
			UnequipExclusiveGroup(cfg->m_Group, cfg);
	}

	if(Equip != 0)
	{
		if(cfg->m_Apply)
			cfg->m_Apply(*this, *cfg, OverrideValue);
		Entry.m_Value = Equip;
	}
	else
	{
		if(CurrentlyEquipped && cfg->m_Remove)
			cfg->m_Remove(*this, *cfg, OverrideValue);
		Entry.m_Value = 0;
	}
	return true;
}

bool CPlayer::OpenLootCase(const CItemConfig &CaseCfg)
{
	if(!Acc()->m_LoggedIn)
		return false;
	if(m_LootBoxData.m_Opening)
	{
		GameServer()->SendChatTarget(GetCid(), "You are already opening a loot case!");
		return false;
	}

	const EItemRarity CaseRarity = CaseCfg.m_Rarity;
	const bool AllowAny = CaseCfg.m_Id == EItemId::LootCaseExotic;

	std::vector<const CItemConfig *> vCandidates;
	int MaxStars = 1;
	for(const auto &kv : GameServer()->m_Shop.Registry().Map())
	{
		const CItemConfig &Other = kv.second;
		if(HasFlag(Other.m_Flags, EItemFlag::LootCase))
			continue;

		if(!AllowAny && Other.m_Rarity != CaseRarity)
			continue;

		vCandidates.push_back(&kv.second); // pointer to registry-owned item
		MaxStars = std::max(MaxStars, Other.m_Stars);
	}
	if(vCandidates.empty())
		return false;

	std::vector<EItemRarity> RarityLevels;
	RarityLevels.reserve(vCandidates.size());
	for(const CItemConfig *pIt : vCandidates)
	{
		EItemRarity r = pIt->m_Rarity;
		if(std::find(RarityLevels.begin(), RarityLevels.end(), r) == RarityLevels.end())
			RarityLevels.push_back(r);
	}
	std::sort(RarityLevels.begin(), RarityLevels.end());
	const int LastIdx = (int)RarityLevels.size() - 1;
	const int EpicIdx = std::min(2, LastIdx);
	const int MythicIdx = std::min(3, LastIdx);

	auto RarityFactor = [&](EItemRarity r) -> int {
		if(!AllowAny)
			return 1;

		int idx = 0;
		for(size_t i = 0; i < RarityLevels.size(); i++)
		{
			if(RarityLevels[i] == r)
			{
				idx = (int)i;
				break;
			}
		}
		if(idx == EpicIdx || idx == MythicIdx)
			return 5;
		if(idx == LastIdx)
			return 3;
		if(idx == 1)
			return 2;
		return 1;
	};

	std::vector<int> ItemWeights;
	ItemWeights.reserve(vCandidates.size());
	int TotalWeight = 0;
	for(const CItemConfig *pIt : vCandidates)
	{
		const int Base = (MaxStars - pIt->m_Stars + 1);
		int W = Base * RarityFactor(pIt->m_Rarity);
		if(W < 1)
			W = 1;
		ItemWeights.push_back(W);
		TotalWeight += W;
	}

	std::random_device rd;
	std::uniform_int_distribution<int> DistItem(1, std::max(TotalWeight, 1));
	int Pick = DistItem(rd);

	const CItemConfig *pSelectedItem = nullptr;
	for(size_t i = 0; i < vCandidates.size(); i++)
	{
		if(Pick <= ItemWeights[i])
		{
			pSelectedItem = vCandidates[i];
			break;
		}
		Pick -= ItemWeights[i];
	}
	if(!pSelectedItem)
		return false;

	auto it = Inv()->m_Map.find(CaseCfg.m_pName);
	if(it == Inv()->m_Map.end() || it->second.m_Quantity <= 0)
	{
		GameServer()->SendChatTarget(GetCid(), "You don't own this loot case.");
		return false;
	}
	it->second.m_Quantity = std::max(0, it->second.m_Quantity - 1);

	struct SDayWeight
	{
		int m_Days;
		int m_Weight;
	};
	const SDayWeight aDayTable[] = {
		{14, 50},
		{30, 35},
		{7, 10},
		{50, 5},
	};
	int DayTotal = 0;
	for(const auto &d : aDayTable)
		DayTotal += d.m_Weight;
	std::uniform_int_distribution<int> DistDay(1, std::max(DayTotal, 1));
	int DayPick = DistDay(rd);
	int RewardDays = 14;
	for(const auto &d : aDayTable)
	{
		if(DayPick <= d.m_Weight)
		{
			RewardDays = d.m_Days;
			break;
		}
		DayPick -= d.m_Weight;
	}
	GameServer()->m_Shop.GiveItem(GetCid(), pSelectedItem, RewardDays, "Loot Case");

	m_LootBoxData.m_pLootBox = &CaseCfg;
	m_LootBoxData.m_pGotItem = pSelectedItem;
	m_LootBoxData.m_Opening = true;
	m_LootBoxData.m_Ticks = LootBoxOpeningTicks;
	m_LootBoxData.m_Days = RewardDays;

	return true;
}

void CPlayer::RainbowTick()
{
	if(!GetCharacter() || (!Cosmetics()->m_RainbowBody && !Cosmetics()->m_RainbowFeet && GetCharacter()->GetPowerHooked() != HOOKTYPE_RAINBOW))
		return;

	if(Cosmetics()->m_RainbowSpeed < 1)
		Cosmetics()->m_RainbowSpeed = 1;

	if(Server()->Tick() % 2 == 1)
		m_RainbowColor = (m_RainbowColor + Cosmetics()->m_RainbowSpeed) % 256;
}

void CPlayer::OverrideSnap(int SnappingClient, CNetObj_ClientInfo *pClientInfo)
{
	Overriddename(SnappingClient, pClientInfo);
	RainbowSnap(SnappingClient, pClientInfo);

	if(g_Config.m_SvForceSkin[0])
		StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), g_Config.m_SvForceSkin);
}

void CPlayer::RainbowSnap(int SnappingClient, CNetObj_ClientInfo *pClientInfo)
{
	if(!GetCharacter() || (!Cosmetics()->m_RainbowBody && !Cosmetics()->m_RainbowFeet && GetCharacter()->GetPowerHooked() != HOOKTYPE_RAINBOW))
		return;

	if(GetCharacter()->GetPowerHooked() == HOOKTYPE_RAINBOW)
		GetCharacter()->m_IsRainbowHooked = true;

	int BaseColor = m_RainbowColor * 0x010000;
	int Color = 0xff32;

	// only send rainbow updates to people close to you, to reduce network traffic
	if(GameServer()->m_apPlayers[SnappingClient] && !GetCharacter()->NetworkClipped(SnappingClient))
	{
		if(GameServer()->m_aAccounts[SnappingClient].m_Configs.m_Cosmetics.m_ShowRainbow)
		{
			pClientInfo->m_UseCustomColor = 1;
			if(Cosmetics()->m_RainbowBody || GetCharacter()->m_IsRainbowHooked)
				pClientInfo->m_ColorBody = BaseColor + Color;
			if(Cosmetics()->m_RainbowFeet || GetCharacter()->m_IsRainbowHooked)
				pClientInfo->m_ColorFeet = BaseColor + Color;
		}
	}
}

void CPlayer::Overriddename(int SnappingClient, CNetObj_ClientInfo *pClientInfo)
{
	if(m_Obfuscated)
	{
		constexpr int maxBytes = sizeof(pClientInfo->m_aName);
		std::string obfStr = RandomUnicode(maxBytes / 3);
		if(obfStr.size() >= maxBytes)
			obfStr.resize(maxBytes - 1);
		const char *pObf = obfStr.c_str();

		StrToInts(pClientInfo->m_aName, std::size(pClientInfo->m_aName), pObf);
		StrToInts(pClientInfo->m_aClan, std::size(pClientInfo->m_aClan), " ");
	}

	if(!GetCharacter())
		return;

	if(GetCharacter()->m_InSnake)
	{
		StrToInts(pClientInfo->m_aName, std::size(pClientInfo->m_aName), " ");
		StrToInts(pClientInfo->m_aClan, std::size(pClientInfo->m_aClan), " ");
	}
}
void CPlayer::SetRainbowBody(bool Active)
{
	Cosmetics()->m_RainbowBody = Active;
}

void CPlayer::SetRainbowFeet(bool Active)
{
	Cosmetics()->m_RainbowFeet = Active;
}

void CPlayer::SetSparkle(bool Active)
{
	Cosmetics()->m_Sparkle = Active;
}

void CPlayer::SetInverseAim(bool Active)
{
	Cosmetics()->m_InverseAim = Active;
}

void CPlayer::SetBloody(bool Active)
{
	Cosmetics()->m_Bloody = Active;
	Cosmetics()->m_StrongBloody = false;
}

void CPlayer::SetRotatingBall(bool Active)
{
	if(Cosmetics()->m_RotatingBall == Active)
		return;
	Cosmetics()->m_RotatingBall = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_RotatingBall)
		new CRotatingBall(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetEpicCircle(bool Active)
{
	if(Cosmetics()->m_EpicCircle == Active)
		return;
	Cosmetics()->m_EpicCircle = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_EpicCircle)
		new CEpicCircle(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetLovely(bool Active)
{
	if(Cosmetics()->m_Lovely == Active)
		return;
	Cosmetics()->m_Lovely = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_Lovely)
		new CLovely(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetTrail(int Type)
{
	if(Cosmetics()->m_Trail == Type)
		return;
	Cosmetics()->m_Trail = Type;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_Trail == TRAILTYPE_DOT)
		new CDotTrail(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetStaffInd(bool Active)
{
	if(Cosmetics()->m_StaffInd == Active)
		return;
	Cosmetics()->m_StaffInd = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_StaffInd)
		new CStaffInd(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetPickupPet(bool Active)
{
	if(Cosmetics()->m_PickupPet == Active)
		return;
	Cosmetics()->m_PickupPet = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_PickupPet)
		m_pPickupPet = new CPickupPet(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetLissajous(bool Active)
{
	if(Cosmetics()->m_Lissajous == Active)
		return;
	Cosmetics()->m_Lissajous = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_Lissajous)
		new CLissajous(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetHalo(bool Active)
{
	if(Cosmetics()->m_Halo == Active)
		return;
	Cosmetics()->m_Halo = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_Halo)
		new CHalo(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetHeartHat(bool Active)
{
	if(Cosmetics()->m_HeartHat == Active)
		return;
	Cosmetics()->m_HeartHat = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_HeartHat)
		new CHeartHat(&GameServer()->m_World, GetCid(), Pos);
}

void CPlayer::SetHatType(EHatType Type)
{
	if(Cosmetics()->m_HatType == Type)
		return;
	EHatType PrevType = Cosmetics()->m_HatType;
	Cosmetics()->m_HatType = Type;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_HatType != EHatType::None && PrevType == EHatType::None)
		new CHeadItem(&GameServer()->m_World, GetCid(), Pos, HEADITEM_COSMETIC, vec2(0, -45.0f));
}

void CPlayer::SetDeathEffect(int Type)
{
	Cosmetics()->m_DeathEffect = Type;
}
void CPlayer::SetDamageIndType(int Type)
{
	Cosmetics()->m_DamageIndType = Type;
}
void CPlayer::SetGunType(int Type)
{
	Cosmetics()->m_GunType = Type;
}

void CPlayer::SetStrongBloody(bool Active)
{
	Cosmetics()->m_StrongBloody = Active;
	Cosmetics()->m_Bloody = false;
}

void CPlayer::HookPower(int Extra)
{
	if(Cosmetics()->m_HookPower == HOOKTYPE_NORMAL && Extra == HOOKTYPE_NORMAL)
		return;
	Cosmetics()->m_HookPower = Extra;
}

void CPlayer::SetEmoticonGun(int EmoteType)
{
	Cosmetics()->m_EmoticonGun = EmoteType;
}

void CPlayer::SetPhaseGun(bool Active)
{
	Cosmetics()->m_PhaseGun = Active;
}

void CPlayer::SetConfettiGun(bool Set)
{
	Cosmetics()->m_ConfettiGun = Set;
}

void CPlayer::SetInvisible(bool Active)
{
	m_Invisible = Active;
}

void CPlayer::SetExtraPing(int Ping)
{
	m_ExtraPing = Ping;
}

void CPlayer::SetIgnoreGameLayer(bool Set)
{
	m_IgnoreGamelayer = Set;
}

void CPlayer::SetObfuscated(bool Set)
{
	m_Obfuscated = Set;
}

void CPlayer::SetTelekinesisImmunity(bool Active)
{
	m_TelekinesisImmunity = Active;
}

void CPlayer::SetHidePowerUps(bool Set)
{
	Acc()->m_Configs.m_HidePowerUps = Set;
}

static const char *GetAbilityName(int Type)
{
	switch(Type)
	{
	case ABILITY_FIREWORK:
		return "Firework";
	case ABILITY_TELEKINESIS:
		return "Telekinesis";
	}
	return "Unknown";
}

void CPlayer::SetAbility(int Type)
{
	if(Cosmetics()->m_Ability == Type)
		return;

	Cosmetics()->m_Ability = Type;

	int ClientId = GetCid();

	if(Cosmetics()->m_Ability <= ABILITY_NONE)
		return;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Ability set to %s", GetAbilityName(Cosmetics()->m_Ability));
	GameServer()->SendChatTarget(ClientId, aBuf);

	GameServer()->SendChatTarget(ClientId, "Use f3 (Vote Yes) to use your Ability");
}

void CPlayer::DisableAllCosmetics()
{
	Cosmetics()->Reset();
}

int CPlayer::NumDDraceHudRows()
{
	if(Server()->IsSixup(GetCid()) || GameServer()->GetClientVersion(GetCid()) < VERSION_DDNET_NEW_HUD)
		return 0;

	CCharacter *pChr = GetCharacter();
	if((GetTeam() == TEAM_SPECTATORS || IsPaused()) && SpectatorId() >= 0 && GameServer()->GetPlayerChar(SpectatorId()))
		pChr = GameServer()->GetPlayerChar(SpectatorId());

	if(!pChr)
		return 0;

	int Rows = 0;
	if(pChr->Core()->m_EndlessJump || pChr->Core()->m_EndlessHook || pChr->Core()->m_Jetpack || pChr->Core()->m_HasTelegunGrenade || pChr->Core()->m_HasTelegunGun || pChr->Core()->m_HasTelegunLaser)
		Rows++;
	if(pChr->Core()->m_Solo || pChr->Core()->m_CollisionDisabled || pChr->Core()->m_Passive || pChr->Core()->m_HookHitDisabled || pChr->Core()->m_HammerHitDisabled || pChr->Core()->m_ShotgunHitDisabled || pChr->Core()->m_GrenadeHitDisabled || pChr->Core()->m_LaserHitDisabled)
		Rows++;
	if(pChr->Teams()->IsPractice(pChr->Team()) || pChr->Teams()->TeamLocked(pChr->Team()) || pChr->Core()->m_DeepFrozen || pChr->Core()->m_LiveFrozen)
		Rows++;

	if(GameServer()->m_VoteCloseTime)
		Rows += 3;

	return Rows;
}

// Broadcasts like opening a lootcase or having a bot client are more important than area broadcasts or stuff like that
bool CPlayer::HasImportantBroadcast() const
{
	return m_LootBoxData.m_Opening;
}

void CPlayer::SendBroadcastHud(std::vector<std::string> pMessages, int Offset)
{
	if(pMessages.empty())
		return;
	if(HasImportantBroadcast())
		return; // Other broadcast is being sent

	char aBuf[256] = "";
	int NextLines = Offset == -1 ? NumDDraceHudRows() : Offset;

	for(int i = 0; i < NextLines; i++)
		str_append(aBuf, "\n", sizeof(aBuf));

	for(std::string pMessage : pMessages)
	{
		str_append(aBuf, pMessage.c_str(), sizeof(aBuf));
		str_append(aBuf, "\n", sizeof(aBuf));
	}

	if(!Server()->IsSixup(GetCid()))
		for(int i = 0; i < 137; i++) // 16:9 ratio default font
			str_append(aBuf, " ", sizeof(aBuf));

	SendBroadcast(aBuf);
}

void CPlayer::SendBroadcast(const char *pText)
{
	if(!str_comp(m_BroadcastData.m_aMessage, pText) && m_BroadcastData.m_Time + Server()->TickSpeed() * 9 > Server()->Tick())
		return;

	str_copy(m_BroadcastData.m_aMessage, pText);
	m_BroadcastData.m_Time = Server()->Tick();

	GameServer()->SendBroadcast(pText, GetCid());
}

void CPlayer::SendAreaMotd(int Area)
{
	if(m_Area == Area)
		return;

	CCharacter *pChr = GetCharacter();
	if(!pChr)
		return;
	if(pChr->Team() != TEAM_FLOCK)
		return;

	if(Area == 0)
	{
		ClearBroadcast();
		return;
	}

	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = "\0";
	switch(Area)
	{
	case AREA_ROULETTE:
		Msg.m_pMessage =
			"\n"
			"[Viewable in Server info Tab]\n"
			"\n"
			"\n"
			"--  Rᴏᴜʟᴇᴛᴛᴇ  --\n"
			"\n"
			"To start, write '/bet <amount>', after that you can select your bet type by hovering your mouse over any of the options below and hammering\n"
			"\n"
			"Pᴀʏᴏᴜᴛs:\n"
			"Black | Red: 2x\n"
			"3x dozens: 3x\n"
			"Green [Zero]: 10x\n"
			"\n"
			"[Press Tab to hide]";
		break;
	default:
		break;
	}
	if(Msg.m_pMessage[0] == '\0')
		return;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, GetCid());
}

void CPlayer::SetArea(int Area)
{
	if(HasImportantBroadcast())
		return;
	SendAreaMotd(Area);
	m_Area = Area;
}

float CPlayer::GetClientPred()
{
	float Ping = (m_Latency.m_Min) / 10.0f - 0.8f;
	return std::max(Ping + m_PredMargin, 2.2f);
}

int CPlayer::GetSubPage()
{
	return GameServer()->m_VoteMenu.GetSubPage(m_ClientId);
}
int CPlayer::GetPage()
{
	return GameServer()->m_VoteMenu.GetPage(m_ClientId);
}

void CPlayer::SetPage(int Page)
{
	GameServer()->m_VoteMenu.SetPage(m_ClientId, Page);
}
void CPlayer::SetSubPage(int SubPage)
{
	GameServer()->m_VoteMenu.SetSubPage(m_ClientId, SubPage);
}

float CPlayer::StatMultiplier()
{
	float Multiplier = 1.0f;
	if(GameServer()->IsWeekend())
		Multiplier += 1.0f;

	if(!Acc()->m_LoggedIn)
		return Multiplier;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(Server()->ClientSlotEmpty(ClientId))
			continue;
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;
		if(pPlayer->OwnsItem(EItemId::BOOSTER))
		{
			Multiplier += 1.5f;
			break;
		}
	}

	if(OwnsItem(EItemId::VIP))
		Multiplier += 2.5f;
	if(OwnsItem(EItemId::MVP))
		Multiplier += 3.5f;
	return Multiplier;
}