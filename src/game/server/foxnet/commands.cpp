#include "entities/pickupdrop.h"

#include <base/log.h>
#include <base/net.h>
#include <base/str.h>
#include <base/system.h>
#include <base/time.h>
#include <base/types.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/foxnet/components/votemenu.h>
#include <game/server/foxnet/entities/text/text.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/score.h>

#include <algorithm>
#include <cstdint>
#include <ctime>

void CGameContext::ConGiveMoney(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;

	if(!g_Config.m_SvAccounts)
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	if(!pPlayer->Acc()->m_LoggedIn)
		return;

	const int Amount = pResult->GetInteger(1);
	pPlayer->GiveMoney(Amount, false);
}

void CGameContext::ConPayMoney(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int UserId = pResult->m_ClientId;
	const char *pName = pResult->GetString(0);
	if(!CheckClientId(UserId))
		return;

	if(!g_Config.m_SvAccounts)
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[UserId];
	if(!pPlayer)
		return;
	if(!pPlayer->Acc()->m_LoggedIn)
	{
		pSelf->SendChatTarget(UserId, "You need to be logged in for this.");
		return;
	}
	int Victim = pSelf->ClientIdByName(pName);
	if(!CheckClientId(Victim))
	{
		pSelf->SendChatTarget(UserId, "Player not found");
		return;
	}
	CPlayer *pVictim = pSelf->m_apPlayers[Victim];
	if(!pVictim)
	{
		pSelf->SendChatTarget(UserId, "Player not found");
		return;
	}
	if(Victim == UserId)
	{
		pSelf->SendChatTarget(UserId, "You can't pay yourself");
		return;
	}
	if(!pVictim->Acc()->m_LoggedIn)
	{
		pSelf->SendChatTarget(UserId, "Player isn't logged in");
		return;
	}

	const int Amount = pResult->GetInteger(1);
	pPlayer->PayMoney(pVictim, Amount);
}

void CGameContext::ConGiveXp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	if(!pPlayer->Acc()->m_LoggedIn)
		return;

	const int Amount = pResult->GetInteger(1);
	if(Amount > 0)
		pPlayer->GiveXP(Amount, "", false);
}

void CGameContext::ConAddChatDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	const char *Reason = pResult->GetString(1);
	bool Ban = pResult->GetInteger(2);
	int BanTime = pResult->GetInteger(3);
	float Addition = pResult->GetFloat(4);
	if(BanTime < 0)
	{
		log_info("chat-detection", "Ban time must be greater than 0");
		return;
	}
	if(Addition <= 0.0f)
		Addition = 1.0f;
	for(const auto &Words : pSelf->m_vChatDetection)
	{
		if(Words.String()[0] == '\0')
			continue;
		if(!str_comp_nocase(Words.String(), String))
		{
			log_info("chat-detection", "String \"%s\" already exists in the list", String);
			return;
		}
	}

	if(str_comp_nocase(String, "") != 0)
	{
		pSelf->m_vChatDetection.push_back(CStringDetection(String, Reason, Addition, Ban, BanTime));
		if(pResult->m_ClientId >= 0)
			log_info("chat-detection", "Added \"%s\" to the Chat Detection List", String);
	}
}

void CGameContext::ConClearChatDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_vChatDetection.clear();
}

void CGameContext::ConRemoveChatDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	pSelf->RemoveChatDetectionString(String);
}

void CGameContext::RemoveChatDetectionString(const char *pString)
{
	if(pString[0] == '\0')
		return;

	if(m_vChatDetection.empty())
	{
		log_info("chat-detection", "List is Empty");
		return;
	}

	for(auto It = m_vChatDetection.begin(); It != m_vChatDetection.end(); ++It)
	{
		if(!str_comp_nocase(It->String(), pString))
		{
			m_vChatDetection.erase(It);
			log_info("chat-detection", "Removed \"%s\" from the Chat Detection List", pString);
			return;
		}
	}
}

void CGameContext::ConListChatDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_vChatDetection.empty())
	{
		log_info("chat-detection", "List is Empty");
		return;
	}

	for(const auto &Words : pSelf->m_vChatDetection)
	{
		if(Words.String()[0] == '\0')
			continue;

		log_info("chat-detection", "Str: %s | Reas: %s | Time: %d | Bans: %s | CountAdd: %.1f", Words.String(), Words.Reason(), Words.Time(), Words.IsBan() ? "Yes" : "No", Words.Addition());
	}
}

void CGameContext::ConAddNameDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	const char *Reason = pResult->GetString(1);
	int BanTime = pResult->GetInteger(2);
	int ExactName = pResult->NumArguments() > 3 ? pResult->GetInteger(3) : 0;
	if(BanTime < 0)
	{
		log_info("name-detection", "Ban time must be greater than 0");
		return;
	}
	if(ExactName < 0 || ExactName > 2)
	{
		log_info("name-detection", "Exact Name must be between 0 and 2");
		log_info("name-detection", "0=search for string | 1=full name match case sensitive | 2=1 but case insensitive");
		return;
	}

	for(const auto &Words : pSelf->m_vNameDetection)
	{
		if(Words.String()[0] == '\0')
			continue;
		if(!str_comp_nocase(Words.String(), String))
		{
			log_info("name-detection", "Name \"%s\" already exists in the list", String);
			return;
		}
	}

	if(str_comp_nocase(String, "") != 0)
	{
		pSelf->m_vNameDetection.push_back(CStringDetection(String, Reason, 1, BanTime, ExactName));
		if(pResult->m_ClientId >= 0)
			log_info("name-detection", "Added \"%s\" to the Name Detection List", String);
	}
}

void CGameContext::ConClearNameDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_vNameDetection.clear();
}

void CGameContext::ConRemoveNameDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	pSelf->RemoveNameDetectionString(String);
}

void CGameContext::RemoveNameDetectionString(const char *pString)
{
	if(pString[0] == '\0')
		return;
	if(m_vNameDetection.empty())
	{
		log_info("name-detection", "List is Empty");
		return;
	}

	for(auto It = m_vNameDetection.begin(); It != m_vNameDetection.end(); ++It)
	{
		if(!str_comp(It->String(), pString))
		{
			m_vNameDetection.erase(It);
			log_info("name-detection", "Removed \"%s\" from the Name Detection List", pString);
			return;
		}
	}
}

void CGameContext::ConListNameDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_vNameDetection.empty())
	{
		log_info("name-detection", "List is Empty");
		return;
	}

	for(const auto &Words : pSelf->m_vNameDetection)
	{
		if(Words.String()[0] == '\0')
			continue;

		log_info("name-detection", "Str: %s | Reas: %s | Time: %d | Exact: %d", Words.String(), Words.Reason(), Words.Time(), Words.ExactMatch());
	}
}

void CGameContext::ConToggleItem(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];

	if(!pPlayer)
		return;

	const char *pItem = pResult->GetString(0);
	const int Value = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : -1;

	pPlayer->UseItem(pItem, Value);
}

