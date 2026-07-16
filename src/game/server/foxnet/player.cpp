#include "component.h"
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
#include "entities/roulette.h"
#include "entities/text/text.h"
#include "item_registry.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/foxnet/components/accounts/accounts.h>
#include <game/server/foxnet/components/shop.h>
#include <game/server/foxnet/cosmetics/firework.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <vector>

CAccountSession *CPlayer::Acc() { return &GameServer()->m_aAccounts[m_ClientId]; }
CInventory *CPlayer::Inv() { return &Acc()->m_Inventory; }
CCosmetics *CPlayer::Cosmetics() { return &Acc()->m_Inventory.m_Cosmetics; }

void CPlayer::FoxNetPreTick()
{
	// Handle telekinesis before everything
	HandleTelekinesis();
}

void CPlayer::FoxNetTick()
{
	if(!Acc()->m_LoggedIn && m_RetryAutoLogin)
	{
		constexpr int AutoLoginCooldown = round_to_int((int)SERVER_TICK_SPEED * 3); // 3 seconds cooldown between auto login attempts
		if(m_LastAutoLoginAttempt.has_value() && m_LastAutoLoginAttempt.value() + AutoLoginCooldown < Server()->Tick())
		{
			GameServer()->m_AccountManager.AutoLogin(GetCid());
			m_LastAutoLoginAttempt.reset();
		}
	}

	if(m_VoteActionDelay >= 0)
		m_VoteActionDelay--;

	if(m_LootBoxData.m_Opening)
		LootBoxTick();

	RainbowTick();
	if(Server()->Tick() % (Server()->TickSpeed() * 5) == 0)
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

	if(CCharacter *pChr = GetCharacter())
	{
		GameServer()->CreateDeath(pChr->GetPos(), m_ClientId, pChr->TeamMask());
		GameServer()->CreateSound(pChr->GetPos(), SOUND_WEAPON_SWITCH);
	}

	SendBroadcast(aBuf);
}

void CPlayer::ExpireItems()
{
	if(!Acc()->m_LoggedIn)
		return;
	int64_t Now = time(0);
	for(auto &Item : Inv()->m_Map)
	{
		CInventoryEntry &Entry = Item.second;
		if(Entry.m_ExpiresAt == ForeverDays) // || Entry.m_ExpiresAt == ForeverDays)
			continue;
		if(Entry.m_ExpiresAt <= Now)
		{
			const char *pName = Item.first.c_str();
			if(Entry.m_Value != 0)
			{
				const CItemConfig *pCfg = GameServer()->m_Shop.FindItem(pName);
				if(pCfg && pCfg->m_Remove)
					pCfg->m_Remove(*this, *pCfg, -1);
			}
			Entry = CInventoryEntry();
			SendChatFmt("Item '%s' has expired!", pName);
			GameServer()->m_AccountManager.RemoveItem(Acc()->m_aUsername, pName);
		}
	}
}
void CPlayer::FoxNetReset()
{
	m_LastAutoLoginAttempt.reset();
	m_RetryAutoLogin = false;

	m_LastTransaction = 0;
	m_vReceivedConditionals.clear();

	m_MultiMapIndex = DefaultMapIndex;
	m_LastReport = Server()->Tick();

	m_AccLoginAttempts = 0;
	m_AccRegisters = 0;

	m_IncludeServerInfo = true;
	if(Server()->DebugDummy(GetCid()))
		m_IncludeServerInfo = false;

	m_ExtraPing = false;
	m_Vanish = false;
	m_IgnoreGamelayer = false;
	m_TelekinesisImmunity = false;

	m_Obfuscated = false;
	m_SpiderHook = false;
	m_Spazzing = false;

	m_Area = EArea::Game;
	m_LastArea = EArea::Game;
	m_LastAreaMotd = 0;

	Repredict(10); // Default PredMargin set by DDNet Client

	if(!Acc()->m_LoggedIn)
		Acc()->m_Inventory = CInventory();
	m_vPickupDrops.clear();

	if(GameServer()->m_apPersistentData[GetCid()])
	{
		GameServer()->m_apPersistentData[GetCid()]->Load(this);
		delete GameServer()->m_apPersistentData[GetCid()];
		GameServer()->m_apPersistentData[GetCid()] = nullptr;
	}
	if(Acc()->m_LoggedIn)
	{
		Inv()->m_Cosmetics.Reset();
		for(const auto &[ItemName, Entry] : Inv()->m_Map)
		{
			if(Entry.m_Value <= 0)
				continue;
			const CItemConfig *pCfg = GameServer()->m_Shop.FindItem(ItemName.c_str());
			if(!pCfg || !HasFlag(pCfg->m_Flags, EItemFlag::Equippable) || !pCfg->m_Apply)
				continue;
			pCfg->m_Apply(*this, *pCfg, Entry.m_Value);
		}
	}
	else
		GameServer()->m_AccountManager.ForceLogin(GetCid(), Acc()->m_aUsername, true, true);
}

