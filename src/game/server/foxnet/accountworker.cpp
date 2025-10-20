#include <engine/server/databases/connection.h>

#include <game/server/gamecontext.h>

#include "accounts.h"
#include "accountworker.h"
#include "shop.h"
#include <base/system.h>

static bool LoadInventoryAndEquipment(IDbConnection *pSql, const char *pUsername, CInventory &Inv, char *pError, int ErrorSize)
{
	Inv.Reset();

	// Load owned items
	{
		char aInvSql[256];
		str_copy(aInvSql, "SELECT ItemName FROM foxnet_account_inventory WHERE Username = ?", sizeof(aInvSql));
		if(!pSql->PrepareStatement(aInvSql, pError, ErrorSize))
			return false;
		pSql->BindString(1, pUsername);

		bool End = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;

		while(!End)
		{
			char aFull[128] = {};
			pSql->GetString(1, aFull, sizeof(aFull));
			int Idx = CInventory::IndexOfName(aFull);
			if(Idx >= 0)
				Inv.SetOwnedIndex(Idx, true);

			if(!pSql->Step(&End, pError, ErrorSize))
				return false;
		}
	}

	// Load equipped from inventory.Value
	{
		char aEqSql[256];
		str_copy(aEqSql, "SELECT ItemName, Value FROM foxnet_account_inventory WHERE Username = ? AND Value > 0", sizeof(aEqSql));
		if(!pSql->PrepareStatement(aEqSql, pError, ErrorSize))
			return false;
		pSql->BindString(1, pUsername);

		bool End = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;

		while(!End)
		{
			char aFull[128] = {};
			pSql->GetString(1, aFull, sizeof(aFull));
			const int Val = maximum(0, pSql->GetInt(2));
			int Idx = CInventory::IndexOfName(aFull);
			if(Idx >= 0)
				Inv.SetEquippedIndex(Idx, Val > 0 ? Val : 1);

			if(!pSql->Step(&End, pError, ErrorSize))
				return false;
		}
	}

	return true;
}

static bool WriteEquippedValues(IDbConnection *pSql, const char *pUsername, const CInventory &Inv, char *pError, int ErrorSize)
{
	// Collect equipped pairs (ItemName, Value)
	struct Pair
	{
		const char *pId;
		int Val;
	};
	Pair aPairs[NUM_ITEMS];
	int Count = 0;
	for(int i = 0; i < NUM_ITEMS; i++)
	{
		const int Val = Inv.m_aEquipped[i];
		if(Val > 0)
		{
			aPairs[Count].pId = Items[i];
			aPairs[Count].Val = Val;
			Count++;
		}
	}

	if(Count == 0)
	{
		// Only reset all to 0 for this user
		char aReset[128];
		str_copy(aReset, "UPDATE foxnet_account_inventory SET Value = 0 WHERE Username = ?", sizeof(aReset));
		if(!pSql->PrepareStatement(aReset, pError, ErrorSize))
			return false;
		pSql->BindString(1, pUsername);
		int NumUpd = 0;
		return pSql->ExecuteUpdate(&NumUpd, pError, ErrorSize);
	}

	char aUpd[2048];
	int Len = str_format(aUpd, sizeof(aUpd), "%s",
		"UPDATE foxnet_account_inventory "
		"SET Value = CASE ItemName");

	for(int i = 0; i < Count; i++)
	{
		Len += str_format(aUpd + Len, sizeof(aUpd) - Len, "%s", " WHEN ? THEN ?");
	}
	Len += str_format(aUpd + Len, sizeof(aUpd) - Len, "%s", " ELSE 0 END WHERE Username = ?");

	if(!pSql->PrepareStatement(aUpd, pError, ErrorSize))
		return false;

	int BindIdx = 1;
	// Bind (ItemName, Value) pairs
	for(int i = 0; i < Count; i++)
	{
		pSql->BindString(BindIdx++, aPairs[i].pId);
		pSql->BindInt(BindIdx++, aPairs[i].Val);
	}
	// Bind Username
	pSql->BindString(BindIdx++, pUsername);

	int Num = 0;
	return pSql->ExecuteUpdate(&Num, pError, ErrorSize);
}