void CGameContext::ConRainbowBody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_RainbowBody;
	pPlayer->SetRainbowBody(Set);
	log_info("cosmetics", "Set rainbow body to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConRainbowFeet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_RainbowFeet;
	pPlayer->SetRainbowFeet(Set);
	log_info("cosmetics", "Set rainbow feet to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConRainbowSpeed(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	if(pResult->NumArguments() < 2)
	{
		log_info("cosmetics", "Speed: %d", pPlayer->Cosmetics()->m_RainbowSpeed);
	}
	else
	{
		int Speed = std::clamp(pResult->GetInteger(1), 1, 200);
		log_info("cosmetics", "Rainbow speed for '%s' changed to %d", pSelf->Server()->ClientName(Victim), Speed);
		pPlayer->Cosmetics()->m_RainbowSpeed = Speed;
	}
}

void CGameContext::ConSparkle(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Sparkle;
	pPlayer->SetSparkle(Set);
	log_info("cosmetics", "Set sparkle to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConDotTrail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	int Trail = pPlayer->Cosmetics()->m_Trail == TRAILTYPE_DOT ? TRAILTYPE_NONE : TRAILTYPE_DOT;

	pPlayer->SetTrail(Trail);
	log_info("cosmetics", "Set trail to %d for player %s", Trail, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConStarTrail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Trail = pPlayer->Cosmetics()->m_Trail == TRAILTYPE_STAR ? TRAILTYPE_NONE : TRAILTYPE_STAR;

	pPlayer->SetTrail(Trail);
	log_info("cosmetics", "Set star trail to %d for player %s", Trail, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConInverseAim(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_InverseAim;
	pPlayer->SetInverseAim(Set);
	log_info("cosmetics", "Set inverse aim to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConLovely(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Lovely;
	pPlayer->SetLovely(Set);
	log_info("cosmetics", "Set lovely to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConRotatingBall(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_RotatingBall;
	pPlayer->SetRotatingBall(Set);
	log_info("cosmetics", "Set rotating ball to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConEpicCircle(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_EpicCircle;
	pPlayer->SetEpicCircle(Set);
	log_info("cosmetics", "Set epic circle to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConBloody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Bloody;
	pPlayer->SetBloody(Set);
	log_info("cosmetics", "Set bloody to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHeartHat(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_HeartHat;
	pPlayer->SetHeartHat(Set);
	log_info("cosmetics", "Set heart hat to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConDeathEffect(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() < 1 ? 0 : pResult->GetInteger(0);
	pPlayer->SetDeathEffect(Type);
	log_info("cosmetics", "Set death effect to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConDamageIndType(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() < 1 ? 0 : pResult->GetInteger(0);
	pPlayer->SetDamageIndType(Type);
	log_info("cosmetics", "Set damage ind to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConGunType(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() < 1 ? 0 : pResult->GetInteger(0);
	pPlayer->SetGunType(Type);
	log_info("cosmetics", "Set gun type to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHatType(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0;
	pPlayer->SetHatType((EHatType)Type);
	log_info("cosmetics", "Set hat type to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConStaffInd(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_StaffInd;
	pPlayer->SetStaffInd(Set);
	log_info("cosmetics", "Set staff indicator to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetPickupPet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;
	bool Set = !pPlayer->Cosmetics()->m_PickupPet;
	pPlayer->SetPickupPet(Set);
	log_info("cosmetics", "Set pickup pet to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConLissajous(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Lissajous;
	pPlayer->SetLissajous(Set);
	log_info("cosmetics", "Set lissajous to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHalo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Halo;
	pPlayer->SetHalo(Set);
	log_info("cosmetics", "Set halo to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHookPower(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Power = -1;
	if(pResult->NumArguments() && pSelf->IsValidHookPower(pResult->GetInteger(0)))
		Power = pResult->GetInteger(0);
	if(Power == -1)
	{
		log_info("console", "~~~ Hook Powers ~~~");
		for(int i = 0; i < NUM_HOOKTYPES; i++)
		{
			log_info("hook-power", "%d = %s", i, pSelf->HookTypeName(i));
		}
	}
	else
	{
		if(pPlayer->Cosmetics()->m_HookPower == Power)
			Power = HOOKTYPE_NORMAL;
		pPlayer->HookPower(Power);
		log_info("hook-power", "%s", pSelf->HookTypeName(Power));
	}
}

void CGameContext::ConInvisible(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->m_Invisible;
	pPlayer->SetInvisible(Set);
	log_info("cosmetics", "Set invisible to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConStrongBloody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_StrongBloody;
	pPlayer->SetStrongBloody(Set);
	log_info("cosmetics", "Set strong bloody to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSnake(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;
	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	bool Set = !pChr->m_Snake.Active();
	pChr->SetSnake(!pChr->m_Snake.Active());
	log_info("cosmetics", "Set snake to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHidePowerUps(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Acc()->m_Configs.m_HidePowerUps;
	pPlayer->SetHidePowerUps(Set);
	log_info("cosmetics", "Set hide powerups to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetEmoticonGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Set = pResult->GetInteger(0);
	pPlayer->SetEmoticonGun(Set);
	log_info("cosmetics", "Set emote gun to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConPhaseGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_PhaseGun;
	pPlayer->SetPhaseGun(Set);
	log_info("cosmetics", "Set phase gun to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetConfettiGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;
	bool Set = !pPlayer->Cosmetics()->m_ConfettiGun;
	pPlayer->SetConfettiGun(Set);
	log_info("cosmetics", "Set confetti gun to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetUfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr || g_Config.m_SvAutoUfo)
		return;

	bool Set = !pChr->m_Ufo.Active();
	pChr->SetUfo(Set);
	log_info("cosmetics", "Set ufo to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetPlayerName(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->Server()->OverrideClientName(Victim, pResult->GetString(1));
	log_info("server", "changed player '%s's changed name to '%s'", pSelf->Server()->ClientName(Victim), pResult->GetString(1));
}

void CGameContext::ConSetPlayerClan(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pSelf->Server()->SetClientClan(Victim, pResult->GetString(1));
	log_info("server", "changed player '%s's changed clan to '%s'", pSelf->Server()->ClientName(Victim), pResult->GetString(1));
}

void CGameContext::ConSetPlayerSkin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	str_copy(pPlayer->m_TeeInfos.m_aSkinName, pResult->GetString(1));
	log_info("server", "changed player '%s's changed skin to '%s'", pSelf->Server()->ClientName(Victim), pResult->GetString(1));
}

void CGameContext::ConSetPlayerCustomColor(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_TeeInfos.m_UseCustomColor = pResult->GetInteger(1);
	log_info("server", "changed player '%s's changed custom color to '%d'", pSelf->Server()->ClientName(Victim), pResult->GetInteger(1));
}

void CGameContext::ConSetPlayerColorBody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_TeeInfos.m_ColorBody = pResult->GetInteger(1);
	log_info("server", "changed player '%s's changed body color to '%d'", pSelf->Server()->ClientName(Victim), pResult->GetInteger(1));
}

void CGameContext::ConSetPlayerColorFeet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_TeeInfos.m_ColorFeet = pResult->GetInteger(1);
	log_info("server", "changed player '%s's changed feet color to '%d'", pSelf->Server()->ClientName(Victim), pResult->GetInteger(1));
}

void CGameContext::ConSetPlayerAfk(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Afk = !pPlayer->IsAfk();
	if(pResult->NumArguments() > 1)
		Afk = pResult->GetInteger(1);

	pPlayer->SetInitialAfk(Afk);
	log_info("server", "changed player '%s's afk status to '%d'", pSelf->Server()->ClientName(Victim), Afk);
}

void CGameContext::ConSetExtraPing(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;
	int Amount = pResult->GetInteger(1);

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_ExtraPing = Amount;
	log_info("server", "changed player '%s's extra ping to '%d'", pSelf->Server()->ClientName(Victim), Amount);
}

void CGameContext::ConSetAbility(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	const int Ability = pResult->GetInteger(0);
	pPlayer->SetAbility(Ability);
	log_info("server", "Set ability to %d for player '%s'", Ability, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConIgnoreGameLayer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->SetIgnoreGameLayer(!pPlayer->m_IgnoreGamelayer);
	log_info("server", "Set ignore game layer to %d for player %s", pPlayer->m_IgnoreGamelayer, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetVanish(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_Vanish = !pPlayer->m_Vanish;

	char aBuf[128];

	if(!pPlayer->m_Vanish)
	{
		char PlayerInfo[24] = " (No Client Info)";

		int ClientVersion = pSelf->Server()->GetClientVersion(Victim);
		if(ClientVersion >= 0)
			str_format(PlayerInfo, sizeof(PlayerInfo), "(%s %d)", pSelf->Server()->GetCustomClient(Victim), ClientVersion);
		else
			str_format(PlayerInfo, sizeof(PlayerInfo), "(%s)", pSelf->Server()->GetCustomClient(Victim));

		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s %s", pSelf->Server()->ClientName(Victim), pSelf->m_pController->GetTeamName(pPlayer->GetTeam()), PlayerInfo);
	}
	else
		str_format(aBuf, sizeof(aBuf), "'%s' has left the game", pSelf->Server()->ClientName(Victim));

	pSelf->SendChat(-1, TEAM_ALL, aBuf, -1, CGameContext::FLAG_SIX);
}

void CGameContext::ConSetVanishQuiet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_Vanish = !pPlayer->m_Vanish;
}

void CGameContext::ConIncludeInServerInfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	int Include = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : -1;

	if(Include == -1)
		pPlayer->m_IncludeServerInfo = !pPlayer->m_IncludeServerInfo;
	else
		pPlayer->m_IncludeServerInfo = Include;
	log_info("server", "Set include in server info to %d for player '%s'", pPlayer->m_IncludeServerInfo, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConRedirectClient(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;
	if(!CheckClientId(Victim))
		return;
	if(pSelf->Server()->ClientSlotEmpty(Victim))
		return;

	pSelf->Server()->RedirectClient(Victim, pResult->GetInteger(1));
}

void CGameContext::ConSetPassive(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetPassive(!pChr->Core()->m_Passive);
}

void CGameContext::ConSetHittable(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetHittable(!pChr->Core()->m_Hittable);
}

void CGameContext::ConSetHookable(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetHookable(!pChr->Core()->m_Hookable);
}

void CGameContext::ConSetCollidable(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetCollidable(!pChr->Core()->m_Collidable);
}

void CGameContext::ConSetTuneOverride(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetTuneOverride(pResult->GetInteger(0));
}

void CGameContext::ConTelekinesisImmunity(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->SetTelekinesisImmunity(!pPlayer->m_TelekinesisImmunity);
	log_info("server", "Set telekinesis immunity to %d for player %s", pPlayer->m_TelekinesisImmunity, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConTelekinesis(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	const int Weapon = WEAPON_TELEKINESIS;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConHeartGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	const int Weapon = WEAPON_HEARTGUN;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConLightsaber(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;
	const int Weapon = WEAPON_LIGHTSABER;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConPortalGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	const int Weapon = WEAPON_PORTALGUN;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConSetObfuscated(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->SetObfuscated(!pPlayer->m_Obfuscated);
	log_info("server", "Set obfuscated to %d for '%s' (%d)", pPlayer->m_Obfuscated, pSelf->Server()->ClientName(ClientId), ClientId);
}

void CGameContext::ConSetSpiderHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->m_SpiderHook = !pPlayer->m_SpiderHook;
	log_info("server", "Set spider hook to %d for '%s' (%d)", pPlayer->m_SpiderHook, pSelf->Server()->ClientName(ClientId), ClientId);
}

void CGameContext::ConSetSpazzing(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->m_Spazzing = !pPlayer->m_Spazzing;
	log_info("server", "Set spazzing to %d for '%s' (%d)", pPlayer->m_Spazzing, pSelf->Server()->ClientName(ClientId), ClientId);
}

void CGameContext::ConToggleMapVoteLock(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_MapVoteLock = !pSelf->m_MapVoteLock;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(pSelf->m_apPlayers[ClientId])
		{
			const int VoteMenuPage = pSelf->m_VoteMenu.GetPage(ClientId);
			if(VoteMenuPage == PAGE_VOTES || !g_Config.m_SvCustomVoteMenu)
				pSelf->ClearVotes(ClientId);
		}
	}
}

void CGameContext::ConInsertMapEntry(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pMap = pResult->GetString(0);
	const char *pServer = pResult->GetString(1);
	const char *pMapper = pResult->GetString(2);
	int Points = pResult->GetInteger(3);
	int Stars = pResult->GetInteger(4);
	char aTimestamp[32];
	if(pResult->NumArguments() > 5)
		str_copy(aTimestamp, pResult->GetString(5), sizeof(aTimestamp));
	else
	{
		time_t Now = time(0);
		str_timestamp_ex(Now, aTimestamp, sizeof(aTimestamp), "%Y-%m-%d");
	}

	if(!pMap)
	{
		log_info("score", "ConInsertMapEntry: no map specified");
		return;
	}
	if(!pServer)
	{
		log_info("score", "ConInsertMapEntry: no server specified");
		return;
	}
	if(!pMapper)
	{
		log_info("score", "ConInsertMapEntry: no mapper(s) specified");
		return;
	}

	pSelf->Score()->InsertMapEntry(pMap, pServer, pMapper, Points, Stars, aTimestamp);
}

void CGameContext::ConRemoveMapEntry(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pMapName = pResult->GetString(0);

	if(!pMapName)
	{
		log_info("score", "ConRemoveMapEntry: no map specified");
		return;
	}

	pSelf->Score()->RemoveMapEntry(pMapName);
}

void CGameContext::ConInsertRecord(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	float Time = pResult->GetFloat(1);
	const char *pMap = pResult->NumArguments() > 2 ? pResult->GetString(2) : pSelf->Map()->BaseName();
	pSelf->Score()->InsertPlayerRecord(pResult->m_ClientId, pName, pMap, Time);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}

void CGameContext::ConRemoveRecord(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	const char *pMap = pResult->NumArguments() > 1 ? pResult->GetString(2) : pSelf->Map()->BaseName();
	pSelf->Score()->RemovePlayerRecords(pName, pMap);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}
void CGameContext::ConRemoveRecordWithTime(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	float Time = pResult->GetFloat(1);
	const char *pMap = pResult->NumArguments() > 2 ? pResult->GetString(2) : pSelf->Map()->BaseName();
	pSelf->Score()->RemovePlayerRecordWithTime(pName, pMap, Time);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}

void CGameContext::ConRemoveAllRecords(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	pSelf->Score()->RemoveAllPlayerRecords(pName);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}

void CGameContext::ConDropWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
		return;

	vec2 Dir = normalize(vec2(pChr->Input()->m_TargetX, pChr->Input()->m_TargetY));
	int Type = pChr->Core()->m_ActiveWeapon;

	pChr->DropWeapon(Type, pChr->Core()->m_Vel * 0.7f + Dir * vec2(5.0f, 6.0f));
}

void CGameContext::ConCleanDroppedPickups(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
		if(pPlayer)
			pPlayer->m_vPickupDrops.clear();
	}
	// clean all existing pickupdrops
	pSelf->m_World.RemoveEntities(CGameWorld::ENTTYPE_PICKUPDROP);
}

void CGameContext::ConNewPickupDrop(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
		return;

	vec2 Pos = pChr->m_Pos;
	vec2 Dir = vec2(0, 0);
	int TeleCheck = pChr->m_TeleCheckpoint;
	int Team = pChr->Team();
	int Type = pResult->GetInteger(0);

	int Lifetime = pSelf->Server()->TickSpeed() * 300; // 5 minutes

	new CPickupDrop(&pSelf->m_World, pPlayer->MultiMapIdx(), pResult->m_ClientId, Pos, Team, TeleCheck, Dir, Lifetime, Type); // NOLINT(clang-analyzer-unix.Malloc)
}

void CGameContext::ConRepredict(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!pResult->NumArguments())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You need to specify your client prediction margin");
		pSelf->SendChatTarget(pResult->m_ClientId, "10 is the default.");
		return;
	}

	int PredMargin = pResult->GetInteger(0);

	pPlayer->Repredict(PredMargin);
}

void CGameContext::ConPowerups(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	pPlayer->SetHidePowerUps(!pPlayer->Acc()->m_Configs.m_HidePowerUps);
	if(pPlayer->Acc()->m_Configs.m_HidePowerUps)
		pSelf->SendChatTarget(pResult->m_ClientId, "Powerups are now hidden");
	else
		pSelf->SendChatTarget(pResult->m_ClientId, "Powerups are now shown");
}

void CGameContext::ConCosmetics(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	pPlayer->Acc()->m_Configs.ToggleCosmetics();
	if(pPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowRainbow)
		pSelf->SendChatTarget(pResult->m_ClientId, "All Cosmetics are now shown");
	else
		pSelf->SendChatTarget(pResult->m_ClientId, "All Cosmetics are now hidden");
}

void CGameContext::ConSetBet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;

	if(!g_Config.m_SvAccounts)
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Acc()->m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You need to be logged in for this");
		return;
	}

	if(pPlayer->m_Area == EArea::Game)
	{
		pSelf->SendChatTarget(ClientId, "You need to be in an area where you can place a bet");
		return;
	}

	const int Amount = pResult->GetInteger(0);
	const int Money = pPlayer->Acc()->m_Money;
	if(Amount > Money)
	{
		pSelf->SendChatTarget(ClientId, "You don't have enough money to place that bet");
		return;
	}

	if(Amount <= 0)
		return;
	if(pPlayer->m_BetAmount == Amount)
		return;

	char aBuf[64];
	if(pPlayer->m_BetAmount <= 0)
		str_format(aBuf, sizeof(aBuf), "You wagered %d%s", Amount, g_Config.m_SvCurrencyName);
	else
		str_format(aBuf, sizeof(aBuf), "You changed your wager to %d%s", Amount, g_Config.m_SvCurrencyName);

	pSelf->SendChatTarget(ClientId, aBuf);

	pPlayer->m_BetAmount = Amount;
	pPlayer->m_LastBet = pSelf->Server()->Tick();
}

void CGameContext::ConReport(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!g_Config.m_DcReportsWebhookUrl[0] || !g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Reporting is not enabled on this server.");
		return;
	}

	const char *pFrom = pSelf->Server()->ClientName(ClientId);
	const char *pAgainst = pResult->GetString(0);
	const char *pReportText = pResult->GetString(1);

	if(!str_comp_nocase(pAgainst, pFrom))
	{
		pSelf->SendChatTarget(ClientId, "You can't report yourself.");
		return;
	}

	if(!pPlayer->Acc()->m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You need to be logged in for this.");
		return;
	}

	if(!pPlayer->CanReport())
	{
		pSelf->SendChatTarget(ClientId, "You need to wait a bit until you can report.");
		return;
	}

	bool Found = false;
	CPlayer *pAgainstPl = nullptr;
	for(int ClientId2 = 0; ClientId2 < MAX_CLIENTS; ClientId2++)
	{
		if(!pSelf->Server()->ClientIngame(ClientId2))
			continue;

		if(!str_comp(pSelf->Server()->ClientName(ClientId2), pAgainst))
		{
			Found = true;
			pAgainstPl = pSelf->m_apPlayers[ClientId2];
			break;
		}
	}
	if(!Found)
	{
		pSelf->SendChatTarget(ClientId, "Player not found.");
		return;
	}

	char aBuf[1024];
	str_format(aBuf, sizeof(aBuf), "From: `%s` (Acc: `%s`)\n"
				       "against: `%s` (Acc: `%s`)\n"
				       "\n"
				       "Reason: %s\n"
				       "\n"
				       "-----------------------------------",
		pFrom,
		pPlayer->Acc()->m_aUsername,
		pAgainst,
		pAgainstPl ? pAgainstPl->Acc()->m_aUsername : "No Account",
		pReportText);

	char aNameBuf[32] = "";
	str_format(aNameBuf, sizeof(aNameBuf), "Player Report (Port: %d)", pSelf->Server()->Port());

	pSelf->Server()->SendWebhookMessage(g_Config.m_DcReportsWebhookUrl, aBuf, aNameBuf);

	pPlayer->m_LastReport = pSelf->Server()->Tick();
	pSelf->SendChatTarget(ClientId, "Report sent!");
}

void CGameContext::ConLaserText(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;

	if(!CheckClientId(ClientId))
		return;

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pChr)
		return;

	const vec2 Pos = pChr->m_Pos + vec2(0, -100);

	const char *pText = pResult->NumArguments() ? pResult->GetString(0) : "noob";

	new CLaserText(&pSelf->m_World, pChr->MultiMapIdx(), ClientId, Pos, 250, pText); // NOLINT(clang-analyzer-unix.Malloc)
}

void CGameContext::ConProjectileText(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;

	if(!CheckClientId(ClientId))
		return;

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pChr)
		return;

	const vec2 Pos = pChr->m_Pos + vec2(0, -60);
	const char *pText = pResult->GetString(0);
	new CProjectileText(&pSelf->m_World, pChr->MultiMapIdx(), ClientId, Pos, 250, pText, WEAPON_HAMMER); // NOLINT(clang-analyzer-unix.Malloc)
}

void CGameContext::ConSendAsPlayer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();

	if(!CheckClientId(ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const char *pText = pResult->GetString(1);

	if(pText[0] == '/')
	{
		const char *pWhisper;
		if((pWhisper = str_startswith_nocase(pText + 1, "w ")))
		{
			pSelf->Whisper(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else if((pWhisper = str_startswith_nocase(pText + 1, "whisper ")))
		{
			pSelf->Whisper(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else if((pWhisper = str_startswith_nocase(pText + 1, "c ")))
		{
			pSelf->Converse(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else if((pWhisper = str_startswith_nocase(pText + 1, "converse ")))
		{
			pSelf->Converse(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else
		{
			if(g_Config.m_SvSpamprotection && !str_startswith(pText + 1, "timeout ") && pPlayer->m_aLastCommands[0] && pPlayer->m_aLastCommands[0] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick() && pPlayer->m_aLastCommands[1] && pPlayer->m_aLastCommands[1] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick() && pPlayer->m_aLastCommands[2] && pPlayer->m_aLastCommands[2] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick() && pPlayer->m_aLastCommands[3] && pPlayer->m_aLastCommands[3] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick())
				return;

			int64_t Now = pSelf->Server()->Tick();
			pPlayer->m_aLastCommands[pPlayer->m_LastCommandPos] = Now;
			pPlayer->m_LastCommandPos = (pPlayer->m_LastCommandPos + 1) % 4;

			for(CServerComponent *pComponent : pSelf->m_vpComponents)
			{
				if(!pComponent->CanUseCommand(pPlayer, pText + 1))
					return;
			}

			pSelf->Console()->SetFlagMask(CFGFLAG_CHAT);
			pSelf->Console()->ExecuteLine(pText + 1, ClientId, false);

			// m_apPlayers[ClientId] can be NULL, if the player used a
			// timeout code and replaced another client.
			// log_info("chat-command", "%d used %s", ClientId, pText);

			pSelf->Console()->SetFlagMask(CFGFLAG_SERVER);
		}
	}
	else
	{
		// pPlayer->UpdatePlaytime();
		char aCensoredMessage[256];
		pSelf->CensorMessage(aCensoredMessage, pText, sizeof(aCensoredMessage));
		pSelf->SendChat(ClientId, TEAM_ALL, aCensoredMessage, ClientId);
	}
}

void CGameContext::ConPlaySoundGlobal(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Sound = pResult->NumArguments() ? pResult->GetInteger(0) : -1;

	if(Sound < 0 || Sound >= NUM_SOUNDS)
	{
		log_info("foxnet", "0  = Gun Fire");
		log_info("foxnet", "1  = Shot Gun Fire");
		log_info("foxnet", "2  = Grenade Fire");
		log_info("foxnet", "3  = Hammer Fire");
		log_info("foxnet", "4  = Hammer Hit");
		log_info("foxnet", "5  = Ninja Fire");
		log_info("foxnet", "6  = Grenade Explosion");
		log_info("foxnet", "7  = Ninja Hit");
		log_info("foxnet", "8  = Laser Fire");
		log_info("foxnet", "9  = Laser Bounce");
		log_info("foxnet", "10 = Weapon Switch");
		log_info("foxnet", "11 = Player Pain Short");
		log_info("foxnet", "12 = Player Pain Long");
		log_info("foxnet", "13 = Body Land");
		log_info("foxnet", "14 = Player Airjump");
		log_info("foxnet", "15 = Player Jump");
		log_info("foxnet", "16 = Player Die");
		log_info("foxnet", "17 = Player Spawn");
		log_info("foxnet", "18 = Player Skid");
		log_info("foxnet", "19 = Tee Cry");
		log_info("foxnet", "20 = Hook Loop");
		log_info("foxnet", "21 = Hook Attach Ground");
		log_info("foxnet", "22 = Hook Attach Player");
		log_info("foxnet", "23 = Hook Sound No-Attach");
		log_info("foxnet", "24 = Pickup Health");
		log_info("foxnet", "25 = Pickup Armor");
		log_info("foxnet", "26 = Pickup Grenade");
		log_info("foxnet", "27 = Pickup Shotgun");
		log_info("foxnet", "28 = Pickup Ninja");
		log_info("foxnet", "29 = Weapon Spawn");
		log_info("foxnet", "30 = Weapon No-Ammo");
		log_info("foxnet", "31 = Hit");
		log_info("foxnet", "32 = Chat Server");
		log_info("foxnet", "33 = Chat Client");
		log_info("foxnet", "34 = Chat Highlight");
		log_info("foxnet", "35 = CTF Drop");
		log_info("foxnet", "36 = CTF Return");
		log_info("foxnet", "37 = CTF grab PL");
		log_info("foxnet", "37 = CTF grab PL");
		log_info("foxnet", "38 = CTF Grab EN");
		log_info("foxnet", "39 = CTF Flag Capture");
		log_info("foxnet", "40 = Menu");
		return;
	}

	pSelf->CreateSoundGlobal(Sound);
}

void CGameContext::ConLoadMultiMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments() != 2)
	{
		log_info("foxnet", "Usage: load_map <type> <map-name>");
		log_info("foxnet", "Types:");
		log_info("foxnet", "0 = Any");
		log_info("foxnet", "1 = Casino");
		return;
	}
	const char *pMapName = pResult->GetString(1);
	EMapType Type = (EMapType)pResult->GetInteger(0);
	if(!pMapName[0])
		return;

	pSelf->LoadMapByName(pMapName, Type);
}

void CGameContext::ConUnloadMultiMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aMapName[IO_MAX_PATH_LENGTH];
	if(pResult->NumArguments())
	{
		str_copy(aMapName, pResult->GetString(0), sizeof(aMapName));
	}
	else
	{
		int UserId = pResult->m_ClientId;
		if(CheckClientId(UserId) && !pSelf->Server()->ClientSlotEmpty(UserId))
		{
			int MultiMapIndex = pSelf->GetMultiMapIdx(UserId);
			if(MultiMapIndex > DefaultMapIndex)
				str_copy(aMapName, pSelf->m_vMultiMaps[MultiMapIndex]->m_pMap->BaseName(), sizeof(aMapName));
		}
	}

	pSelf->UnloadMapByName(aMapName);
}

void CGameContext::ConReloadMultiMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aMapName[IO_MAX_PATH_LENGTH];
	if(pResult->NumArguments())
	{
		str_copy(aMapName, pResult->GetString(0), sizeof(aMapName));
	}
	else
	{
		int UserId = pResult->m_ClientId;
		if(CheckClientId(UserId) && !pSelf->Server()->ClientSlotEmpty(UserId))
		{
			int MultiMapIndex = pSelf->GetMultiMapIdx(UserId);
			if(MultiMapIndex > DefaultMapIndex)
			{
				str_copy(aMapName, pSelf->m_vMultiMaps[MultiMapIndex]->m_pMap->BaseName(), sizeof(aMapName));
			}
		}

		pSelf->ReloadMapByName(aMapName);
	}
}

void CGameContext::ConListMultiMaps(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Idx = 0;
	for(const auto &MultiMap : pSelf->m_vMultiMaps)
	{
		const char *pMapName = MultiMap->m_pMap->BaseName();
		log_info("multimap", "%d: %s (Type: %d)", Idx, pMapName, (int)MultiMap->m_MapType);
		Idx++;
	}
}

void CGameContext::ConSendToMultiMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;
	const char *pMapName = pResult->GetString(1);
	if(!pMapName[0])
		return;

	int Idx = pSelf->GetMapIndexByMapName(pMapName);

	if(Idx < 0 || (int)pSelf->m_vMultiMaps.size() < Idx)
	{
		log_error("multimap", "Map '%s' isn't loaded", pMapName);
		return;
	}

	if(!CheckClientId(Victim))
		return;
	if(pSelf->Server()->ClientSlotEmpty(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	pPlayer->SendToMap(Idx);
}

void CGameContext::ConCasino(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int UserId = pResult->m_ClientId;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : UserId;
	if(UserId >= 0 || UserId < MAX_CLIENTS)
	{
		if(!pSelf->CanUseCmd(UserId, pResult->GetCommand()))
		{
			Victim = UserId;
		}
	}

	int Idx = pSelf->GetMapIndexByType(EMapType::Casino);

	if(Idx < 0 || (int)pSelf->m_vMultiMaps.size() < Idx)
	{
		log_error("multimap", "Casino map isn't loaded");
		return;
	}

	if(!CheckClientId(Victim))
		return;
	if(pSelf->Server()->ClientSlotEmpty(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	pPlayer->SendToMap(Idx);
}

void CGameContext::ConMainMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int UserId = pResult->m_ClientId;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : UserId;
	if(UserId >= 0 || UserId < MAX_CLIENTS)
	{
		if(!pSelf->CanUseCmd(UserId, pResult->GetCommand()))
		{
			Victim = UserId;
		}
	}
	const int MainMapIdx = DefaultMapIndex; // might change in the future

	if(!CheckClientId(Victim))
		return;
	if(pSelf->Server()->ClientSlotEmpty(Victim))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	pPlayer->SendToMap(MainMapIdx);
}

void CGameContext::RegisterFoxNetCommands()
{
	// MultiMaps
	Console()->Register("load_map", "?i[type] ?r[map-name]", CFGFLAG_SERVER, ConLoadMultiMap, this, "Load a map of type (leave empty for help text)");
	Console()->Register("unload_map", "?r[map-name]", CFGFLAG_SERVER, ConUnloadMultiMap, this, "Unload a map by name");
	Console()->Register("reload_map", "?r[map-name]", CFGFLAG_SERVER, ConReloadMultiMap, this, "Reload a map by name");
	Console()->Register("list_maps", "", CFGFLAG_SERVER, ConListMultiMaps, this, "Reload a map by name");

	Console()->Register("send_to_main_map", "?v[id]", CFGFLAG_SERVER, ConMainMap, this, "Send player to the main map");
	Console()->Register("send_to_map", "v[id] r[map-name]", CFGFLAG_SERVER, ConSendToMultiMap, this, "Send player to a different loaded map");

	Console()->Register("playsound", "?i[sound_id]", CFGFLAG_SERVER, ConPlaySoundGlobal, this, "Play a sound globally for everyone");

	Console()->Register("send_as", "v[id] r[message]", CFGFLAG_SERVER, ConSendAsPlayer, this, "Send a chat message as player (id)");

	Console()->Register("lasertext", "r[string]", CFGFLAG_SERVER, ConLaserText, this, "laser text");
	Console()->Register("projectiletext", "r[string]", CFGFLAG_SERVER, ConProjectileText, this, "projectile text");

	Console()->Register("chat_string_add", "s[string] s[reason] i[should Ban] i[bantime] ?f[addition]", CFGFLAG_SERVER, ConAddChatDetectionString, this, "Add a string to the chat detection list");
	Console()->Register("chat_string_remove", "r[name]", CFGFLAG_SERVER, ConRemoveChatDetectionString, this, "Remove a string from the chat detection list");
	Console()->Register("chat_strings_list", "", CFGFLAG_SERVER, ConListChatDetectionStrings, this, "List all strings on the list");
	Console()->Register("chat_string_clear", "", CFGFLAG_SERVER, ConClearChatDetectionStrings, this, "Clear all strings on the list");

	Console()->Register("name_string_add", "s[name] s[reason] i[bantime] ?i[exact name]", CFGFLAG_SERVER, ConAddNameDetectionString, this, "Add a string to the name detection list");
	Console()->Register("name_string_remove", "r[name]", CFGFLAG_SERVER, ConRemoveNameDetectionString, this, "Remove a string from the name detection list");
	Console()->Register("name_strings_list", "", CFGFLAG_SERVER, ConListNameDetectionStrings, this, "List all strings on the list");
	Console()->Register("name_string_clear", "", CFGFLAG_SERVER, ConClearNameDetectionStrings, this, "Clear all strings on the list");

	Console()->Register("snake", "?v[id]", CFGFLAG_SERVER, ConSnake, this, "Makes a player (id) a Snake");
	Console()->Register("ufo", "?v[id]", CFGFLAG_SERVER, ConSetUfo, this, "Puts player (id) into an UFO");

	Console()->Register("set_name", "v[id] r[name]", CFGFLAG_SERVER, ConSetPlayerName, this, "Set a players (id) Name");
	Console()->Register("set_clan", "v[id] r[clan]", CFGFLAG_SERVER, ConSetPlayerClan, this, "Set a players (id) Clan");
	Console()->Register("set_skin", "v[id] r[skin]", CFGFLAG_SERVER, ConSetPlayerSkin, this, "Set a players (id) Skin");
	Console()->Register("set_custom_color", "v[id] i[int]", CFGFLAG_SERVER, ConSetPlayerCustomColor, this, "Whether a player (id) uses custom color (1 = true | 0 = false)");
	Console()->Register("set_color_body", "v[id] i[color]", CFGFLAG_SERVER, ConSetPlayerColorBody, this, "Set a players (id) Body Color");
	Console()->Register("set_color_feet", "v[id] i[color]", CFGFLAG_SERVER, ConSetPlayerColorFeet, this, "Set a players (id) Feet Color");
	Console()->Register("set_afk", "v[id] ?i[afk]", CFGFLAG_SERVER, ConSetPlayerAfk, this, "Set a players (id) afk status");
	Console()->Register("set_extra_ping", "v[id] i[amount]", CFGFLAG_SERVER, ConSetExtraPing, this, "Set a players (id) extra ping");

	Console()->Register("set_ability", "i[ability] ?v[id]", CFGFLAG_SERVER, ConSetAbility, this, "Set a players (id) Ability");

	Console()->Register("ignore_gamelayer", "?v[id]", CFGFLAG_SERVER, ConIgnoreGameLayer, this, "Turns off the kill-border for (id)");
	Console()->Register("invisible", "?v[id]", CFGFLAG_SERVER, ConInvisible, this, "Makes a players (id) Invisible");
	Console()->Register("vanish", "?v[id]", CFGFLAG_SERVER, ConSetVanish, this, "Completely hide player (id) from everyone on the server");
	Console()->Register("vanish_quiet", "?v[id]", CFGFLAG_SERVER, ConSetVanishQuiet, this, "Completely hide player (id) from everyone on the server without the chat join/leave message");
	Console()->Register("include_serverinfo", "?v[id] ?i[include]", CFGFLAG_SERVER, ConIncludeInServerInfo, this, "whether a player should be in the serverinfo (true by default for everyone)");
	Console()->Register("redirect", "v[id] i[port]", CFGFLAG_SERVER, ConRedirectClient, this, "Redirect player (id) to a different Server (port)");

	Console()->Register("passive", "?v[id]", CFGFLAG_SERVER, ConSetPassive, this, "Put player (id) into passive");
	Console()->Register("hittable", "?v[id]", CFGFLAG_SERVER, ConSetHittable, this, "whether player (id) can be hit by other players");
	Console()->Register("hookable", "?v[id]", CFGFLAG_SERVER, ConSetHookable, this, "whether player (id) can be hooked by other players");
	Console()->Register("collidable", "?v[id]", CFGFLAG_SERVER, ConSetCollidable, this, "whether player (id) can collide with others");

	Console()->Register("set_tune_override", "i[zone] ?v[id]", CFGFLAG_SERVER, ConSetTuneOverride, this, "Sets the tune override for the player (id)");

	Console()->Register("telekinesis_immunity", "?v[id]", CFGFLAG_SERVER, ConTelekinesisImmunity, this, "Makes player (id) immunte to telekinesis");

	Console()->Register("telekinesis", "?v[id]", CFGFLAG_SERVER, ConTelekinesis, this, "Gives/Takes telekinses to/from player (id)");
	Console()->Register("heartgun", "?v[id]", CFGFLAG_SERVER, ConHeartGun, this, "Gives/Takes a heartgun to/from player (id)");
	Console()->Register("lightsaber", "?v[id]", CFGFLAG_SERVER, ConLightsaber, this, "Gives/Takes a lightsaber to/from player (id)");
	Console()->Register("portalgun", "?v[id]", CFGFLAG_SERVER, ConPortalGun, this, "Gives/Takes the portal gun to/from player (id)");

	Console()->Register("obfuscate", "?v[id]", CFGFLAG_SERVER, ConSetObfuscated, this, "Makes players (id) name obfuscated");
	Console()->Register("spider_hook", "?v[id]", CFGFLAG_SERVER, ConSetSpiderHook, this, "whether player (id) has spider hook");
	Console()->Register("spazzing", "?v[id]", CFGFLAG_SERVER, ConSetSpazzing, this, "Makes players (id) spazzing");

	Console()->Register("map_vote_lock", "", CFGFLAG_SERVER, ConToggleMapVoteLock, this, "Toggle Map Vote Locking");

	// Cosmetics
	Console()->Register("c_lovely", "?v[id]", CFGFLAG_SERVER, ConLovely, this, "Makes a player (id) Lovely");
	Console()->Register("c_epic_circle", "?v[id]", CFGFLAG_SERVER, ConEpicCircle, this, "Gives a player (id) an Epic Circle");
	Console()->Register("c_rotating_ball", "?v[id]", CFGFLAG_SERVER, ConRotatingBall, this, "Gives a player (id) a Rotating Ball");
	Console()->Register("c_bloody", "?v[id]", CFGFLAG_SERVER, ConBloody, this, "Gives a player (id) the Bloody Effect");
	Console()->Register("c_strongbloody", "?v[id]", CFGFLAG_SERVER, ConStrongBloody, this, "Gives a player (id) the Strong Bloody Effect");
	Console()->Register("c_sparkle", "?v[id]", CFGFLAG_SERVER, ConSparkle, this, "Gives a player (id) the Sparkle");
	Console()->Register("c_inverse_aim", "?v[id]", CFGFLAG_SERVER, ConInverseAim, this, "Makes a players (id) aim be inversed");
	Console()->Register("c_heart_hat", "?v[id]", CFGFLAG_SERVER, ConHeartHat, this, "Gives a player (id) a heart hat");
	Console()->Register("c_hookpower", "?i[power] ?v[id]", CFGFLAG_SERVER, ConHookPower, this, "Sets hook power for player (id)");

	Console()->Register("c_staff_ind", "?v[id]", CFGFLAG_SERVER, ConStaffInd, this, "Gives a player (id) a Staff Indicator");
	Console()->Register("c_pickuppet", "?v[id]", CFGFLAG_SERVER, ConSetPickupPet, this, "Gives player (id) a pet");
	Console()->Register("c_lissajous", "?v[id]", CFGFLAG_SERVER, ConLissajous, this, "Gives player (id) a lissajous figure");
	Console()->Register("c_halo", "?v[id]", CFGFLAG_SERVER, ConHalo, this, "Gives player (id) a halo");

	Console()->Register("c_star_trail", "?v[id]", CFGFLAG_SERVER, ConStarTrail, this, "Gives a player (id) a Star Trail");
	Console()->Register("c_dot_trail", "?v[id]", CFGFLAG_SERVER, ConDotTrail, this, "Gives a player (id) a Dot Trail");

	Console()->Register("c_rainbow_body", "?v[id]", CFGFLAG_SERVER, ConRainbowBody, this, "Makes a players (id) Body Rainbow");
	Console()->Register("c_rainbow_feet", "?v[id]", CFGFLAG_SERVER, ConRainbowFeet, this, "Makes a players (id) Feet Rainbow");
	Console()->Register("c_rainbow_speed", "?v[id] ?i[speed]", CFGFLAG_SERVER, ConRainbowSpeed, this, "Makes a players (id) Rainbow");

	Console()->Register("c_phase_gun", "?v[id]", CFGFLAG_SERVER, ConPhaseGun, this, "Gives player (id) a gun that shoots trough walls");
	Console()->Register("c_emote_gun", "i[type] ?v[id]", CFGFLAG_SERVER, ConSetEmoticonGun, this, "Set a players (id) Emoticon Gun to i[type] (1-12)");
	Console()->Register("c_confetti_gun", "?v[id]", CFGFLAG_SERVER, ConSetConfettiGun, this, "Set a players (id) Gun to shoot confetti");

	Console()->Register("c_death_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConDeathEffect, this, "Set players (id) Death Type");
	Console()->Register("c_damageind_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConDamageIndType, this, "Set players (id) Damage Ind Type");
	Console()->Register("c_gun_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConGunType, this, "Set players (id) Gun Type");
	Console()->Register("c_hat_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConHatType, this, "Set players (id) Hat Type");

	// Player configs
	// Console()->Register("hide_cosmetics", "?v[id]", CFGFLAG_SERVER, ConHideCosmetics, this, "Hides Cosmetics for Player (id)");
	// Console()->Register("hide_powerups", "?v[id]", CFGFLAG_SERVER, ConHidePowerUps, this, "Hides Powerups for Player (id)");

	// Records
	Console()->Register("map_entry_insert", "s[mapname] s[server] s[mapper] i[points] i[stars] ?r[timestamp]", CFGFLAG_SERVER, ConInsertMapEntry, this, "Insert a new map entry into the ddnet_maps sql table");
	Console()->Register("map_entry_remove", "s[mapname]", CFGFLAG_SERVER, ConRemoveMapEntry, this, "Remove a map entry from the ddnet_maps sql table");

	Console()->Register("record_insert", "s[name] f[time] ?r[mapname]", CFGFLAG_SERVER, ConInsertRecord, this, "Insert a new record for that name on the given map with given time");
	Console()->Register("record_remove", "s[name] ?r[mapname]", CFGFLAG_SERVER, ConRemoveRecord, this, "Remove all records a name has on the given map");
	Console()->Register("record_remove_time", "s[name] f[time] ?r[mapname]", CFGFLAG_SERVER, ConRemoveRecordWithTime, this, "Remove records a name has on given map with given time");
	Console()->Register("record_remove_all", "r[name]", CFGFLAG_SERVER, ConRemoveAllRecords, this, "Remove all records a name has");

	// Account
	Console()->Register("give_money", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveMoney, this, "Give player (id) money");
	Console()->Register("give_xp", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveXp, this, "Give player (id) xp");

	Console()->Register("pay", "s[player] i[amount]", CFGFLAG_CHAT, ConPayMoney, this, "Pay someone money");
	Console()->Register("report", "s[player] r[message]", CFGFLAG_CHAT, ConReport, this, "Report a player");

	// Casino/Map related
	Console()->Register("bet", "i[amount]", CFGFLAG_CHAT, ConSetBet, this, "place a bet on the roulette");
	Console()->Register("casino", "?v[id]", CFGFLAG_CHAT | CMDFLAG_CONDITIONAL, ConCasino, this, "Send players (id) to the casino map (if loaded)");
	Console()->Register("leave", "?v[id]", CFGFLAG_CHAT | CMDFLAG_CONDITIONAL, ConMainMap, this, "leave to the main map");
	Console()->Register("exit", "?v[id]", CFGFLAG_CHAT | CMDFLAG_CONDITIONAL, ConMainMap, this, "leave to the main map");

	// Shop
	Console()->Register("toggleitem", "s[item] ?i[value]", CFGFLAG_CHAT, ConToggleItem, this, "Toggle an Item, value is only needed for 2 items");
	Console()->Register("dropweapon", "", CFGFLAG_CHAT, ConDropWeapon, this, "Drops the weapon you're currently holding");

	Console()->Register("cleanup_pickupdrops", "", CFGFLAG_SERVER, ConCleanDroppedPickups, this, "Removes all dropped pickups");
	Console()->Register("new_pickupdrop", "i[type]", CFGFLAG_SERVER, ConNewPickupDrop, this, "Spawns a new pickup drop on your position");

	Console()->Register("repredict", "?i[predmargin]", CFGFLAG_CHAT, ConRepredict, this, "Recalculates the Server-Side prediction (based on Ping + pred margin)");

	Console()->Register("powerups", "", CFGFLAG_CHAT, ConPowerups, this, "Hide/show powerups");
	Console()->Register("cosmetics", "", CFGFLAG_CHAT, ConCosmetics, this, "Hide/show all cosmetics");

	Console()->Chain("sv_solo_on_spawn", ConchainSoloOnSpawn, this);
	Console()->Chain("sv_cosmetics", ConchainCosmetics, this);
	Console()->Chain("sv_accounts", ConchainAccounts, this);
	Console()->Chain("sv_custom_vote_menu", ConchainResendVoteMenu, this);
	Console()->Chain("sv_accounts_forced", ConchainAccountsForced, this);

	Console()->Chain("unban", ConchainScriptingBan, this);
	Console()->Chain("ban", ConchainScriptingBan, this);
	Console()->Chain("banid", ConchainScriptingBan, this);
	Console()->Chain("ban_timestamp", ConchainScriptingBan, this);
	Console()->Chain("ban_range", ConchainScriptingBan, this);
	Console()->Chain("unban_range", ConchainScriptingBan, this);

	Console()->Chain("sv_multimap", ConchainMultimap, this);
}
void CGameContext::ConchainMultimap(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CGameContext *pSelf = (CGameContext *)pUserData;
	int NumArgs = pResult->NumArguments();
	if(NumArgs)
		return;
	pSelf->UnloadMapsAll();
}

void CGameContext::ConchainScriptingBan(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CGameContext *pSelf = (CGameContext *)pUserData;
	int NumArgs = pResult->NumArguments();
	if(!NumArgs)
		return;
	int UserId = pResult->m_ClientId;
	if(!CheckClientId(UserId) && UserId != IConsole::CLIENT_ID_FOXNET)
		return;

	char aCmdBuf[1028];
	char aArgBuf[256] = "";
	for(int i = 0; i < NumArgs; i++)
	{
		char aTempBuf[128];
		str_copy(aTempBuf, pResult->GetString(i), sizeof(aTempBuf));
		if(i == 0 && pResult->GetVictim() >= 0 && pResult->GetVictimAddrStr())
		{
			char aIdBuf[32];
			str_format(aIdBuf, sizeof(aIdBuf), "%s", pResult->GetVictimAddrStr());
			str_copy(aTempBuf, aIdBuf, sizeof(aTempBuf));

			if(pResult->GetVictim() == UserId)
				return; // prevent self ban
		}

		if(i > 0)
			str_append(aArgBuf, " ", sizeof(aArgBuf));
		str_append(aArgBuf, aTempBuf, sizeof(aArgBuf));
	}

	char aCmdName[64];
	str_copy(aCmdName, pResult->GetCommand(), sizeof(aCmdName));
	if(!str_comp(aCmdName, "banid"))
		str_copy(aCmdName, "ban", sizeof(aCmdName));

	str_format(aCmdBuf, sizeof(aCmdBuf), "%s %s", aCmdName, aArgBuf);
	pSelf->FormatAndRunScriptingBan(aCmdBuf, UserId);
}

void CGameContext::FormatAndRunScriptingBan(const char *pStr, int UserId)
{
	if(!CheckClientId(UserId) && UserId != IConsole::CLIENT_ID_FOXNET)
		return;

	if(!g_Config.m_SvScriptPlayerBans[0])
		return;

	// (un)ban ip minutes reason
	char aScriptingArgs[256] = "";
	NETADDR Addr;
	char aAddrStr[NETADDR_MAXSTRSIZE] = "";
	const char *pArg = GetParsedArgument(pStr, 1, false);
	if(!pArg)
		return;

	if(str_startswith_nocase(pStr, "ban_range "))
	{
		str_copy(aScriptingArgs, pStr, sizeof(aScriptingArgs));
	}
	else if(str_startswith_nocase(pStr, "unban_range "))
	{
		str_copy(aScriptingArgs, pStr, sizeof(aScriptingArgs));
	}
	else if(str_startswith_nocase(pStr, "unban "))
	{
		if(str_isallnum(pArg))
		{
			int Index = str_toint(pArg);
			const NETADDR *pTempAddr = Server()->GetAddrFromBanIndex(Index);
			if(pTempAddr)
				net_addr_str(pTempAddr, aAddrStr, sizeof(aAddrStr), false);
		}
		else if(!net_addr_from_str(&Addr, pArg))
			str_copy(aAddrStr, pArg, sizeof(aAddrStr));

		if(aAddrStr[0])
		{
			str_format(aScriptingArgs, sizeof(aScriptingArgs), "unban %s", aAddrStr);
		}
	}
	else if(str_startswith_nocase(pStr, "ban ") || str_startswith_nocase(pStr, "ban_timestamp "))
	{
		if(str_isallnum(pArg))
		{
			int ClientId = str_toint(pArg);
			if(ClientId == UserId)
				return; // prevent self ban

			const NETADDR *pTempAddr = Server()->GetAddrFromBanIndex(-1);
			if(pTempAddr)
				net_addr_str(pTempAddr, aAddrStr, sizeof(aAddrStr), false);
		}
		else if(!net_addr_from_str(&Addr, pArg))
			str_copy(aAddrStr, pArg, sizeof(aAddrStr));

		if(UserId >= 0)
		{
			const char *pUserAddrStr = Server()->ClientAddrString(UserId, false);
			if(!str_comp(aAddrStr, pUserAddrStr))
				return; // prevent banning the user themselves
		}

		if(aAddrStr[0])
		{
			const char *pMinutesStr = GetParsedArgument(pStr, 2, false);
			long Minutes = (pMinutesStr && str_isallnum(pMinutesStr)) ? (long)str_toint(pMinutesStr) : 5;
			const char *pReason = GetParsedArgument(pStr, 3, true);
			if(!pReason)
				pReason = "No Reason Provided.";
			str_format(aScriptingArgs, sizeof(aScriptingArgs), "ban %s %ld %s", aAddrStr, Minutes, pReason);
		}
	}

	if(!aScriptingArgs[0])
		return;

	char aScriptingBuf[256];
	str_format(aScriptingBuf, sizeof(aScriptingBuf), "chai %s %s", g_Config.m_SvScriptPlayerBans, aScriptingArgs);
	Console()->ExecuteLine(aScriptingBuf, IConsole::CLIENT_ID_UNSPECIFIED);
}

void CGameContext::ConchainSoloOnSpawn(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments() || !pResult->GetInteger(0))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
		if(pChr && pChr->m_SpawnSolo)
			pChr->UnSpawnSolo();
	}
}

void CGameContext::ConchainCosmetics(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool Value = pResult->GetInteger(0) != 0;
	if(!Value)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
			if(pPlayer)
			{
				pPlayer->DisableAllCosmetics();
			}
		}
	}
}

void CGameContext::ConchainAccounts(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool Value = pResult->GetInteger(0) != 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
		if(pPlayer)
		{
			if(!Value)
			{
				pPlayer->SetPage(PAGE_MAIN);
				if(pPlayer->Acc()->m_LoggedIn && pSelf->m_AccountManager.Logout(ClientId))
					pSelf->SendChatTarget(ClientId, "You have been logged out because accounts have been disabled on this server.");

				pSelf->m_AccountManager.LogoutAllAccountsPort(pSelf->Server()->Port());
			}
			else
				pSelf->m_AccountManager.AutoLogin(ClientId); // try to login all clients
		}
	}
}

void CGameContext::ConchainResendVoteMenu(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ClearVotes(-1);
}

void CGameContext::ConchainAccountsForced(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;
	if(!g_Config.m_SvAccounts)
	{
		log_info("accounts", "Cannot force accounts when accounts are disabled. Disabling forced accounts.");
		g_Config.m_SvAccountsForced = 0;
		return;
	}

	CGameContext *pSelf = (CGameContext *)pUserData;

	int Value = pResult->GetInteger(0);

	if(Value)
	{
		for(CPlayer *pPl : pSelf->m_apPlayers)
		{
			if(!pPl)
				continue;
			if(pPl->GetTeam() == TEAM_SPECTATORS)
				continue;
			if(pPl->Acc()->m_LoggedIn)
				continue;
			pSelf->SendChatTarget(pPl->GetCid(), "You have been moved to spectators because accounts are now forced on this server.");
			pPl->SetTeam(TEAM_SPECTATORS, false);
		}
	}
}
