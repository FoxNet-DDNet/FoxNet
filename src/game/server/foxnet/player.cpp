﻿#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/teams.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include "cosmetics/dot_trail.h"
#include "cosmetics/epic_circle.h"
#include "cosmetics/heart_hat.h"
#include "cosmetics/lovely.h"
#include "cosmetics/pickup_pet.h"
#include "cosmetics/rotating_ball.h"
#include "cosmetics/lissajous.h"
#include "cosmetics/staff_ind.h"
#include "entities/text/text.h"

#include <base/vmath.h>
#include <base/system.h>
#include <base/str.h>

#include <string>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <vector>

#include "accounts.h"
#include "shop.h"

CAccountSession *CPlayer::Acc() { return &GameServer()->m_aAccounts[m_ClientId]; }
CInventory *CPlayer::Inv() { return &Acc()->m_Inventory; }
CCosmetics *CPlayer::Cosmetics() { return &Acc()->m_Inventory.m_Cosmetics; }

void CPlayer::FoxNetTick()
{
	RainbowTick();

	if(m_LastBet + Server()->TickSpeed() * 30 < Server()->Tick())
		m_BetAmount = -1; // Reset bet amount every 30 seconds

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

void CPlayer::FoxNetReset()
{
	m_IncludeServerInfo = -1;
	m_HideCosmetics = false;
	m_HidePowerUps = false;

	m_ExtraPing = false;
	m_Obfuscated = false;
	m_Vanish = false;
	m_IgnoreGamelayer = false;
	m_TelekinesisImmunity = false;
	m_SpiderHook = false;

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

void CPlayer::GivePlaytime(int Amount)
{
	if(!Acc()->m_LoggedIn)
		return;

	Acc()->m_Playtime++;
	if(Acc()->m_Playtime % 60 == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "for reaching %ld Minutes of Playtime!", Acc()->m_Playtime);
		GiveMoney(g_Config.m_SvPlaytimeMoney, aBuf, false);
	}
}

void CPlayer::GiveXP(long Amount, const char *pMessage)
{
	if(!Acc()->m_LoggedIn)
		return;

	if(GameServer()->IsWeekend())
		Amount *= 2;

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

	// Level up as long as we have enough XP for the current level
	while(true)
	{
		const int NeededXp = GameServer()->m_AccountManager.NeededXP((int)Acc()->m_Level);
		if(Acc()->m_XP < NeededXp)
			break;

		Acc()->m_Level++;
		Acc()->m_XP -= NeededXp;

		GiveMoney(g_Config.m_SvLevelUpMoney, "", false);
		LeveledUp = true;
	}

	if(LeveledUp && !Silent)
	{
		str_format(aBuf, sizeof(aBuf), "You are now level %ld!", Acc()->m_Level);
		GameServer()->SendChatTarget(m_ClientId, aBuf);

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && i != m_ClientId)
			{
				str_format(aBuf, sizeof(aBuf), "%s is now level %ld!", Server()->ClientName(m_ClientId), Acc()->m_Level);
				GameServer()->SendChatTarget(i, aBuf);
			}
		}

		GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());

		if(GetCharacter())
			GameServer()->CreateBirthdayEffect(GetCharacter()->GetPos(), GetCharacter()->TeamMask());
	}

	return LeveledUp;
}

void CPlayer::GiveMoney(long Amount, const char *pMessage, bool Multiplier)
{
	if(!Acc()->m_LoggedIn)
		return;
	if(Amount <= 0)
		return;

	if(GameServer()->IsWeekend() && Multiplier)
		Amount *= 2.0f;

	Acc()->m_Money += Amount;

	char aBuf[256];

	if(pMessage[0])
	{
		str_format(aBuf, sizeof(aBuf), "+%ld%s %s", Amount, g_Config.m_SvCurrencyName, pMessage);
		GameServer()->SendChatTarget(m_ClientId, aBuf);
	}	

	CCharacter *pChr = GetCharacter();
	if(pChr)
	{
		const vec2 Pos = pChr->m_Pos + vec2(0, -74);
		char aText[66];
		str_format(aText, sizeof(aText), "+%ld", Amount);
		new CProjectileText(pChr->GameWorld(), Pos, GetCid(), 175, aText, WEAPON_HAMMER);
		pChr->SetEmote(EMOTE_HAPPY, Server()->Tick() + 175);
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());
}