void CPlayer::GivePlaytime(int64_t Amount)
{
	if(!Acc()->m_LoggedIn)
		return;

	Acc()->m_Playtime++;
	if(Acc()->m_Playtime % 60 == 0)
	{
		SendChatFmt("+%d%s for reaching %" PRId64 " Hours of Playtime!", g_Config.m_SvPlaytimeMoney, g_Config.m_SvCurrencyName, Acc()->m_Playtime / 60);
		GiveMoney(g_Config.m_SvPlaytimeMoney, false);
	}
}

void CPlayer::GiveXP(int64_t Amount, const char *pMessage, bool Multiplier)
{
	if(!Acc()->m_LoggedIn)
		return;

	if(Multiplier)
		Amount = (int64_t)(Amount * StatMultiplier());

	Acc()->m_XP += Amount;

	if(pMessage[0])
		SendChatFmt("+%" PRId64 " XP %s", Amount, pMessage);

	CheckLevelUp();
}

bool CPlayer::CheckLevelUp(bool Silent)
{
	bool LeveledUp = false;

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

		class CReward
		{
		public:
			int m_MinMoney = 1000;
			int m_MaxMoney = 10000;
			int m_ItemCount = 1;
			std::vector<EItemRarity> m_AllowedRarities = {EItemRarity::Common};

			CReward(int MinMoney, int MaxMoney, int ItemCount, const std::vector<EItemRarity> &AllowedRarities) :
				m_MinMoney(MinMoney), m_MaxMoney(MaxMoney), m_ItemCount(ItemCount), m_AllowedRarities(AllowedRarities) {}
			CReward() = default;
		};

		auto NewRewardMail = [this](CReward Reward) {
			class CRewardItem
			{
			public:
				const CItemConfig *m_pItem = nullptr;
				int m_Days = 0;
			};

			char aCmd[512] = "";
			char aCmdName[512] = "";
			char aSubject[128] = "";
			char aMessage[128] = "";

			std::vector<const CItemConfig *> Items;
			for(auto &Item : GameServer()->m_Shop.Registry().Map())
			{
				const CItemConfig &pCfg = Item.second;
				if(std::find(Reward.m_AllowedRarities.begin(), Reward.m_AllowedRarities.end(), pCfg.m_Rarity) != Reward.m_AllowedRarities.end() && pCfg.m_Price > 0)
					Items.push_back(&pCfg);
			}

			if(!Items.empty())
			{
				std::uniform_int_distribution<int> Dis(0, Items.size() - 1);
				std::uniform_int_distribution<int> DaysDis(1, 3);

				std::vector<CRewardItem> vRewardItems;
				for(int i = 0; i < Reward.m_ItemCount; i++)
				{
					CRewardItem RewardItem;
					RewardItem.m_pItem = Items[Dis(Rng())];
					RewardItem.m_Days = DaysDis(Rng()) * 7;
					vRewardItems.push_back(RewardItem);
				}

				std::uniform_int_distribution<int> MoneyDis(Reward.m_MinMoney / 100, Reward.m_MaxMoney / 100);
				int MoneyReward = MoneyDis(Rng()) * 100;

				str_copy(aCmd, "");
				str_copy(aCmdName, "");
				for(const CRewardItem &RewardItem : vRewardItems)
				{
					char aTemp[128];
					str_format(aTemp, sizeof(aTemp), "give_item_days %s %d %s;", "%d", RewardItem.m_Days, RewardItem.m_pItem->m_pName);
					str_append(aCmd, aTemp, sizeof(aCmd));
					char aTempName[128];
					str_format(aTempName, sizeof(aTempName), "%s for %d days\n", RewardItem.m_pItem->m_pName, RewardItem.m_Days);
					str_append(aCmdName, aTempName, sizeof(aCmdName));
				}
				char aMoneyCmd[128];
				str_format(aMoneyCmd, sizeof(aMoneyCmd), "give_money %s %d", "%d", MoneyReward);
				str_append(aCmd, aMoneyCmd, sizeof(aCmd));
				char aMoneyCmdName[128];
				str_format(aMoneyCmdName, sizeof(aMoneyCmdName), "%d%s", MoneyReward, g_Config.m_SvCurrencyName);
				str_append(aCmdName, aMoneyCmdName, sizeof(aCmdName));

				str_format(aSubject, sizeof(aSubject), "Level %" PRId64 " Reward!", Acc()->m_Level);
				str_format(aMessage, sizeof(aMessage), "You have been given %d random Item%s and some Money!", Reward.m_ItemCount, Reward.m_ItemCount > 1 ? "s" : "");
				GameServer()->m_AccountManager.NewMail(m_ClientId, aSubject, aMessage, aCmdName, aCmd);
			}
		};

		if(Acc()->m_Level == 5)
		{
			NewRewardMail(CReward(1000, 5000, 1, {EItemRarity::Common}));
		}
		else if(Acc()->m_Level % 100 == 0)
		{
			NewRewardMail(CReward(50000, 125000, 4, {
									EItemRarity::Common,
									EItemRarity::Uncommon,
									EItemRarity::Rare,
									EItemRarity::Epic,
									EItemRarity::Mythic,
								}));
		}
		else if(Acc()->m_Level % 50 == 0)
		{
			NewRewardMail(CReward(10000, 50000, 3, {EItemRarity::Common, EItemRarity::Uncommon, EItemRarity::Rare, EItemRarity::Epic}));
		}
		else if(Acc()->m_Level % 10 == 0)
		{
			NewRewardMail(CReward(5000, 10000, 2, {EItemRarity::Common, EItemRarity::Uncommon, EItemRarity::Rare}));
		}
	}

	if(LeveledUp && !Silent)
	{
		SendChatFmt("You are now level %" PRId64 "!", Acc()->m_Level);

		for(int ClientId = 0; ClientId < Server()->MaxClients(); ClientId++)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(pPlayer && ClientId != m_ClientId)
			{
				pPlayer->SendChatFmt("'%s' leveled up to level %" PRId64 "!", Server()->ClientName(m_ClientId), Acc()->m_Level);
			}
		}

		GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());

		if(GetCharacter())
			GameServer()->CreateBirthdayEffect(GetCharacter()->GetPos(), GetCharacter()->TeamMask());
	}

	return LeveledUp;
}