bool CAccountsWorker::Register(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccRegisterRequest *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());

	char aSql[256];
	str_copy(aSql, "INSERT INTO foxnet_accounts (Username, Password, RegisterDate, Flags) VALUES (?, ?, ?, ?)", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;

	pSql->BindString(1, pReq->m_aUsername);
	pSql->BindString(2, pReq->m_PasswordHash);
	pSql->BindInt64(3, pReq->m_RegisterDate);
	pSql->BindInt64(4, ACC_FLAG_AUTOLOGIN);

	int NumUpdated = 0;
	if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
		return false;

	pRes->m_Success = NumUpdated == 1;
	pRes->m_Completed.store(true);
	return true;
}

bool CAccountsWorker::Login(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccLoginRequest *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());

	char aSql[512];
	str_copy(aSql,
		"SELECT Username, RegisterDate, PlayerName, LastPlayerName, CurrentIP, LastIP, "
		"LoggedIn, LastLogin, Port, ClientId, Flags, VoteMenuPage, Playtime, Deaths, Kills, "
		"Level, XP, Money, Disabled "
		"FROM foxnet_accounts WHERE Username = ? AND Password = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;

	pSql->BindString(1, pReq->m_aUsername);
	pSql->BindString(2, pReq->m_PasswordHash);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	if(!End)
	{
		pSql->GetString(1, pRes->m_aUsername, sizeof(pRes->m_aUsername));
		pRes->m_RegisterDate = pSql->GetInt64(2);
		pSql->GetString(3, pRes->m_PlayerName, sizeof(pRes->m_PlayerName));
		pSql->GetString(4, pRes->m_LastPlayerName, sizeof(pRes->m_LastPlayerName));
		pSql->GetString(5, pRes->m_CurrentIP, sizeof(pRes->m_CurrentIP));
		pSql->GetString(6, pRes->m_LastIP, sizeof(pRes->m_LastIP));
		pRes->m_LoggedIn = pSql->GetInt(7);
		pRes->m_LastLogin = pSql->GetInt64(8);
		pRes->m_Port = pSql->GetInt(9);
		pRes->m_ClientId = pSql->GetInt(10);
		pRes->m_Flags = pSql->GetInt64(11);
		pRes->m_VoteMenuPage = pSql->GetInt(12);
		pRes->m_Playtime = pSql->GetInt64(13);
		pRes->m_Deaths = pSql->GetInt64(14);
		pRes->m_Kills = pSql->GetInt64(15);
		pRes->m_Level = pSql->GetInt64(16);
		pRes->m_XP = pSql->GetInt64(17);
		pRes->m_Money = pSql->GetInt64(18);
		pRes->m_Disabled = pSql->GetInt(19);
		pRes->m_Found = true;
		pRes->m_Success = true;
		pRes->m_Inventory.Reset();

		// Load owned items
		{
			char aInvSql[256];
			str_copy(aInvSql, "SELECT ItemName FROM foxnet_account_inventory WHERE Username = ?", sizeof(aInvSql));
			if(!pSql->PrepareStatement(aInvSql, pError, ErrorSize))
				return false;
			pSql->BindString(1, pRes->m_aUsername);

			bool End2 = true;
			if(!pSql->Step(&End2, pError, ErrorSize))
				return false;

			while(!End2)
			{
				char aFull[128] = "";
				pSql->GetString(1, aFull, sizeof(aFull));
				int Idx = CInventory::IndexOfName(aFull);
				if(Idx >= 0)
					pRes->m_Inventory.SetOwnedIndex(Idx, true);

				if(!pSql->Step(&End2, pError, ErrorSize))
					return false;
			}
		}

		// Load equipped values from inventory.Value
		{
			char aEqSql[256];
			str_copy(aEqSql, "SELECT ItemName, Value FROM foxnet_account_inventory WHERE Username = ? AND Value > 0", sizeof(aEqSql));
			if(!pSql->PrepareStatement(aEqSql, pError, ErrorSize))
				return false;
			pSql->BindString(1, pRes->m_aUsername);

			bool End2 = true;
			if(!pSql->Step(&End2, pError, ErrorSize))
				return false;

			while(!End2)
			{
				char aFull[128] = "";
				pSql->GetString(1, aFull, sizeof(aFull));
				const int Val = maximum(0, pSql->GetInt(2));
				int Idx = CInventory::IndexOfName(aFull);
				if(Idx >= 0)
					pRes->m_Inventory.SetEquippedIndex(Idx, Val > 0 ? Val : 1);

				if(!pSql->Step(&End2, pError, ErrorSize))
					return false;
			}
		}
	}
	pRes->m_Completed.store(true);
	return true;
}