void CPlayer::TakeMoney(long Amount, bool Silent, const char *pMessage)
{
	if(!Acc()->m_LoggedIn)
		return;
	if(Amount <= 0)
		return;

	Acc()->m_Money -= Amount;

	char aBuf[256];

	if(pMessage[0])
	{
		str_format(aBuf, sizeof(aBuf), "-%ld%s %s", Amount, g_Config.m_SvCurrencyName, pMessage);
		GameServer()->SendChatTarget(m_ClientId, aBuf);
	}

	CCharacter *pChr = GetCharacter();
	if(!Silent && pChr)
	{
		const vec2 Pos = pChr->m_Pos + vec2(0, -74);
		char aText[66];
		str_format(aText, sizeof(aText), "-%ld", Amount);
		new CProjectileText(pChr->GameWorld(), Pos, GetCid(), 125, aText, WEAPON_HAMMER);
		pChr->SetEmote(EMOTE_PAIN, Server()->Tick() + 125);
	}

	GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());
}

bool CPlayer::OwnsItem(const char *pItemName)
{
	if(!Acc()->m_LoggedIn)
		return false;

	return Acc()->m_Inventory.Owns(pItemName);
}

int CPlayer::GetItemToggle(const char *pItemName)
{
	int Value = -1;

	char pItem[64];
	str_copy(pItem, pItemName);
	if(str_comp(GameServer()->m_Shop.NameToShortcut(pItem), "") != 0)
		str_copy(pItem, GameServer()->m_Shop.NameToShortcut(pItem));

	if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_SPARKLE]))
		Value = (int)!Cosmetics()->m_Sparkle;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_HEARTHAT]))
		Value = (int)!Cosmetics()->m_HeartHat;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_INVERSEAIM]))
		Value = (int)!Cosmetics()->m_InverseAim;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_LOVELY]))
		Value = (int)!Cosmetics()->m_Lovely;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_ROTATINGBALL]))
		Value = (int)!Cosmetics()->m_RotatingBall;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_RAINBOW_FEET]))
		Value = (int)!Cosmetics()->m_RainbowFeet;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_RAINBOW_BODY]))
		Value = (int)!Cosmetics()->m_RainbowBody;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_RAINBOW_HOOK]))
		Value = (int)Cosmetics()->m_HookPower == HOOK_RAINBOW ? HOOK_NORMAL : HOOK_RAINBOW;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_GUN_EMOTICON]))
		Value = (int)Cosmetics()->m_EmoticonGun;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_PHASE_GUN]))
		Value = (int)!Cosmetics()->m_PhaseGun;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_HEART_GUN]))
		Value = (int)!Cosmetics()->m_GunType == GUN_HEART ? GUN_NONE : GUN_HEART;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_MIXED_GUN]))
		Value = (int)!Cosmetics()->m_GunType == GUN_MIXED ? GUN_NONE : GUN_MIXED;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_LASER_GUN]))
		Value = (int)!Cosmetics()->m_GunType == GUN_LASER ? GUN_NONE : GUN_LASER;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_TRAIL_STAR]))
		Value = (int)Cosmetics()->m_Trail == TRAIL_STAR ? TRAIL_NONE : TRAIL_STAR;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_TRAIL_DOT]))
		Value = (int)Cosmetics()->m_Trail == TRAIL_DOT ? TRAIL_NONE : TRAIL_DOT;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_CLOCKWISE]))
		Value = (int)Cosmetics()->m_DamageIndType == IND_CLOCKWISE ? IND_NONE : IND_CLOCKWISE;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_COUNTERCLOCKWISE]))
		Value = (int)Cosmetics()->m_DamageIndType == IND_COUNTERWISE ? IND_NONE : IND_COUNTERWISE;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_INWARD_TURNING]))
		Value = (int)Cosmetics()->m_DamageIndType == IND_INWARD ? IND_NONE : IND_INWARD;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_OUTWARD_TURNING]))
		Value = (int)Cosmetics()->m_DamageIndType == IND_OUTWARD ? IND_NONE : IND_OUTWARD;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_LINE]))
		Value = (int)Cosmetics()->m_DamageIndType == IND_LINE ? IND_NONE : IND_LINE;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_CRISSCROSS]))
		Value = (int)Cosmetics()->m_DamageIndType == IND_CRISSCROSS ? IND_NONE : IND_CRISSCROSS;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_EXPLOSIVE]))
		Value = (int)Cosmetics()->m_DeathEffect == DEATH_EXPLOSION ? DEATH_NONE : DEATH_EXPLOSION;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_HAMMERHIT]))
		Value = (int)Cosmetics()->m_DeathEffect == DEATH_HAMMERHIT ? DEATH_NONE : DEATH_HAMMERHIT;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_INDICATOR]))
		Value = (int)Cosmetics()->m_DeathEffect == DEATH_DAMAGEIND ? DEATH_NONE : DEATH_DAMAGEIND;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_LASER]))
		Value = (int)Cosmetics()->m_DeathEffect == DEATH_LASER ? DEATH_NONE : DEATH_LASER;

	return Value;
}