void CPlayer::GiveMoney(int64_t Amount, bool Multiplier, bool Silent)
{
	if(!Acc()->m_LoggedIn)
		return;

	if(Multiplier)
		Amount = (int64_t)(Amount * StatMultiplier());

	const int64_t OldMoney = Acc()->m_Money;
	int64_t NewMoney = OldMoney;
	if(Amount >= 0)
		NewMoney = std::min<int64_t>(std::numeric_limits<int64_t>::max() - OldMoney, Amount) + OldMoney;
	else
		NewMoney = std::max<int64_t>(0, OldMoney + Amount);

	Acc()->m_Money = NewMoney;
	const int64_t AppliedAmount = NewMoney - OldMoney;

	const char PlusMinus = AppliedAmount >= 0 ? '+' : '-';

	CCharacter *pChr = GetCharacter();
	if(pChr && !Silent && AppliedAmount != 0)
	{
		const vec2 Pos = pChr->m_Pos + vec2(0, -74);
		char aText[24];
		str_format(aText, sizeof(aText), "%c%" PRId64, PlusMinus, AppliedAmount >= 0 ? AppliedAmount : -AppliedAmount);
		new CProjectileText(pChr->GameWorld(), MultiMapIdx(), GetCid(), Pos, 100, aText, WEAPON_HAMMER); // NOLINT(clang-analyzer-unix.Malloc)
		pChr->SetEmote(AppliedAmount >= 0 ? EMOTE_HAPPY : EMOTE_PAIN, Server()->Tick() + 75);
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());
}

void CPlayer::PayMoney(CPlayer *pReceiver, int64_t Amount)
{
	if(!Acc()->m_LoggedIn || !pReceiver || !pReceiver->Acc()->m_LoggedIn)
		return;
	if(Amount <= 0)
		return;

	if(!CanUseMoney())
	{
		SendChat("You can't use money right now");
		return;
	}

	if(Acc()->m_Level < 10)
	{
		SendChat("You need to be at least level 10 to do transactions.");
		return;
	}
	if(Acc()->m_Money < Amount)
	{
		SendChat("You don't have enough money to do this transaction.");
		return;
	}

	constexpr int Cooldown = 300; // 5 minutes cooldown between transactions to prevent abuse
	if(m_LastTransaction != 0 && Server()->Tick() - m_LastTransaction < Server()->TickSpeed() * Cooldown)
	{
		SendChat("You need to wait a bit before doing another transaction.");
		return;
	}

	TakeMoney(Amount, false);
	pReceiver->GiveMoney(Amount, false, false);

	SendChatFmt("You paid %s %" PRId64 "%s", Server()->ClientName(pReceiver->GetCid()), Amount, g_Config.m_SvCurrencyName);
	pReceiver->SendChatFmt("You received %" PRId64 "%s from %s", Amount, g_Config.m_SvCurrencyName, Server()->ClientName(GetCid()));
	m_LastTransaction = Server()->Tick();
}

int64_t CPlayer::GetDiscountedPrice(int64_t Price)
{
	float Discount = 0.0f;

	if(OwnsItem(EItemId::VIP))
		Discount = 0.05f;
	if(OwnsItem(EItemId::MVP))
		Discount = 0.15f;

	return (int64_t)(Price * (1.0f - Discount));
}

bool CPlayer::CanUseMoney()
{
	if(!Acc()->m_LoggedIn)
		return false;
	CRoulette *pRoulette = static_cast<CRoulette *>(GameServer()->m_World.FindEntityOnMap(CGameWorld::ENTTYPE_ROULETTE, MultiMapIdx()));
	if(pRoulette && pRoulette->ClientBetting(GetCid()))
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

	const CItemConfig *pCfg = GameServer()->m_Shop.Registry().FindById(ItemId);
	return pCfg && Acc()->m_Inventory.Owns(pCfg->m_pName);
}