bool CAccountsWorker::UpdateLoginState(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccUpdLoginState *>(pData);
	char aSql[512];
	str_copy(aSql,
		"UPDATE foxnet_accounts "
		"SET LastPlayerName = PlayerName, "
		"    PlayerName = ?, "
		"    LastIP = CurrentIP, "
		"    CurrentIP = ?, "
		"    LoggedIn = 1, "
		"    LastLogin = ?, "
		"    Port = ?, "
		"    ClientId = ? "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_PlayerName);
	pSql->BindString(2, p->m_CurrentIP);
	pSql->BindInt64(3, p->m_LastLogin);
	pSql->BindInt(4, p->m_Port);
	pSql->BindInt(5, p->m_ClientId);
	pSql->BindString(6, p->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::UpdateLogoutState(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccUpdLogoutState *>(pData);

	// Upsert owned items first (ensures inventory rows exist)
	{
		time_t Now;
		time(&Now);
		for(int i = 0; i < NUM_ITEMS; i++)
		{
			if(!pReq->m_Inventory.m_aOwned[i])
				continue;

			char aIns[256];
			str_format(aIns, sizeof(aIns),
				"%s INTO foxnet_account_inventory (Username, ItemName, Quantity, AcquiredAt, ExpiresAt, Meta) "
				"VALUES (?, ?, 1, ?, 0, '')",
				pSql->InsertIgnore());
			if(!pSql->PrepareStatement(aIns, pError, ErrorSize))
				return false;
			pSql->BindString(1, pReq->m_aUsername);
			pSql->BindString(2, Items[i]);
			pSql->BindInt64(3, (int64_t)Now);
			int NumIns = 0;
			if(!pSql->ExecuteUpdate(&NumIns, pError, ErrorSize))
				return false;
		}
	}

	// Single UPDATE with CASE for all equipped values
	if(!WriteEquippedValues(pSql, pReq->m_aUsername, pReq->m_Inventory, pError, ErrorSize))
		return false;

	// Update scalar account fields
	char aSql[512];
	str_copy(aSql,
		"UPDATE foxnet_accounts "
		"SET LoggedIn = 0, Port = 0, ClientId = -1, "
		"    LastPlayerName = PlayerName, LastIP = CurrentIP, "
		"    Flags = ?, VoteMenuPage = ?, Playtime = ?, Deaths = ?, Kills = ?, "
		"    Level = ?, XP = ?, Money = ? "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindInt64(1, pReq->m_Flags);
	pSql->BindInt(2, pReq->m_VoteMenuPage);
	pSql->BindInt64(3, pReq->m_Playtime);
	pSql->BindInt64(4, pReq->m_Deaths);
	pSql->BindInt64(5, pReq->m_Kills);
	pSql->BindInt64(6, pReq->m_Level);
	pSql->BindInt64(7, pReq->m_XP);
	pSql->BindInt64(8, pReq->m_Money);
	pSql->BindString(9, pReq->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::SaveInfo(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccSaveInfo *>(pData);

	// Upsert owned items (cheap, idempotent)
	{
		time_t Now;
		time(&Now);
		for(int i = 0; i < NUM_ITEMS; i++)
		{
			if(!p->m_Inventory.m_aOwned[i])
				continue;

			char aIns[256];
			str_format(aIns, sizeof(aIns),
				"%s INTO foxnet_account_inventory (Username, ItemName, Quantity, AcquiredAt, ExpiresAt, Meta) "
				"VALUES (?, ?, 1, ?, 0, '')",
				pSql->InsertIgnore());
			if(!pSql->PrepareStatement(aIns, pError, ErrorSize))
				return false;
			pSql->BindString(1, p->m_aUsername);
			pSql->BindString(2, Items[i]);
			pSql->BindInt64(3, (int64_t)Now);
			int Num = 0;
			if(!pSql->ExecuteUpdate(&Num, pError, ErrorSize))
				return false;
		}
	}

	// Single UPDATE with CASE for all equipped values
	if(!WriteEquippedValues(pSql, p->m_aUsername, p->m_Inventory, pError, ErrorSize))
		return false;

	char aSql[512];
	str_copy(aSql,
		"UPDATE foxnet_accounts "
		"SET LastPlayerName = PlayerName, LastIP = CurrentIP, "
		"    Flags = ?, VoteMenuPage = ?, Playtime = ?, Deaths = ?, Kills = ?, "
		"    Level = ?, XP = ?, Money = ? "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindInt64(1, p->m_Flags);
	pSql->BindInt(2, p->m_VoteMenuPage);
	pSql->BindInt64(3, p->m_Playtime);
	pSql->BindInt64(4, p->m_Deaths);
	pSql->BindInt64(5, p->m_Kills);
	pSql->BindInt64(6, p->m_Level);
	pSql->BindInt64(7, p->m_XP);
	pSql->BindInt64(8, p->m_Money);
	pSql->BindString(9, p->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::SetPlayerName(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccSetNameReq *>(pData);
	char aSql[256];
	str_copy(aSql,
		"UPDATE foxnet_accounts SET LastPlayerName = PlayerName, PlayerName = ? WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_NewPlayerName);
	pSql->BindString(2, p->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::SelectByLastPlayerName(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccSelectByLastName *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());

	char aSql[512];
	str_copy(aSql,
		"SELECT Username, RegisterDate, PlayerName, LastPlayerName, CurrentIP, LastIP, "
		"LoggedIn, LastLogin, Port, ClientId, Flags, VoteMenuPage, Playtime, Deaths, Kills, "
		"Level, XP, Money, Disabled "
		"FROM foxnet_accounts WHERE LastPlayerName = ? "
		"ORDER BY LastLogin DESC "
		"LIMIT 1",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_LastPlayerName);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	if(!End)
	{
		pSql->GetString(1, pRes->m_aUsername, sizeof(pRes->m_aUsername));
		pRes->m_RegisterDate = pSql->GetInt64(2);
		pSql->GetString(3, pRes->m_PlayerName, sizeof(pRes->m_PlayerName));
		pSql->GetString(4, pRes->m_LastPlayerName, sizeof(pRes->m_LastPlayerName));
		pSql->GetString(5, pRes->m_CurrentIP, sizeof(pRes->m_CurrentIP));
		pSql->GetString(6, pRes->m_LastIP, sizeof(pRes->m_LastIP));
		pRes->m_LoggedIn = pSql->GetInt(7);
		pRes->m_LastLogin = pSql->GetInt64(8);
		pRes->m_Port = pSql->GetInt(9);
		pRes->m_ClientId = pSql->GetInt(10);
		pRes->m_Flags = pSql->GetInt64(11);
		pRes->m_VoteMenuPage = pSql->GetInt(12);
		pRes->m_Playtime = pSql->GetInt64(13);
		pRes->m_Deaths = pSql->GetInt64(14);
		pRes->m_Kills = pSql->GetInt64(15);
		pRes->m_Level = pSql->GetInt64(16);
		pRes->m_XP = pSql->GetInt64(17);
		pRes->m_Money = pSql->GetInt64(18);
		pRes->m_Disabled = pSql->GetInt(19);
		pRes->m_Found = true;
		pRes->m_Success = true;

		// Load inventory/equipped too (was previously missing here)
		if(!LoadInventoryAndEquipment(pSql, pRes->m_aUsername, pRes->m_Inventory, pError, ErrorSize))
			return false;
	}
	pRes->m_Completed.store(true);
	return true;
}

bool CAccountsWorker::SelectByUsername(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccSelectByUser *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());

	char aSql[512];
	str_copy(aSql,
		"SELECT Username, RegisterDate, PlayerName, LastPlayerName, CurrentIP, LastIP, "
		"LoggedIn, LastLogin, Port, ClientId, Flags, VoteMenuPage, Playtime, Deaths, Kills, "
		"Level, XP, Money, Disabled "
		"FROM foxnet_accounts WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_aUsername);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	if(!End)
	{
		pSql->GetString(1, pRes->m_aUsername, sizeof(pRes->m_aUsername));
		pRes->m_RegisterDate = pSql->GetInt64(2);
		pSql->GetString(3, pRes->m_PlayerName, sizeof(pRes->m_PlayerName));
		pSql->GetString(4, pRes->m_LastPlayerName, sizeof(pRes->m_LastPlayerName));
		pSql->GetString(5, pRes->m_CurrentIP, sizeof(pRes->m_CurrentIP));
		pSql->GetString(6, pRes->m_LastIP, sizeof(pRes->m_LastIP));
		pRes->m_LoggedIn = pSql->GetInt(7);
		pRes->m_LastLogin = pSql->GetInt64(8);
		pRes->m_Port = pSql->GetInt(9);
		pRes->m_ClientId = pSql->GetInt(10);
		pRes->m_Flags = pSql->GetInt64(11);
		pRes->m_VoteMenuPage = pSql->GetInt(12);
		pRes->m_Playtime = pSql->GetInt64(13);
		pRes->m_Deaths = pSql->GetInt64(14);
		pRes->m_Kills = pSql->GetInt64(15);
		pRes->m_Level = pSql->GetInt64(16);
		pRes->m_XP = pSql->GetInt64(17);
		pRes->m_Money = pSql->GetInt64(18);
		pRes->m_Disabled = pSql->GetInt(19);
		pRes->m_Found = true;
		pRes->m_Success = true;

		if(!LoadInventoryAndEquipment(pSql, pRes->m_aUsername, pRes->m_Inventory, pError, ErrorSize))
			return false;
	}
	pRes->m_Completed.store(true);
	return true;
}

bool CAccountsWorker::SelectPortByUsername(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccSelectPortByUser *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());

	char aSql[256];
	str_copy(aSql, "SELECT Port FROM foxnet_accounts WHERE Username = ?", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_aUsername);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;
	if(!End)
	{
		pRes->m_Port = pSql->GetInt(1);
		pRes->m_Success = true;
	}
	pRes->m_Completed.store(true);
	return true;
}
bool CAccountsWorker::ShowTop5(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccShowTop5 *>(pData);
	if(!pReq || !pReq->m_pGameServer)
		return false;

	const char *pMetric = "Level";
	if(!str_comp_nocase(pReq->m_Type, "Level"))
		pMetric = "Level";
	else if(!str_comp_nocase(pReq->m_Type, "XP"))
		pMetric = "XP";
	else if(!str_comp_nocase(pReq->m_Type, "Money"))
		pMetric = "Money";
	else if(!str_comp_nocase(pReq->m_Type, "Playtime"))
		pMetric = "Playtime";
	else if(!str_comp_nocase(pReq->m_Type, "Deaths"))
		pMetric = "Deaths";
	else if(!str_comp_nocase(pReq->m_Type, "Kills"))
		pMetric = "Kills";
	else
		return false;

	const int Page = pReq->m_Offset < 0 ? 0 : pReq->m_Offset;
	const int LimitStart = Page * 5;

	char aSql[512];
	str_format(
		aSql, sizeof(aSql),
		"SELECT Username, PlayerName, %s AS Metric "
		"FROM foxnet_accounts "
		"WHERE Disabled = %s "
		"ORDER BY %s DESC, Username ASC "
		"LIMIT %d, %d",
		pMetric, pSql->False(), pMetric, LimitStart, 5);

	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;

	// Header
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "------- Top 5 by %s -------", pMetric);
	pReq->m_pGameServer->SendChatTarget(pReq->m_ClientId, aBuf);

	// Iterate rows
	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	int Rank = LimitStart + 1;
	while(!End)
	{
		char aUsername[ACC_MAX_USERNAME_LENGTH]{};
		char aPlayerName[MAX_NAME_LENGTH]{};
		pSql->GetString(1, aUsername, sizeof(aUsername));
		pSql->GetString(2, aPlayerName, sizeof(aPlayerName));
		const long Metric = pSql->GetInt64(3);

		const char *pName = aPlayerName[0] ? aPlayerName : aUsername;

		if(!str_comp(pMetric, "Playtime"))
		{
			if(Metric < 100)
				str_format(aBuf, sizeof(aBuf), "%d. %s %s: %ld Minutes", Rank, pName, pMetric, Metric);
			else
			{
				const float Hours = Metric / 60.0f;
				str_format(aBuf, sizeof(aBuf), "%d. %s %s: %.1f Hours", Rank, pName, pMetric, Hours);
			}
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%d. %s %s: %ld", Rank, pName, pMetric, Metric);
		}

		pReq->m_pGameServer->SendChatTarget(pReq->m_ClientId, aBuf);
		Rank++;

		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
	}

	// Footer
	pReq->m_pGameServer->SendChatTarget(pReq->m_ClientId, "---------------------------------");
	return true;
}

bool CAccountsWorker::DisableAccount(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccDisable *>(pData);
	char aSql[256];
	str_copy(aSql,
		"UPDATE foxnet_accounts "
		"SET LastPlayerName = PlayerName, LastIP = CurrentIP, "
		"    Disabled = ? "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindInt(1, p->m_Disable);
	pSql->BindString(2, p->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::RemoveItem(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccRemoveItem *>(pData);
	char aSql[256];
	str_copy(aSql,
		"DELETE FROM foxnet_account_inventory "
		"WHERE Username = ? AND ItemName = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_aUsername);
	pSql->BindString(2, p->m_aItemName);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}