bool CPlayer::ItemEnabled(const char *pItemName)
{
	int Value = false;

	char pItem[64];
	str_copy(pItem, pItemName);
	if(str_comp(GameServer()->m_Shop.NameToShortcut(pItem), "") != 0)
		str_copy(pItem, GameServer()->m_Shop.NameToShortcut(pItem));

	if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_SPARKLE]))
		Value = Cosmetics()->m_Sparkle;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_HEARTHAT]))
		Value = Cosmetics()->m_HeartHat;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_INVERSEAIM]))
		Value = Cosmetics()->m_InverseAim;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_LOVELY]))
		Value = Cosmetics()->m_Lovely;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_OTHER_ROTATINGBALL]))
		Value = Cosmetics()->m_RotatingBall;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_RAINBOW_FEET]))
		Value = Cosmetics()->m_RainbowFeet;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_RAINBOW_BODY]))
		Value = Cosmetics()->m_RainbowBody;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_RAINBOW_HOOK]))
		Value = Cosmetics()->m_HookPower == HOOK_RAINBOW;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_GUN_EMOTICON]))
		Value = Cosmetics()->m_EmoticonGun;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_PHASE_GUN]))
		Value = Cosmetics()->m_PhaseGun;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_HEART_GUN]))
		Value = Cosmetics()->m_GunType == GUN_HEART;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_MIXED_GUN]))
		Value = Cosmetics()->m_GunType == GUN_MIXED;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_LASER_GUN]))
		Value = Cosmetics()->m_GunType == GUN_LASER;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_TRAIL_STAR]))
		Value = Cosmetics()->m_Trail == TRAIL_STAR;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_TRAIL_DOT]))
		Value = Cosmetics()->m_Trail == TRAIL_DOT;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_CLOCKWISE]))
		Value = Cosmetics()->m_DamageIndType == IND_CLOCKWISE;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_COUNTERCLOCKWISE]))
		Value = Cosmetics()->m_DamageIndType == IND_COUNTERWISE;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_INWARD_TURNING]))
		Value = Cosmetics()->m_DamageIndType == IND_INWARD;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_OUTWARD_TURNING]))
		Value = Cosmetics()->m_DamageIndType == IND_OUTWARD;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_LINE]))
		Value = Cosmetics()->m_DamageIndType == IND_LINE;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_INDICATOR_CRISSCROSS]))
		Value = Cosmetics()->m_DamageIndType == IND_CRISSCROSS;

	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_EXPLOSIVE]))
		Value = Cosmetics()->m_DeathEffect == DEATH_EXPLOSION;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_HAMMERHIT]))
		Value = Cosmetics()->m_DeathEffect == DEATH_HAMMERHIT;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_INDICATOR]))
		Value = Cosmetics()->m_DeathEffect == DEATH_DAMAGEIND;
	else if(!str_comp_nocase(pItem, ItemShortcuts[C_DEATH_LASER]))
		Value = Cosmetics()->m_DeathEffect == DEATH_LASER;

	return Value;
}
bool CPlayer::ReachedItemLimit(const char *pItem, int Set, int Value)
{
	if(Server()->GetAuthedState(GetCid()) >= AUTHED_MOD)
		return false;

	if(Set == 0 || Value == 0)
		return false;

	int Amount = 0;

	if(Cosmetics()->m_Lovely)
		Amount++;
	if(Cosmetics()->m_RotatingBall)
		Amount++;
	if(Cosmetics()->m_Sparkle)
		Amount++;
	if(Cosmetics()->m_HookPower > 0)
		Amount++;
	if(Cosmetics()->m_Bloody || Cosmetics()->m_StrongBloody)
		Amount++;
	if(Cosmetics()->m_InverseAim)
		Amount++;
	if(Cosmetics()->m_HeartHat)
		Amount++;

	if(Cosmetics()->m_DeathEffect > 0)
		Amount++;
	if(Cosmetics()->m_DamageIndType > 0)
		Amount++;

	if(Cosmetics()->m_EmoticonGun > 0)
		Amount++;
	if(Cosmetics()->m_ConfettiGun)
		Amount++;
	if(Cosmetics()->m_PhaseGun)
		Amount++;
	if(Cosmetics()->m_GunType > 0)
		Amount++;

	if(Cosmetics()->m_Trail > 0)
		Amount++;
	if(Cosmetics()->m_RainbowBody || Cosmetics()->m_RainbowFeet)
		Amount++;

	if(Cosmetics()->m_StaffInd)
		Amount++;
	if(Cosmetics()->m_PickupPet)
		Amount++;

	return Amount >= g_Config.m_SvCosmeticLimit;
}