bool CPlayer::ItemEnabled(const char *pItemName)
{
	if(!Acc()->m_LoggedIn)
		return false;
	auto It = Inv()->m_Map.find(std::string(pItemName));
	return It != Inv()->m_Map.end() && It->second.m_Value != 0;
}

bool CPlayer::ReachedItemLimit(const CItemConfig *pCfg)
{
	if(Server()->GetAuthedState(GetCid()) >= AUTHED_MOD || !pCfg)
		return false;
	int Amount = 0;
	for(const auto &Item : GameServer()->m_Shop.Registry().Map())
	{
		const CItemConfig &Other = Item.second;

		const auto &Mit = Inv()->m_Map.find(Other.m_pName);

		if(Mit == Inv()->m_Map.end())
			continue;

		if(Item.second.m_Id == EItemId::MaxCosmeticsUpgrade)
			Amount -= Mit->second.m_Quantity;

		if(HasFlag(Other.m_Flags, EItemFlag::Upgrade))
			continue; // Upgrades aren't cosmetics

		if(Other.m_Group != EExclusiveGroup::None && Other.m_Group == pCfg->m_Group)
			continue;
		if(!HasFlag(Other.m_Flags, EItemFlag::Equippable))
			continue;
		if(pCfg == &Other)
			continue;

		if(Mit->second.m_Value > 0)
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
		auto It = Inv()->m_Map.find(Other.m_pName);
		if(It == Inv()->m_Map.end())
			return;
		CInventoryEntry &Entry = It->second;
		if(Entry.m_Value <= 0)
			return;
		if(Other.m_Remove)
			Other.m_Remove(*this, Other, -1);
		Entry.m_Value = 0;
	});
}

bool CPlayer::UseItem(const char *pName, int OverrideValue, bool Force)
{
	const CItemConfig *pCfg = GameServer()->m_Shop.Registry().FindByName(pName);
	return UseItem(pCfg, OverrideValue, Force);
}