bool CPlayer::ToggleItem(const char *pItemName, int Set, bool IgnoreAccount)
{
	if(!g_Config.m_SvCosmetics)
	{
		GameServer()->SendChatTarget(GetCid(), "Cosmetics are currently disabled");
		return false;
	}
	if(!Acc()->m_LoggedIn && !IgnoreAccount)
		return false;
	if(m_HideCosmetics)
		return false;

	char Item[64];
	str_copy(Item, pItemName);
	if(str_comp(GameServer()->m_Shop.NameToShortcut(Item), "") != 0)
		str_copy(Item, GameServer()->m_Shop.NameToShortcut(Item));

	if(!OwnsItem(Item) && !IgnoreAccount)
		return false;

	int Value = GetItemToggle(Item);
	if(Value == -1 && Set == -1)
		return false;
	if(ReachedItemLimit(pItemName, Set, Value) && Value != 0)
	{
		GameServer()->SendChatTarget(GetCid(), "You have reached the item limit! Disable another item first.");
		return false;
	}

	// Expire Item
	int Idx = Inv()->IndexOfName(pItemName);
	int64_t Now = time(0);
	int64_t Expiry = Inv()->m_ExpiresAt[Idx];
	if(Expiry == 0) // Old items without expiry date
	{
		Inv()->SetExpiresAt(Idx, Now + (30 * 24 * 60 * 60)); // 30 days
		GameServer()->m_AccountManager.SaveAccountsInfo(m_ClientId, *Acc());
	}
	else
	{
		int64_t Remaining = Expiry - Now;
		if(Remaining <= 0 && Expiry == -1)
		{
			Inv()->SetOwnedIndex(Idx, false);
			Inv()->SetAcquiredAt(Idx, 0);
			Inv()->SetExpiresAt(Idx, -1);
			GameServer()->m_AccountManager.RemoveItem(Acc()->m_aUsername, pItemName);
			return false;
		}
	}

	if(!str_comp_nocase(Item, ItemShortcuts[C_OTHER_SPARKLE]))
		SetSparkle(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_OTHER_HEARTHAT]))
		SetHeartHat(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_OTHER_INVERSEAIM]))
		SetInverseAim(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_OTHER_LOVELY]))
		SetLovely(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_OTHER_ROTATINGBALL]))
		SetRotatingBall(Value);

	else if(!str_comp_nocase(Item, ItemShortcuts[C_RAINBOW_FEET]))
		SetRainbowFeet(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_RAINBOW_BODY]))
		SetRainbowBody(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_RAINBOW_HOOK]))
		HookPower(Value);

	else if(!str_comp_nocase(Item, ItemShortcuts[C_GUN_EMOTICON]))
	{
		Value = Set;
		SetEmoticonGun(Value);
	}
	else if(!str_comp_nocase(Item, ItemShortcuts[C_PHASE_GUN]))
		SetPhaseGun(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_HEART_GUN]))
		SetGunType(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_MIXED_GUN]))
		SetGunType(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_LASER_GUN]))
		SetGunType(Value);

	else if(!str_comp_nocase(Item, ItemShortcuts[C_TRAIL_STAR]))
		SetTrail(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_TRAIL_DOT]))
		SetTrail(Value);

	else if(!str_comp_nocase(Item, ItemShortcuts[C_INDICATOR_CLOCKWISE]))
		SetDamageIndType(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_INDICATOR_COUNTERCLOCKWISE]))
		SetDamageIndType(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_INDICATOR_INWARD_TURNING]))
		SetDamageIndType(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_INDICATOR_OUTWARD_TURNING]))
		SetDamageIndType(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_INDICATOR_LINE]))
		SetDamageIndType(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_INDICATOR_CRISSCROSS]))
		SetDamageIndType(Value);

	else if(!str_comp_nocase(Item, ItemShortcuts[C_DEATH_EXPLOSIVE]))
		SetDeathEffect(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_DEATH_HAMMERHIT]))
		SetDeathEffect(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_DEATH_INDICATOR]))
		SetDeathEffect(Value);
	else if(!str_comp_nocase(Item, ItemShortcuts[C_DEATH_LASER]))
		SetDeathEffect(Value);

	if(!IgnoreAccount)
		Acc()->m_Inventory.SetEquippedIndex(Acc()->m_Inventory.IndexOfShortcut(Item), Value);

	return true;
}

void CPlayer::RainbowSnap(int SnappingClient, CNetObj_ClientInfo *pClientInfo)
{
	if(!GetCharacter() || (!Cosmetics()->m_RainbowBody && !Cosmetics()->m_RainbowFeet && GetCharacter()->GetPowerHooked() != HOOK_RAINBOW))
		return;

	if(GetCharacter()->GetPowerHooked() == HOOK_RAINBOW)
		GetCharacter()->m_IsRainbowHooked = true;

	int BaseColor = m_RainbowColor * 0x010000;
	int Color = 0xff32;

	// only send rainbow updates to people close to you, to reduce network traffic
	if(GameServer()->m_apPlayers[SnappingClient] && !GetCharacter()->NetworkClipped(SnappingClient))
	{
		if(!GameServer()->m_apPlayers[SnappingClient]->m_HideCosmetics)
		{
			pClientInfo->m_UseCustomColor = 1;
			if(Cosmetics()->m_RainbowBody || GetCharacter()->m_IsRainbowHooked)
				pClientInfo->m_ColorBody = BaseColor + Color;
			if(Cosmetics()->m_RainbowFeet || GetCharacter()->m_IsRainbowHooked)
				pClientInfo->m_ColorFeet = BaseColor + Color;
		}
	}
}

void CPlayer::RainbowTick()
{
	if(!GetCharacter() || (!Cosmetics()->m_RainbowBody && !Cosmetics()->m_RainbowFeet && GetCharacter()->GetPowerHooked() != HOOK_RAINBOW))
		return;

	if(Cosmetics()->m_RainbowSpeed < 1)
		Cosmetics()->m_RainbowSpeed = 1;

	if(Server()->Tick() % 2 == 1)
		m_RainbowColor = (m_RainbowColor + Cosmetics()->m_RainbowSpeed) % 256;
}