bool CPlayer::UseItem(const CItemConfig *pCfg, int OverrideValue, bool Force)
{
	if(!pCfg)
		return false;
	if(!Acc()->m_LoggedIn && !Force)
		return false;

	// Consumables (loot cases)
	if(HasFlag(pCfg->m_Flags, EItemFlag::LootCase))
	{
		OpenLootCase(*pCfg);
		return true;
	}

	if(!HasFlag(pCfg->m_Flags, EItemFlag::Consumable) && !HasFlag(pCfg->m_Flags, EItemFlag::Equippable))
		return false;

	CInventoryEntry &Entry = Inv()->Entry(pCfg->m_pName);
	const bool CurrentlyEquipped = Entry.m_Value;

	int Equip = OverrideValue >= 0 ? OverrideValue : !CurrentlyEquipped;

	const bool IsUpgrade = HasFlag(pCfg->m_Flags, EItemFlag::Upgrade); // Upgrades aren't cosmetics

	if(!IsUpgrade && ReachedItemLimit(pCfg) && Equip != 0 && !Force)
	{
		SendChat("You have reached the limit of equipped cosmetics. Unequip some other items first.");
		return false;
	}

	if(Equip != 0 && pCfg->m_Group != EExclusiveGroup::None)
	{
		if(!CurrentlyEquipped)
			UnequipExclusiveGroup(pCfg->m_Group, pCfg);
	}

	if(Equip != 0)
	{
		if(pCfg->m_Apply)
			pCfg->m_Apply(*this, *pCfg, OverrideValue);
		Entry.m_Value = Equip;
	}
	else
	{
		if(CurrentlyEquipped && pCfg->m_Remove)
			pCfg->m_Remove(*this, *pCfg, OverrideValue);
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
		SendChat("You are already opening a loot case!");
		return false;
	}

	if(GetTeam() == TEAM_SPECTATORS)
	{
		SendChat("You can't open loot cases while spectating.");
		return false;
	}

	const EItemRarity CaseRarity = CaseCfg.m_Rarity;
	const bool AllowAny = CaseCfg.m_Id == EItemId::LootCaseExotic;

	std::vector<const CItemConfig *> vCandidates;
	int MaxStars = 1;
	for(const auto &Item : GameServer()->m_Shop.Registry().Map())
	{
		const CItemConfig &Other = Item.second;
		if(HasFlag(Other.m_Flags, EItemFlag::LootCase))
			continue;

		if(!AllowAny && Other.m_Rarity != CaseRarity)
			continue;

		vCandidates.push_back(&Item.second); // pointer to registry-owned item
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

		int Idx = 0;
		for(size_t i = 0; i < RarityLevels.size(); i++)
		{
			if(RarityLevels[i] == r)
			{
				Idx = (int)i;
				break;
			}
		}
		if(Idx == EpicIdx || Idx == MythicIdx)
			return 5;
		if(Idx == LastIdx)
			return 3;
		if(Idx == 1)
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

	std::random_device Rd;
	std::uniform_int_distribution<int> DistItem(1, std::max(TotalWeight, 1));
	int Pick = DistItem(Rd);

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

	auto It = Inv()->m_Map.find(CaseCfg.m_pName);
	if(It == Inv()->m_Map.end() || It->second.m_Quantity <= 0)
	{
		SendChat("You don't own this loot case.");
		return false;
	}
	It->second.m_Quantity = std::max(0, It->second.m_Quantity - 1);

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
	int DayPick = DistDay(Rd);
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
	GameServer()->m_Shop.GiveItem(this, pSelectedItem, RewardDays, "Loot Case");

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

void CPlayer::OverrideSnap(int SnappingClient, CNetObj_ClientInfo &ClientInfo)
{
	Overriddename(SnappingClient, ClientInfo);
	RainbowSnap(SnappingClient, ClientInfo);

	if(g_Config.m_SvForceSkin[0])
		StrToInts(ClientInfo.m_aSkin, std::size(ClientInfo.m_aSkin), g_Config.m_SvForceSkin);
}

void CPlayer::RainbowSnap(int SnappingClient, CNetObj_ClientInfo &ClientInfo)
{
	if(!GetCharacter() || (!Cosmetics()->m_RainbowBody && !Cosmetics()->m_RainbowFeet && GetCharacter()->GetPowerHooked() != HOOKTYPE_RAINBOW))
		return;

	if(GetCharacter()->GetPowerHooked() == HOOKTYPE_RAINBOW)
		GetCharacter()->m_IsRainbowHooked = true;

	int BaseColor = m_RainbowColor * 0x010000;
	int Color = 0xff32;

	bool RainbowHooked = GetCharacter()->m_IsRainbowHooked;
	bool Local = SnappingClient == GetCid();

	// only send rainbow updates to people close to you, to reduce network traffic
	if(GameServer()->m_apPlayers[SnappingClient] && !GetCharacter()->NetworkClipped(SnappingClient))
	{
		if(Local || GameServer()->m_aAccounts[SnappingClient].m_Configs.m_Cosmetics.m_ShowRainbow)
		{
			ClientInfo.m_UseCustomColor = 1;
			if(Cosmetics()->m_RainbowBody)
				ClientInfo.m_ColorBody = BaseColor + Color;
			if(Cosmetics()->m_RainbowFeet)
				ClientInfo.m_ColorFeet = BaseColor + Color;
		}

		if(SnappingClient == GetCharacter()->m_PowerHookedId || GameServer()->m_aAccounts[SnappingClient].m_Configs.m_Cosmetics.m_ShowRainbow)
		{
			if(RainbowHooked)
			{
				ClientInfo.m_UseCustomColor = 1;
				ClientInfo.m_ColorBody = BaseColor + Color;
				ClientInfo.m_ColorFeet = BaseColor + Color;
			}
		}
	}
}

void CPlayer::Overriddename(int SnappingClient, CNetObj_ClientInfo &ClientInfo)
{
	if(m_Obfuscated)
	{
		constexpr int MaxBytes = sizeof(ClientInfo.m_aName);
		std::string ObfStr = RandomUnicode(MaxBytes / 3);
		if(ObfStr.size() >= MaxBytes)
			ObfStr.resize(MaxBytes - 1);
		const char *pObf = ObfStr.c_str();

		StrToInts(ClientInfo.m_aName, std::size(ClientInfo.m_aName), pObf);
		StrToInts(ClientInfo.m_aClan, std::size(ClientInfo.m_aClan), " ");
	}

	if(!GetCharacter())
		return;

	if(GetCharacter()->m_InSnake)
	{
		StrToInts(ClientInfo.m_aName, std::size(ClientInfo.m_aName), " ");
		StrToInts(ClientInfo.m_aClan, std::size(ClientInfo.m_aClan), " ");
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

// NOLINTBEGIN(clang-analyzer-unix.Malloc)
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
// NOLINTEND(clang-analyzer-unix.Malloc)

void CPlayer::SetDeathEffect(int Type)
{
	Cosmetics()->m_DeathEffect = Type;
}
void CPlayer::SetDamageIndType(int Type)
{
	Cosmetics()->m_DamageIndType = Type;
}
void CPlayer::SetGunType(EGunType Type)
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

void CPlayer::SetConfettiGun(bool Active)
{
	Cosmetics()->m_ConfettiGun = Active;
}

void CPlayer::SetInvisible(bool Active)
{
	m_Invisible = Active;
}

void CPlayer::SetIgnoreGameLayer(bool Active)
{
	m_IgnoreGamelayer = Active;
}

void CPlayer::SetObfuscated(bool Active)
{
	m_Obfuscated = Active;
}

void CPlayer::SetTelekinesisImmunity(bool Active)
{
	m_TelekinesisImmunity = Active;
}

void CPlayer::SetHidePowerUps(bool Active)
{
	Acc()->m_Configs.m_HidePowerUps = Active;
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

	if(Cosmetics()->m_Ability <= ABILITY_NONE)
		return;

	SendChatFmt("Ability set to %s", GetAbilityName(Cosmetics()->m_Ability));
	SendChat("Use f3 (Vote Yes) to use your Ability");
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
	if(pChr->Core()->m_Solo || pChr->Core()->m_CollisionDisabled || pChr->Core()->m_Passive ||
		pChr->Core()->m_HookHitDisabled || pChr->Core()->m_HammerHitDisabled || pChr->Core()->m_ShotgunHitDisabled ||
		pChr->Core()->m_GrenadeHitDisabled || pChr->Core()->m_LaserHitDisabled ||
		!pChr->GetCurrentTuning()->m_PlayerHammering ||
		!pChr->GetCurrentTuning()->m_PlayerHooking ||
		!pChr->GetCurrentTuning()->m_PlayerCollision)
		Rows++;
	if(pChr->Teams()->IsPractice(pChr->Team()) || pChr->Teams()->TeamLocked(pChr->Team()) || pChr->Core()->m_DeepFrozen || pChr->Core()->m_LiveFrozen)
		Rows++;

	if(GameServer()->m_VoteCloseTime)
		Rows = 3;

	return Rows;
}

// Broadcasts like opening a lootcase or having a bot client are more important than area broadcasts or stuff like that
bool CPlayer::HasImportantBroadcast() const
{
	return m_LootBoxData.m_Opening;
}

void CPlayer::SendBroadcast(const char *pText)
{
	if(!str_comp(m_BroadcastData.m_aMessage, pText) && m_BroadcastData.m_Time + Server()->TickSpeed() * 9 > Server()->Tick())
		return;

	str_copy(m_BroadcastData.m_aMessage, pText);
	m_BroadcastData.m_Time = Server()->Tick();

	GameServer()->SendBroadcast(pText, GetCid());
}

void CPlayer::SendBroadcastHud(const std::vector<std::string> &pMessages, int Offset)
{
	if(pMessages.empty())
		return;
	if(HasImportantBroadcast())
		return; // Other broadcast is being sent

	char aBuf[256] = "";
	Offset = std::max(Offset, 0);
	int NextLines = NumDDraceHudRows() + Offset;

	for(int i = 0; i < NextLines; i++)
		str_append(aBuf, "\n", sizeof(aBuf));

	for(const std::string &Message : pMessages)
	{
		str_append(aBuf, Message.c_str(), sizeof(aBuf));
		str_append(aBuf, "\n", sizeof(aBuf));
	}

	if(!Server()->IsSixup(GetCid()))
		for(int i = 0; i < 137; i++) // 16:9 ratio default font
			str_append(aBuf, " ", sizeof(aBuf));

	SendBroadcast(aBuf);
}

void CPlayer::SendChat(const char *pMsg)
{
	GameServer()->SendChatTarget(GetCid(), pMsg);
}

void CPlayer::SendChatFmt(const char *pFmt, ...)
{
	char aBuf[1024];
	va_list args;
	va_start(args, pFmt);
	str_format_v(aBuf, sizeof(aBuf), pFmt, args);
	va_end(args);
	SendChat(aBuf);
}

void CPlayer::SendAreaMotd(EArea Area)
{
	if(m_LastArea == Area)
		return;

	CCharacter *pChr = GetCharacter();
	if(!pChr)
		return;
	if(pChr->Team() != TEAM_FLOCK)
		return;

	if(Area == EArea::Game)
	{
		ClearBroadcast();
		return;
	}
	if(m_LastAreaMotd + Server()->TickSpeed() * 30 > Server()->Tick() && m_LastAreaMotd > 0)
		return;
	m_LastAreaMotd = Server()->Tick();

	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = "";
	switch(Area)
	{
	case EArea::Roulette:
		Msg.m_pMessage =
			"\n"
			"[Viewable in Server info tab]\n"
			"\n"
			"\n"
			"--  Rᴏᴜʟᴇᴛᴛᴇ  --\n"
			"\n"
			"To start, write '/bet <amount>', after that you can select your bet type by hovering your mouse over any of the options below and hammering\n"
			"\n"
			"Pᴀʏᴏᴜᴛs:\n"
			"Black | Red: 2x\n"
			"3x dozens: 3x\n"
			"Green [Zero]: 14x\n"
			"\n"
			"[Press Tab to hide]";
		break;
	case EArea::HideAndSeek:
		Msg.m_pMessage =
			"[Viewable in Server info tab]\n"
			"\n"
			"\n"
			"--  Hɪᴅᴇ ᴀɴᴅ Sᴇᴇᴋ  --\n"
			"\n"
			"Sᴇᴇᴋᴇʀ:\n"
			"Find all seekers and hammer them, shooting your gun will point to the closest hidden player.\n"
			"\n"
			"Hɪᴅᴇʀ:\n"
			"Dark Areas completely hide you from the seeker, hammering will put you in ghost mode for a short time which allows you to run away\n"
			"\n"
			"Depending on the map, entities will or will not work\n"
			"\n"
			"[Press Tab to hide]";
		break;
	default:
		break;
	}
	if(Msg.m_pMessage[0] == '\0')
		return;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, GetCid());
	SendChat("How to view Area Info: Open Menu -> Server Info -> MOTD");
}

void CPlayer::SetArea(EArea Area)
{
	if(m_Area != EArea::Game)
		m_LastArea = m_Area;
	SendAreaMotd(Area);
	m_Area = Area;
}

bool CPlayer::CanReport()
{
	if(!Acc()->m_LoggedIn)
		return false;

	if(OwnsItem(EItemId::SuperUser))
		return true;

	const int PlaytimeHours = Acc()->m_Playtime / 60;
	bool HasPlayedEnough = PlaytimeHours >= g_Config.m_SvReportsMinPlaytimeForBypass && g_Config.m_SvReportsPlaytimeBypass;

	int ReportDelay = g_Config.m_SvReportsDelay;
	if(HasPlayedEnough)
		ReportDelay *= 0.5f; // reduce cooldown

	if(m_LastReport + Server()->TickSpeed() * ReportDelay > Server()->Tick())
		return false; // cooldown between reports

	const int MinRegisterTime = g_Config.m_SvReportsMinAccountAge * 60;
	int64_t Now = time(0);
	if(Acc()->m_RegisterDate + MinRegisterTime > Now && !HasPlayedEnough)
		return false;

	return true;
}

float CPlayer::GetClientPred() const
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

	if(GameServer()->m_BoostData.m_Ticks > 0)
		Multiplier += GameServer()->m_BoostData.m_Boost;

	if(!Acc()->m_LoggedIn)
		return Multiplier;

	for(int ClientId = 0; ClientId < Server()->MaxClients(); ClientId++)
	{
		if(Server()->ClientSlotEmpty(ClientId))
			continue;
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;
		if(pPlayer->OwnsItem(EItemId::Booster))
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

bool CPlayer::SendToMap(int Idx)
{
	if(!g_Config.m_SvMultimap)
	{
		log_error("multimap", "Failed to send to map index %d: multimap is disabled", Idx);
		return false;
	}
	if(Idx == m_MultiMapIndex)
		return true;

	if((int)GameServer()->m_vMultiMaps.size() < Idx)
		return false;

	if(!GameServer()->m_vMultiMaps[Idx]->m_LoadedSwitchers)
	{
		GameServer()->m_World.InitSwitchers(GameServer()->Collision(Idx)->m_HighestSwitchNumber, Idx);
		for(auto &Switcher : GameServer()->Switchers()[Idx])
			Switcher.m_Initial = true;
		GameServer()->m_vMultiMaps[Idx]->m_LoadedSwitchers = true;
	}
	if(!GameServer()->m_vMultiMaps[Idx]->m_CreatedEntities)
	{
		GameServer()->CreateAllEntities(true, Idx);
		GameServer()->m_vMultiMaps[Idx]->m_CreatedEntities = true;
	}

	int CurMapIndex = MultiMapIdx();

	if(Idx != DefaultMapIndex)
	{
		if(!Server()->SendMapByName(GetCid(), GameServer()->m_vMultiMaps[Idx]->m_pMap->BaseName()))
			return false;
		m_MultiMapIndex = Idx;
	}
	else
	{
		if(!Server()->SendMapByName(GetCid(), GameServer()->Map()->BaseName()))
			return false;
		m_MultiMapIndex = DefaultMapIndex;
	}

	CRoulette *pRoulette = static_cast<CRoulette *>(GameServer()->m_World.FindEntityOnMap(CGameWorld::ENTTYPE_ROULETTE, CurMapIndex));
	if(pRoulette)
	{
		pRoulette->OnClientReset(GetCid());
	}

	SetSpectatorId(-1);
	for(int ClientId = 0; ClientId < Server()->MaxClients(); ClientId++)
	{
		if(Server()->ClientSlotEmpty(ClientId))
			continue;
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(pPlayer && pPlayer->SpectatorId() == GetCid())
			pPlayer->SetSpectatorId(-1);
	}

	CCharacter *pChr = GetCharacter();
	if(pChr)
	{
		pChr->Die(-1, WEAPON_GAME);
	}

	if(MultiMapIdx() != DefaultMapIndex)
		SendChat("Use /exit to leave to the main map.");

	return true;
}

int CPlayer::GetShowOthers()
{
	int CurrentTick = Server()->Tick();

	if(m_ShowOthersCacheTick == CurrentTick)
	{
		return m_CachedShowOthers;
	}

	for(CServerComponent *pComponent : GameServer()->m_vpComponents)
	{
		int Value = pComponent->ShowOthers(this);
		if(Value != -1)
		{
			m_CachedShowOthers = Value;
			m_ShowOthersCacheTick = CurrentTick;
			return Value;
		}
	}

	m_CachedShowOthers = m_ShowOthers;
	m_ShowOthersCacheTick = CurrentTick;
	return m_ShowOthers;
}

void CPlayer::HandleTelekinesis()
{
	int &TeleId = m_TelekinesisId;
	if(!CheckClientId(TeleId))
		return;
	CCharacter *pChr = GetCharacter();

	CCharacter *pTeleChr = GameServer()->GetPlayerChar(TeleId);

	if(!IsPaused() && GetTeam() != TEAM_SPECTATORS)
		m_GrabbedWhileSpec = false;

	if(pTeleChr)
	{
		if(!pTeleChr->GetPlayer())
		{
			TeleId = -1;
			return;
		}
		if(!pTeleChr->IsAlive())
			return;

		if(pTeleChr->GetPlayer()->m_TelekinesisImmunity)
		{
			TeleId = -1;
			return;
		}

		const bool TelekinesisWeapoin = pChr && pChr->GetActiveWeapon() == WEAPON_TELEKINESIS;

		if(TelekinesisWeapoin || Cosmetics()->m_Ability == ABILITY_TELEKINESIS)
		{
			pTeleChr->SetPosition(GetCursorPos(m_GrabbedWhileSpec));
			pTeleChr->ResetVelocity();
		}
		else
		{
			TeleId = -1;
			return;
		}
	}
}


void CPlayer::DoTelekinesis()
{
	bool SpecPos = IsPaused() || GetTeam() == TEAM_SPECTATORS;
	const vec2 CursorPos = GetCursorPos(SpecPos);
	CCharacter *pChr = GetCharacter();

	if(m_TelekinesisId == -1)
	{
		float Zoom = std::max(1.5f, m_CameraInfo.GetZoom());
		CCharacter *pClosest = GameServer()->m_World.ClosestCharacter(CursorPos, CCharacterCore::PhysicalSize() * Zoom, pChr);
		if(!pClosest)
			return; // no one close
		if(!pClosest->IsAlive())
			return; // dead
		for(int i = 0; i < Server()->MaxClients(); i++)
		{
			const CPlayer *pPlayer = GameServer()->m_apPlayers[i];
			if(pPlayer && pPlayer->m_TelekinesisId == pClosest->GetPlayer()->GetCid())
				return; // already telekinesis
		}
		if(GetShowOthers() != SHOW_OTHERS_ON && pChr)
		{
			if(!pChr->Teams()->m_Core.SameTeam(GetCid(), pClosest->GetPlayer()->GetCid()) && pChr->Team() != TEAM_SUPER)
				return; // not same team
		}

		if(pClosest->GetPlayer()->m_TelekinesisId == GetCid())
			return; // dont telekinesis back
		if(pClosest->GetPlayer()->m_TelekinesisImmunity)
			return; // immunity
		m_TelekinesisId = pClosest->GetPlayer()->GetCid();
		m_GrabbedWhileSpec = SpecPos;
	}
	else
		m_TelekinesisId = -1;

	CClientMask &TeamMask = CClientMask().set();
	if(pChr)
		TeamMask = pChr->TeamMask();
	else if(m_TelekinesisId >= 0 && GameServer()->GetPlayerChar(m_TelekinesisId))
		TeamMask = GameServer()->GetPlayerChar(m_TelekinesisId)->TeamMask();

	GameServer()->CreateSound(CursorPos, SOUND_NINJA_HIT, TeamMask);

	m_VoteActionDelay = 125 * Server()->TickSpeed() / 1000;
}


void CPlayer::VoteAction(EVoteAction Action)
{
	if(Server()->ClientSlotEmpty(GetCid()))
		return;

	if(GameServer()->m_VoteCloseTime && (m_Vote == 0 || (m_Vote != 0 && (m_PlayerFlags & PLAYERFLAG_SCOREBOARD))))
		return;

	const int Ability = Cosmetics()->m_Ability;

	const bool NoCooldown = Server()->GetAuthedState(GetCid());

	const bool F3 = Action == EVoteAction::Yes;
	const bool F4 = Action == EVoteAction::No;

	CCharacter *pChr = GetCharacter();

	if(F3 && (m_VoteActionDelay <= 0 || NoCooldown))
	{
		if(pChr && Ability == ABILITY_FIREWORK)
		{
			new CFirework(pChr->GameWorld(), GetCid(), pChr->GetPos());
			m_VoteActionDelay = Server()->TickSpeed() * 3;
		}
		else if(Ability == ABILITY_TELEKINESIS)
			DoTelekinesis();
	}

	if(IsPaused())
		return;

	const bool WeaponDropsEnabled = g_Config.m_SvWeaponDrops && g_Config.m_SvWeaponDropsVoteNo;
	const bool AccountDropsEnabled = Acc()->m_LoggedIn && Acc()->m_Configs.m_WeaponDropsUsingVoteNo;

	if(F4 && pChr && WeaponDropsEnabled && AccountDropsEnabled)
	{
		const vec2 Dir = normalize(vec2(pChr->Input()->m_TargetX, pChr->Input()->m_TargetY));
		const int Type = pChr->GetActiveWeapon();

		pChr->DropWeapon(Type, pChr->GetVelocity() * 0.7f + Dir * vec2(5.0f, 6.0f));
	}
}

vec2 CPlayer::GetCursorPos(bool UseSpecPosIfPaused)
{
	CCharacter *pChr = GetCharacter();
	const vec2 ViewPos = m_ViewPos;
	vec2 CursorPos = m_ViewPos;
	if(pChr)
		CursorPos = pChr->GetCursorPos();
	if(UseSpecPosIfPaused && IsPaused())
		CursorPos = ViewPos;

	return CursorPos;
}