void CPlayer::OverrideName(int SnappingClient, CNetObj_ClientInfo *pClientInfo)
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
	if(Cosmetics()->m_Trail == TRAIL_DOT)
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

void CPlayer::SetHeartHat(bool Active)
{
	if(Cosmetics()->m_HeartHat == Active)
		return;
	Cosmetics()->m_HeartHat = Active;
	const vec2 Pos = GetCharacter() ? GetCharacter()->GetPos() : vec2(0, 0);
	if(Cosmetics()->m_HeartHat)
		new CHeartHat(&GameServer()->m_World, GetCid(), Pos);
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
	if(Cosmetics()->m_HookPower == HOOK_NORMAL && Extra == HOOK_NORMAL)
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

void CPlayer::SetHideCosmetics(bool Set)
{
	m_HideCosmetics = Set;
	if(!Acc()->m_LoggedIn)
		return;
	if(Set)
	{
		Acc()->m_Flags |= ACC_FLAG_HIDE_COSMETICS;
		GameServer()->SendChatTarget(m_ClientId, "Cosmetics will be hidden");
		DisableAllCosmetics();
	}
	else
	{
		Acc()->m_Flags &= ~ACC_FLAG_HIDE_COSMETICS;
		GameServer()->SendChatTarget(m_ClientId, "Cosmetics will show");
	}
}

void CPlayer::SetHidePowerUps(bool Set)
{
	m_HidePowerUps = Set;
	if(!Acc()->m_LoggedIn)
		return;
	if(Set)
	{
		Acc()->m_Flags |= ACC_FLAG_HIDE_POWERUPS;
		GameServer()->SendChatTarget(m_ClientId, "PowerUps will be hidden");
	}
	else
	{
		Acc()->m_Flags &= ~ACC_FLAG_HIDE_POWERUPS;
		GameServer()->SendChatTarget(m_ClientId, "PowerUps will show");
	}
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

	GameServer()->SendChatTarget(ClientId, "Use f4 (Vote No) to use your Ability");
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

	return Rows;
}


void CPlayer::SendBroadcastHud(std::vector<std::string> pMessages, int Offset)
{
	if(pMessages.empty())
		return;

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

	if(!str_comp(m_BroadcastData.m_aMessage, aBuf) && m_BroadcastData.m_Time + Server()->TickSpeed() * 9 > Server()->Tick())
		return;

	SendBroadcast(aBuf);
}

void CPlayer::SendBroadcast(const char *pText)
{
	str_copy(m_BroadcastData.m_aMessage, pText);
	m_BroadcastData.m_Time = Server()->Tick();

	GameServer()->SendBroadcast(pText, GetCid());
}

float CPlayer::GetClientPred()
{
	float Ping = (m_Latency.m_Min) / 10.0f + 1.2f;
	return std::max(Ping + m_PredMargin, 4.0f);
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
	case 1:
		Msg.m_pMessage =
			"\n"
			"[Veiwable in Server info Tab]\n"
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
	SendAreaMotd(Area);
	m_Area = Area;
}