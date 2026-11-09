#include "accountworker.h"

#include "accounts.h"
#include "accountworker.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/server/databases/connection.h>

#include <game/server/foxnet/components/shop.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <cinttypes>
#include <limits>

static int GetIntOrDefault(IDbConnection *pSql, int Col, int DefaultValue = 0)
{
	return pSql->IsNull(Col) ? DefaultValue : pSql->GetInt(Col);
}

static int64_t GetInt64OrDefault(IDbConnection *pSql, int Col, int64_t DefaultValue = 0)
{
	return pSql->IsNull(Col) ? DefaultValue : pSql->GetInt64(Col);
}

static bool ConsumeRemainingRows(IDbConnection *pSql, bool *pEnd, char *pError, int ErrorSize)
{
	while(!*pEnd)
	{
		if(!pSql->Step(pEnd, pError, ErrorSize))
			return false;
	}
	return true;
}

static bool EnsureAccountRegistrationsTable(IDbConnection *pSql, char *pError, int ErrorSize)
{
	char aSql[512];
	str_format(aSql, sizeof(aSql),
		"CREATE TABLE IF NOT EXISTS foxnet_account_registrations ("
		"IP VARCHAR(45) COLLATE %s NOT NULL, "
		"Slot INTEGER NOT NULL, "
		"Username VARCHAR(32) COLLATE %s NOT NULL, "
		"PRIMARY KEY (Username), UNIQUE (IP, Slot))",
		pSql->BinaryCollate(), pSql->BinaryCollate());
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

static bool LoadInventoryAndEquipment(IDbConnection *pSql, const char *pUsername, CInventory &Inv, char *pError, int ErrorSize)
{
	// Clear current inventory & cosmetics
	Inv.Reset();

	char aSql[128];
	str_copy(aSql, "SELECT ItemName, Quantity, Value, AcquiredAt, ExpiresAt FROM foxnet_account_inventory WHERE Username = ?", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;

	pSql->BindString(1, pUsername);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	while(!End)
	{
		char aItemName[64];
		pSql->GetString(1, aItemName, sizeof(aItemName));
		const int Quantity = GetIntOrDefault(pSql, 2);
		const int Value = GetIntOrDefault(pSql, 3);
		const int64_t AcquiredAt = GetInt64OrDefault(pSql, 4);
		const int64_t ExpiresAt = GetInt64OrDefault(pSql, 5);

		auto &Entry = Inv.Entry(aItemName);
		Entry.m_Quantity = Quantity;
		Entry.m_Value = Value;
		Entry.m_AcquiredAt = AcquiredAt;
		Entry.m_ExpiresAt = ExpiresAt;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
	}

	return true;
}

static bool UpdateItemValues(IDbConnection *pSql, const char *pUsername, const CInventory &Inv, char *pError, int ErrorSize)
{
	struct SEquip
	{
		const char *pName;
		int Val;
	};
	struct SQty
	{
		const char *pName;
		int Qty;
	};
	struct STime
	{
		const char *pName;
		int64_t Acq;
		int64_t Exp;
	};

	std::vector<SEquip> vEquip;
	std::vector<SQty> vQty;
	std::vector<STime> vTime;

	for(const auto &Item : Inv.m_Map)
	{
		const std::string &Name = Item.first;
		const CInventoryEntry &Entry = Item.second;

		vQty.push_back({Name.c_str(), Entry.m_Quantity});
		vEquip.push_back({Name.c_str(), Entry.m_Value});

		if(Entry.m_Quantity > 0)
			vTime.push_back({Name.c_str(), Entry.m_AcquiredAt, Entry.m_ExpiresAt});
	}

	char aUpd[8192] = "";
	str_append(aUpd, "UPDATE foxnet_account_inventory SET ", sizeof(aUpd));

	// Value
	if(!vEquip.empty())
	{
		str_append(aUpd, "Value = CASE ItemName", sizeof(aUpd));
		for(size_t e = 0; e < vEquip.size(); e++)
			str_append(aUpd, " WHEN ? THEN ?", sizeof(aUpd));
		str_append(aUpd, " ELSE Value END", sizeof(aUpd));
	}
	else
	{
		// No items to change -> keep existing Value
		str_append(aUpd, "Value = Value", sizeof(aUpd));
	}

	// Quantity
	str_append(aUpd, ", ", sizeof(aUpd));
	if(!vQty.empty())
	{
		str_append(aUpd, "Quantity = CASE ItemName", sizeof(aUpd));
		for(size_t q = 0; q < vQty.size(); q++)
			str_append(aUpd, " WHEN ? THEN ?", sizeof(aUpd));
		str_append(aUpd, " ELSE Quantity END", sizeof(aUpd));
	}
	else
	{
		str_append(aUpd, "Quantity = Quantity", sizeof(aUpd));
	}

	// Times
	if(!vTime.empty())
	{
		str_append(aUpd, ", AcquiredAt = CASE ItemName", sizeof(aUpd));
		for(size_t t = 0; t < vTime.size(); t++)
			str_append(aUpd, " WHEN ? THEN ?", sizeof(aUpd));
		str_append(aUpd, " ELSE AcquiredAt END", sizeof(aUpd));

		str_append(aUpd, ", ExpiresAt = CASE ItemName", sizeof(aUpd));
		for(size_t t = 0; t < vTime.size(); t++)
			str_append(aUpd, " WHEN ? THEN ?", sizeof(aUpd));
		str_append(aUpd, " ELSE ExpiresAt END", sizeof(aUpd));
	}

	str_append(aUpd, " WHERE Username = ?", sizeof(aUpd));

	if(!pSql->PrepareStatement(aUpd, pError, ErrorSize))
		return false;

	int Bind = 1;
	// Bind Value cases
	for(const auto &e : vEquip)
	{
		pSql->BindString(Bind++, e.pName);
		pSql->BindInt(Bind++, e.Val);
	}
	// Bind Quantity cases
	for(const auto &q : vQty)
	{
		pSql->BindString(Bind++, q.pName);
		pSql->BindInt(Bind++, q.Qty);
	}
	// Bind times
	for(const auto &t : vTime)
	{
		pSql->BindString(Bind++, t.pName);
		pSql->BindInt64(Bind++, t.Acq);
	}
	for(const auto &t : vTime)
	{
		pSql->BindString(Bind++, t.pName);
		pSql->BindInt64(Bind++, t.Exp);
	}

	pSql->BindString(Bind++, pUsername);

	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}
static bool LoadMailbox(IDbConnection *pSql, const char *pUsername, CMailBox &MailBox, char *pError, int ErrorSize)
{
	MailBox.Clear();
	char aSql[256];
	str_copy(aSql, "SELECT MailId, Subject, Message, Command, CommandName, UsedCommand, Unread FROM foxnet_account_mailbox WHERE Username = ?", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pUsername);
	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;
	while(!End)
	{
		CMailBox::CMail Mail;
		Mail.m_MailId = GetInt64OrDefault(pSql, 1);
		pSql->GetString(2, Mail.m_aSubject, sizeof(Mail.m_aSubject));
		pSql->GetString(3, Mail.m_aMessage, sizeof(Mail.m_aMessage));
		pSql->GetString(4, Mail.m_aCmd, sizeof(Mail.m_aCmd));
		pSql->GetString(5, Mail.m_aCmdName, sizeof(Mail.m_aCmdName));
		Mail.m_UsedCmd = GetIntOrDefault(pSql, 6) != 0;
		Mail.m_Unread = GetIntOrDefault(pSql, 7) != 0;
		MailBox.m_vMails.push_back(Mail);
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
	}
	return true;
}

// Local copy of the helper scoreworker.cpp uses, so a refused transfer leaves
// nothing behind.
class CScopedAccTransaction
{
	IDbConnection *m_pSql;
	bool m_Active = false;

public:
	explicit CScopedAccTransaction(IDbConnection *pSql) :
		m_pSql(pSql) {}
	bool Begin(char *pError, int ErrorSize)
	{
		if(!m_pSql->BeginTransaction(pError, ErrorSize))
			return false;
		m_Active = true;
		return true;
	}
	bool Commit(char *pError, int ErrorSize)
	{
		int NumUpdated = 0;
		if(!m_pSql->PrepareStatement("COMMIT", pError, ErrorSize) || !m_pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
			return false;
		m_Active = false;
		return true;
	}
	~CScopedAccTransaction()
	{
		if(!m_Active)
			return;
		char aError[256];
		int NumUpdated = 0;
		if(m_pSql->PrepareStatement("ROLLBACK", aError, sizeof(aError)))
			m_pSql->ExecuteUpdate(&NumUpdated, aError, sizeof(aError));
	}
};

static bool DeleteRowsByUsername(IDbConnection *pSql, const char *pTable, const char *pUsername, int *pNumDeleted, char *pError, int ErrorSize)
{
	char aSql[256];
	str_format(aSql, sizeof(aSql), "DELETE FROM %s WHERE Username = ?", pTable);
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pUsername);
	return pSql->ExecuteUpdate(pNumDeleted, pError, ErrorSize);
}

static bool UpsertConfigBool(IDbConnection *pSql, const char *pUsername, const char *pKey, bool Value, char *pError, int ErrorSize)
{
	{
		char aIns[256];
		str_format(aIns, sizeof(aIns),
			"%s INTO foxnet_account_config (Username, `Key`, Value, Type, UpdatedAt) "
			"VALUES (?, ?, ?, 'bool', ?)",
			pSql->InsertIgnore());
		if(!pSql->PrepareStatement(aIns, pError, ErrorSize))
			return false;

		char aVal[2] = {Value ? '1' : '0', 0};
		int64_t Now = (int64_t)time(0);
		int Param = 1;
		pSql->BindString(Param++, pUsername);
		pSql->BindString(Param++, pKey);
		pSql->BindString(Param++, aVal);
		pSql->BindInt64(Param++, Now);

		int Num = 0;
		if(!pSql->ExecuteUpdate(&Num, pError, ErrorSize))
			return false;
	}

	{
		char aUpd[256];
		str_copy(aUpd,
			"UPDATE foxnet_account_config "
			"SET Value = ?, Type = 'bool', UpdatedAt = ? "
			"WHERE Username = ? AND `Key` = ?",
			sizeof(aUpd));
		if(!pSql->PrepareStatement(aUpd, pError, ErrorSize))
			return false;

		char aVal[2] = {Value ? '1' : '0', 0};
		int64_t Now = (int64_t)time(0);
		int Param = 1;
		pSql->BindString(Param++, aVal);
		pSql->BindInt64(Param++, Now);
		pSql->BindString(Param++, pUsername);
		pSql->BindString(Param++, pKey);

		int Num = 0;
		if(!pSql->ExecuteUpdate(&Num, pError, ErrorSize))
			return false;
	}
	return true;
}

static bool UpserConfigInteger(IDbConnection *pSql, const char *pUsername, const char *pKey, int Value, char *pError, int ErrorSize)
{
	{
		char aIns[256];
		str_format(aIns, sizeof(aIns),
			"%s INTO foxnet_account_config (Username, `Key`, Value, Type, UpdatedAt) "
			"VALUES (?, ?, ?, 'integer', ?)",
			pSql->InsertIgnore());
		if(!pSql->PrepareStatement(aIns, pError, ErrorSize))
			return false;
		int64_t Now = (int64_t)time(0);
		int Param = 1;
		pSql->BindString(Param++, pUsername);
		pSql->BindString(Param++, pKey);
		pSql->BindInt(Param++, Value);
		pSql->BindInt64(Param++, Now);
		int Num = 0;
		if(!pSql->ExecuteUpdate(&Num, pError, ErrorSize))
			return false;
	}
	{
		char aUpd[256];
		str_copy(aUpd,
			"UPDATE foxnet_account_config "
			"SET Value = ?, Type = 'integer', UpdatedAt = ? "
			"WHERE Username = ? AND `Key` = ?",
			sizeof(aUpd));
		if(!pSql->PrepareStatement(aUpd, pError, ErrorSize))
			return false;
		int64_t Now = (int64_t)time(0);
		int Param = 1;
		pSql->BindInt(Param++, Value);
		pSql->BindInt64(Param++, Now);
		pSql->BindString(Param++, pUsername);
		pSql->BindString(Param++, pKey);
		int Num = 0;
		if(!pSql->ExecuteUpdate(&Num, pError, ErrorSize))
			return false;
	}
	return true;
}

static bool LoadConfigs(IDbConnection *pSql, const char *pUsername, CAccConfigs &Configs, char *pError, int ErrorSize)
{
	char aSel[256];
	str_copy(aSel,
		"SELECT `Key`, Value "
		"FROM foxnet_account_config "
		"WHERE Username = ?",
		sizeof(aSel));
	if(!pSql->PrepareStatement(aSel, pError, ErrorSize))
		return false;
	pSql->BindString(1, pUsername);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	while(!End)
	{
		char aKey[65] = {0};
		pSql->GetString(1, aKey, sizeof(aKey));
		const int Value = GetIntOrDefault(pSql, 2);
		const bool On = Value != 0;

		if(!str_comp(aKey, g_apAccConfigNames[CONFIG_AUTLOGIN]))
			Configs.m_AutoLogin = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_SPAWNSOLO]))
			Configs.m_UseSpawnSolo = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_FASTINPUTS]))
			Configs.m_FastInputs = On;

		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_HIDEPOWERUPS]))
			Configs.m_HidePowerUps = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_HIDEDISTRACTINGEFFECTS]))
			Configs.m_HideDistractingEffects = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_HATITEMFLAGS]))
			Configs.m_HatItemFlags = Value;

		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_SHOWWEAPONDROPS]))
			Configs.m_ShowWeaponDrops = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_WEAPONDROPSUSINGVOTENO]))
			Configs.m_WeaponDropsUsingVoteNo = On;

		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_COSMETIC_RAINBOW]))
			Configs.m_Cosmetics.m_ShowRainbow = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_COSMETIC_GUNS]))
			Configs.m_Cosmetics.m_ShowGuns = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_COSMETIC_INDICATORS]))
			Configs.m_Cosmetics.m_ShowIndicators = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_COSMETIC_DEATHS]))
			Configs.m_Cosmetics.m_ShowDeaths = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_COSMETIC_TRAILS]))
			Configs.m_Cosmetics.m_ShowTrails = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_COSMETIC_HATS]))
			Configs.m_Cosmetics.m_ShowHats = On;
		else if(!str_comp(aKey, g_apAccConfigNames[CONFIG_COSMETIC_EFFECTS]))
			Configs.m_Cosmetics.m_ShowEffects = On;

		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
	}
	return true;
}

static bool SaveConfigs(IDbConnection *pSql, const char *pUsername, const CAccConfigs &Configs, char *pError, int ErrorSize)
{
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_AUTLOGIN], Configs.m_AutoLogin, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_SPAWNSOLO], Configs.m_UseSpawnSolo, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_FASTINPUTS], Configs.m_FastInputs, pError, ErrorSize))
		return false;

	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_HIDEPOWERUPS], Configs.m_HidePowerUps, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_HIDEDISTRACTINGEFFECTS], Configs.m_HideDistractingEffects, pError, ErrorSize))
		return false;
	if(!UpserConfigInteger(pSql, pUsername, g_apAccConfigNames[CONFIG_HATITEMFLAGS], Configs.m_HatItemFlags, pError, ErrorSize))
		return false;

	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_SHOWWEAPONDROPS], Configs.m_ShowWeaponDrops, pError, ErrorSize))
		return false;
	if(!UpserConfigInteger(pSql, pUsername, g_apAccConfigNames[CONFIG_WEAPONDROPSUSINGVOTENO], Configs.m_WeaponDropsUsingVoteNo, pError, ErrorSize))
		return false;

	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_COSMETIC_RAINBOW], Configs.m_Cosmetics.m_ShowRainbow, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_COSMETIC_GUNS], Configs.m_Cosmetics.m_ShowGuns, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_COSMETIC_INDICATORS], Configs.m_Cosmetics.m_ShowIndicators, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_COSMETIC_DEATHS], Configs.m_Cosmetics.m_ShowDeaths, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_COSMETIC_TRAILS], Configs.m_Cosmetics.m_ShowTrails, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_COSMETIC_HATS], Configs.m_Cosmetics.m_ShowHats, pError, ErrorSize))
		return false;
	if(!UpsertConfigBool(pSql, pUsername, g_apAccConfigNames[CONFIG_COSMETIC_EFFECTS], Configs.m_Cosmetics.m_ShowEffects, pError, ErrorSize))
		return false;

	return true;
}

bool CAccountsWorker::Register(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccRegisterRequest *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
		return false;
	if(!EnsureAccountRegistrationsTable(pSql, pError, ErrorSize))
		return false;

	char aSql[256];
	str_copy(aSql, "SELECT Username FROM foxnet_accounts WHERE Username = ?", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_aUsername);
	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;
	if(!End)
	{
		pRes->m_RegisterStatus = CAccResult::ERegisterStatus::USERNAME_TAKEN;
		if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
			return false;
		pRes->m_Completed.store(true);
		return true;
	}

	bool aSlotsUsed[10]{};
	str_copy(aSql, "SELECT Slot FROM foxnet_account_registrations WHERE IP = ?", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_RegisterIP);
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;
	while(!End)
	{
		const int Slot = pSql->GetInt(1);
		if(Slot >= 1 && Slot <= 10)
			aSlotsUsed[Slot - 1] = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
	}

	int FreeSlot = -1;
	for(int i = 0; i < pReq->m_MaxAccountsPerIP; ++i)
	{
		if(!aSlotsUsed[i])
		{
			FreeSlot = i + 1;
			break;
		}
	}
	if(FreeSlot < 0)
	{
		pRes->m_RegisterStatus = CAccResult::ERegisterStatus::IP_LIMIT_REACHED;
		pRes->m_Completed.store(true);
		return true;
	}

	str_copy(aSql, "INSERT INTO foxnet_account_registrations (IP, Slot, Username) VALUES (?, ?, ?)", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_RegisterIP);
	pSql->BindInt(2, FreeSlot);
	pSql->BindString(3, pReq->m_aUsername);
	int NumUpdated = 0;
	if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
		return false;

	str_copy(aSql, "INSERT INTO foxnet_accounts (Username, Password, RegisterDate) VALUES (?, ?, ?)", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_aUsername);
	pSql->BindString(2, pReq->m_PasswordHash);
	pSql->BindInt64(3, pReq->m_RegisterDate);
	if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
	{
		DeleteRowsByUsername(pSql, "foxnet_account_registrations", pReq->m_aUsername, &NumUpdated, pError, ErrorSize);
		return false;
	}

	pRes->m_RegisterStatus = NumUpdated == 1 ? CAccResult::ERegisterStatus::SUCCESS : CAccResult::ERegisterStatus::USERNAME_TAKEN;
	pRes->m_Success = pRes->m_RegisterStatus == CAccResult::ERegisterStatus::SUCCESS;
	pRes->m_Completed.store(true);

	if(pRes->m_Success)
	{
		// Defaults
		if(!UpsertConfigBool(pSql, pReq->m_aUsername, g_apAccConfigNames[CONFIG_AUTLOGIN], true, pError, ErrorSize))
			return false;
	}

	return true;
}

bool CAccountsWorker::Login(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccLoginRequest *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());

	char aSql[512];
	str_copy(aSql,
		"SELECT Username, RegisterDate, PlayerName, LastPlayerName, CurrentIP, LastIP, "
		"LoggedIn, LastLogin, Port, ClientId, Playtime, Deaths, Kills, "
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
		int Param = 1;
		pSql->GetString(Param++, pRes->m_aUsername, sizeof(pRes->m_aUsername));
		pRes->m_RegisterDate = GetInt64OrDefault(pSql, Param++);
		pSql->GetString(Param++, pRes->m_PlayerName, sizeof(pRes->m_PlayerName));
		pSql->GetString(Param++, pRes->m_LastPlayerName, sizeof(pRes->m_LastPlayerName));
		pSql->GetString(Param++, pRes->m_CurrentIP, sizeof(pRes->m_CurrentIP));
		pSql->GetString(Param++, pRes->m_LastIP, sizeof(pRes->m_LastIP));
		pRes->m_LoggedIn = GetIntOrDefault(pSql, Param++);
		pRes->m_LastLogin = GetInt64OrDefault(pSql, Param++);
		pRes->m_Port = GetIntOrDefault(pSql, Param++);
		pRes->m_ClientId = GetIntOrDefault(pSql, Param++);
		pRes->m_Playtime = GetInt64OrDefault(pSql, Param++);
		pRes->m_Deaths = GetInt64OrDefault(pSql, Param++);
		pRes->m_Kills = GetInt64OrDefault(pSql, Param++);
		pRes->m_Level = GetInt64OrDefault(pSql, Param++);
		pRes->m_XP = GetInt64OrDefault(pSql, Param++);
		pRes->m_Money = GetInt64OrDefault(pSql, Param++);
		pRes->m_Disabled = GetIntOrDefault(pSql, Param++);
		pRes->m_Found = true;
		pRes->m_Success = true;
		if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
			return false;
	}
	if(pRes->m_Found)
	{
		if(!LoadConfigs(pSql, pRes->m_aUsername, pRes->m_Configs, pError, ErrorSize))
			return false;
		if(!LoadInventoryAndEquipment(pSql, pRes->m_aUsername, pRes->m_Inventory, pError, ErrorSize))
			return false;
		if(!LoadMailbox(pSql, pRes->m_aUsername, pRes->m_MailBox, pError, ErrorSize))
			return false;
	}
	pRes->m_Completed.store(true);
	return true;
}

bool CAccountsWorker::UpdateLoginState(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *p = dynamic_cast<const CAccUpdLoginState *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());
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
		"    ClientId = ?, "
		"    ServerInstance = ? "
		"WHERE Username = ? AND LoggedIn = 0",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_PlayerName);
	pSql->BindString(2, p->m_CurrentIP);
	pSql->BindInt64(3, p->m_LastLogin);
	pSql->BindInt(4, p->m_Port);
	pSql->BindInt(5, p->m_ClientId);
	pSql->BindString(6, p->m_aInstance);
	pSql->BindString(7, p->m_aUsername);
	int NumUpdated = 0;
	if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
		return false;
	if(pRes && w == Write::NORMAL)
		pRes->m_LoginStateUpdated = NumUpdated == 1;
	return true;
}

bool CAccountsWorker::UpdateLogoutState(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccSaveInfo *>(pData);
	if(!pReq)
		return false;

	// Persist config changes on logout as well
	if(!SaveConfigs(pSql, pReq->m_aUsername, pReq->m_Configs, pError, ErrorSize))
		return false;

	// Upsert owned items (Quantity > 0)
	for(const auto &kv : pReq->m_Inventory.m_Map)
	{
		const CInventoryEntry &E = kv.second;
		if(E.m_Quantity <= 0)
			continue;

		char aIns[256];
		str_format(aIns, sizeof(aIns),
			"%s INTO foxnet_account_inventory (Username, ItemName, Quantity, AcquiredAt, ExpiresAt, Meta) "
			"VALUES (?, ?, ?, ?, ?, '')",
			pSql->InsertIgnore());
		if(!pSql->PrepareStatement(aIns, pError, ErrorSize))
			return false;
		pSql->BindString(1, pReq->m_aUsername);
		pSql->BindString(2, kv.first.c_str());
		pSql->BindInt(3, E.m_Quantity);
		pSql->BindInt64(4, E.m_AcquiredAt);
		pSql->BindInt64(5, E.m_ExpiresAt);
		int NumIns = 0;
		if(!pSql->ExecuteUpdate(&NumIns, pError, ErrorSize))
			return false;
	}

	if(!UpdateItemValues(pSql, pReq->m_aUsername, pReq->m_Inventory, pError, ErrorSize))
		return false;

	// Account scalar fields
	char aSql[512];
	str_copy(aSql,
		"UPDATE foxnet_accounts "
		"SET LoggedIn = 0, Port = 0, ClientId = -1, ServerInstance = '', "
		"    LastPlayerName = PlayerName, LastIP = CurrentIP, "
		"    Playtime = ?, Deaths = ?, Kills = ?, "
		"    Level = ?, XP = ? "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	int Param = 1;
	pSql->BindInt64(Param++, pReq->m_Playtime);
	pSql->BindInt64(Param++, pReq->m_Deaths);
	pSql->BindInt64(Param++, pReq->m_Kills);
	pSql->BindInt64(Param++, pReq->m_Level);
	pSql->BindInt64(Param++, pReq->m_XP);
	pSql->BindString(Param++, pReq->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::SaveInfo(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccSaveInfo *>(pData);
	if(!pReq)
		return false;

	if(!SaveConfigs(pSql, pReq->m_aUsername, pReq->m_Configs, pError, ErrorSize))
		return false;

	// Upsert owned items
	for(const auto &kv : pReq->m_Inventory.m_Map)
	{
		const CInventoryEntry &Entry = kv.second;
		if(Entry.m_Quantity <= 0)
			continue;

		char aIns[256];
		str_format(aIns, sizeof(aIns),
			"%s INTO foxnet_account_inventory (Username, ItemName, Quantity, AcquiredAt, ExpiresAt, Meta) "
			"VALUES (?, ?, ?, ?, ?, '')",
			pSql->InsertIgnore());
		if(!pSql->PrepareStatement(aIns, pError, ErrorSize))
			return false;
		pSql->BindString(1, pReq->m_aUsername);
		pSql->BindString(2, kv.first.c_str());
		pSql->BindInt(3, Entry.m_Quantity);
		pSql->BindInt64(4, Entry.m_AcquiredAt);
		pSql->BindInt64(5, Entry.m_ExpiresAt);
		int NumIns = 0;
		if(!pSql->ExecuteUpdate(&NumIns, pError, ErrorSize))
			return false;
	}

	if(!UpdateItemValues(pSql, pReq->m_aUsername, pReq->m_Inventory, pError, ErrorSize))
		return false;

	// Money is deliberately not written here. It moves through MoneyDelta as a
	// relative update, so an absolute write would clobber a change this session
	// has not seen yet, including one made directly in the database.
	char aSql[512];
	str_copy(aSql,
		"UPDATE foxnet_accounts "
		"SET LastPlayerName = PlayerName, LastIP = CurrentIP, "
		"     Playtime = ?, Deaths = ?, Kills = ?, "
		"    Level = ?, XP = ? "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	int Param = 1;
	pSql->BindInt64(Param++, pReq->m_Playtime);
	pSql->BindInt64(Param++, pReq->m_Deaths);
	pSql->BindInt64(Param++, pReq->m_Kills);
	pSql->BindInt64(Param++, pReq->m_Level);
	pSql->BindInt64(Param++, pReq->m_XP);
	pSql->BindString(Param++, pReq->m_aUsername);
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
		"LoggedIn, LastLogin, Port, ClientId, Playtime, Deaths, Kills, "
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
		int Param = 1;
		pSql->GetString(Param++, pRes->m_aUsername, sizeof(pRes->m_aUsername));
		pRes->m_RegisterDate = GetInt64OrDefault(pSql, Param++);
		pSql->GetString(Param++, pRes->m_PlayerName, sizeof(pRes->m_PlayerName));
		pSql->GetString(Param++, pRes->m_LastPlayerName, sizeof(pRes->m_LastPlayerName));
		pSql->GetString(Param++, pRes->m_CurrentIP, sizeof(pRes->m_CurrentIP));
		pSql->GetString(Param++, pRes->m_LastIP, sizeof(pRes->m_LastIP));
		pRes->m_LoggedIn = GetIntOrDefault(pSql, Param++);
		pRes->m_LastLogin = GetInt64OrDefault(pSql, Param++);
		pRes->m_Port = GetIntOrDefault(pSql, Param++);
		pRes->m_ClientId = GetIntOrDefault(pSql, Param++);
		pRes->m_Playtime = GetInt64OrDefault(pSql, Param++);
		pRes->m_Deaths = GetInt64OrDefault(pSql, Param++);
		pRes->m_Kills = GetInt64OrDefault(pSql, Param++);
		pRes->m_Level = GetInt64OrDefault(pSql, Param++);
		pRes->m_XP = GetInt64OrDefault(pSql, Param++);
		pRes->m_Money = GetInt64OrDefault(pSql, Param++);
		pRes->m_Disabled = GetIntOrDefault(pSql, Param++);
		pRes->m_Found = true;
		pRes->m_Success = true;

		if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
			return false;

		if(!LoadConfigs(pSql, pRes->m_aUsername, pRes->m_Configs, pError, ErrorSize))
			return false;
		if(!LoadInventoryAndEquipment(pSql, pRes->m_aUsername, pRes->m_Inventory, pError, ErrorSize))
			return false;
		if(!LoadMailbox(pSql, pRes->m_aUsername, pRes->m_MailBox, pError, ErrorSize))
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
		"LoggedIn, LastLogin, Port, ClientId, Playtime, Deaths, Kills, "
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
		int Param = 1;
		pSql->GetString(Param++, pRes->m_aUsername, sizeof(pRes->m_aUsername));
		pRes->m_RegisterDate = GetInt64OrDefault(pSql, Param++);
		pSql->GetString(Param++, pRes->m_PlayerName, sizeof(pRes->m_PlayerName));
		pSql->GetString(Param++, pRes->m_LastPlayerName, sizeof(pRes->m_LastPlayerName));
		pSql->GetString(Param++, pRes->m_CurrentIP, sizeof(pRes->m_CurrentIP));
		pSql->GetString(Param++, pRes->m_LastIP, sizeof(pRes->m_LastIP));
		pRes->m_LoggedIn = GetIntOrDefault(pSql, Param++);
		pRes->m_LastLogin = GetInt64OrDefault(pSql, Param++);
		pRes->m_Port = GetIntOrDefault(pSql, Param++);
		pRes->m_ClientId = GetIntOrDefault(pSql, Param++);
		pRes->m_Playtime = GetInt64OrDefault(pSql, Param++);
		pRes->m_Deaths = GetInt64OrDefault(pSql, Param++);
		pRes->m_Kills = GetInt64OrDefault(pSql, Param++);
		pRes->m_Level = GetInt64OrDefault(pSql, Param++);
		pRes->m_XP = GetInt64OrDefault(pSql, Param++);
		pRes->m_Money = GetInt64OrDefault(pSql, Param++);
		pRes->m_Disabled = GetIntOrDefault(pSql, Param++);
		pRes->m_Found = true;
		pRes->m_Success = true;
		if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
			return false;

		if(!LoadConfigs(pSql, pRes->m_aUsername, pRes->m_Configs, pError, ErrorSize))
			return false;
		if(!LoadInventoryAndEquipment(pSql, pRes->m_aUsername, pRes->m_Inventory, pError, ErrorSize))
			return false;
		if(!LoadMailbox(pSql, pRes->m_aUsername, pRes->m_MailBox, pError, ErrorSize))
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
		pRes->m_Port = GetIntOrDefault(pSql, 1);
		pRes->m_Success = true;
		if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
			return false;
	}
	pRes->m_Completed.store(true);
	return true;
}
bool CAccountsWorker::ShowTop5(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	if(w != Write::NORMAL)
		return true;

	const auto *pReq = dynamic_cast<const CAccShowTop5 *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
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
	const int LimitStart = Page;

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
	str_format(aBuf, sizeof(aBuf), "------- Global Top %s -------", pMetric);
	pRes->AddMessage(aBuf);

	// Iterate rows
	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	int Rank = LimitStart + 1;
	while(!End)
	{
		char aUsername[ACC_MAX_USERNAME_LENGTH]{};
		char aPlayerName[MAX_NAME_LENGTH]{};
		int Param = 1;
		pSql->GetString(Param++, aUsername, sizeof(aUsername));
		pSql->GetString(Param++, aPlayerName, sizeof(aPlayerName));
		const int64_t Metric = GetInt64OrDefault(pSql, Param++);

		const char *pName = aPlayerName[0] ? aPlayerName : aUsername;

		if(!str_comp(pMetric, "Playtime"))
		{
			str_format(aBuf, sizeof(aBuf), "%d. '%s': %s", Rank, pName, FormatPlaytime(Metric));
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%d. '%s' %s: %" PRId64, Rank, pName, pMetric, Metric);
		}

		pRes->AddMessage(aBuf);
		Rank++;

		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
	}

	pRes->AddMessage("---------------------------------");
	pRes->m_Success = true;
	pRes->m_Completed.store(true);
	// Footer
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
	int Param = 1;
	pSql->BindInt(Param++, p->m_Disable);
	pSql->BindString(Param++, p->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::DeleteAccount(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccDelete *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
		return false;
	if(!EnsureAccountRegistrationsTable(pSql, pError, ErrorSize))
		return false;

	char aSql[256];
	str_copy(aSql, "SELECT Username FROM foxnet_accounts WHERE Username = ?", sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_aUsername);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;
	pRes->m_Found = !End;
	if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
		return false;

	int NumDeleted = 0;
	if(!DeleteRowsByUsername(pSql, "foxnet_account_mailbox", pReq->m_aUsername, &NumDeleted, pError, ErrorSize))
		return false;
	if(!DeleteRowsByUsername(pSql, "foxnet_account_timers", pReq->m_aUsername, &NumDeleted, pError, ErrorSize))
		return false;
	if(!DeleteRowsByUsername(pSql, "foxnet_account_config", pReq->m_aUsername, &NumDeleted, pError, ErrorSize))
		return false;
	if(!DeleteRowsByUsername(pSql, "foxnet_account_inventory", pReq->m_aUsername, &NumDeleted, pError, ErrorSize))
		return false;
	if(!DeleteRowsByUsername(pSql, "foxnet_account_registrations", pReq->m_aUsername, &NumDeleted, pError, ErrorSize))
		return false;
	if(!DeleteRowsByUsername(pSql, "foxnet_accounts", pReq->m_aUsername, &NumDeleted, pError, ErrorSize))
		return false;

	if(pRes->m_Found)
		pRes->AddMessage("Account deleted successfully");
	else
		pRes->AddMessage("Account doesn't exist");

	pRes->m_Success = true;
	pRes->m_Completed.store(true);
	return true;
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
	int Param = 1;
	pSql->BindString(Param++, p->m_aUsername);
	pSql->BindString(Param++, p->m_aItemName);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::ChangePassword(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	if(w != Write::NORMAL)
		return true;

	const auto *p = dynamic_cast<const CAccChangePassword *>(pData);
	auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());
	if(!p || !pRes)
		return false;
	const char *aSql = "UPDATE foxnet_accounts SET Password = ? WHERE Username = ? AND Password = ?";
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, p->m_NewHash);
	pSql->BindString(2, p->m_aUsername);
	pSql->BindString(3, p->m_OldHash);
	int NumUpdated = 0;
	if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
		return false;

	if(NumUpdated > 0)
		pRes->AddMessage("Password changed successfully");
	else
		pRes->AddMessage("[Err] Old password is incorrect");

	pRes->m_Success = NumUpdated > 0;
	pRes->m_Completed.store(true);
	return true;
}

bool CAccountsWorker::SetPassword(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	if(w != Write::NORMAL)
		return true;

	const auto *p = dynamic_cast<const CAccSetPassword *>(pData);
	char aSql[256];
	str_copy(aSql,
		"UPDATE foxnet_accounts "
		"SET Password = ? "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	int Param = 1;
	pSql->BindString(Param++, p->m_aNewPasswordHash);
	pSql->BindString(Param++, p->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::SetMailRead(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccSetMailRead *>(pData);
	char aSql[256];
	str_copy(aSql,
		"UPDATE foxnet_account_mailbox "
		"SET Unread = ? "
		"WHERE Username = ? AND MailId = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	int Param = 1;
	pSql->BindInt(Param++, pReq->m_Read ? 0 : 1);
	pSql->BindString(Param++, pReq->m_aUsername);
	pSql->BindInt64(Param++, pReq->m_MailId);

	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::MarkAllMailsRead(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccMarkAllMailsRead *>(pData);
	char aSql[256];
	str_copy(aSql,
		"UPDATE foxnet_account_mailbox "
		"SET Unread = 0 "
		"WHERE Username = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_aUsername);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::DeleteAllReadMails(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccDeleteAllRead *>(pData);
	if(!pReq)
		return false;

	// Delete mails that are read AND (have no reward OR reward already claimed)
	// Reward indicator: Command <> '' (CommandName is not authoritative in your codebase)
	char aSql[256];
	str_copy(aSql,
		"DELETE FROM foxnet_account_mailbox "
		"WHERE Username = ? AND Unread = 0 AND (Command = '' OR UsedCommand = 1)",
		sizeof(aSql));

	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;

	int Param = 1;
	pSql->BindString(Param++, pReq->m_aUsername);

	int NumDeleted = 0;
	return pSql->ExecuteUpdate(&NumDeleted, pError, ErrorSize);
}

bool CAccountsWorker::SetMailUsedCmd(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccSetMailUsedCmd *>(pData);
	char aSql[256];
	str_copy(aSql,
		"UPDATE foxnet_account_mailbox "
		"SET UsedCommand = ? "
		"WHERE Username = ? AND MailId = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	int Param = 1;
	pSql->BindInt(Param++, pReq->m_UsedCmd);
	pSql->BindString(Param++, pReq->m_aUsername);
	pSql->BindInt64(Param++, pReq->m_MailId);
	int NumUpdated = 0;
	return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

bool CAccountsWorker::ClaimMailReward(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccClaimMailReward *>(pData);
	auto *pRes = dynamic_cast<CAccClaimMailResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
		return false;
	if(w == Write::NORMAL_FAILED)
	{
		// A backup-only claim cannot guarantee exclusivity against servers that
		// still use the primary database, so it must never grant a reward.
		return true;
	}

	char aSql[256];
	str_copy(aSql,
		"UPDATE foxnet_account_mailbox "
		"SET UsedCommand = 1 "
		"WHERE Username = ? AND MailId = ? AND UsedCommand = 0",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;

	int Param = 1;
	pSql->BindString(Param++, pReq->m_aUsername);
	pSql->BindInt64(Param++, pReq->m_MailId);
	int NumUpdated = 0;
	if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
		return false;

	// The worker is called again to mirror a successful primary write to the
	// backup. Only the authoritative primary attempt may decide the claim.
	if(w == Write::NORMAL)
	{
		pRes->m_PrimarySucceeded = true;
		pRes->m_Claimed = NumUpdated == 1;
	}
	return true;
}

bool CAccountsWorker::DeleteMail(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccDeleteMail *>(pData);
	char aSql[256];
	str_copy(aSql,
		"DELETE FROM foxnet_account_mailbox "
		"WHERE Username = ? AND MailId = ?",
		sizeof(aSql));
	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;
	int Param = 1;
	pSql->BindString(Param++, pReq->m_aUsername);
	pSql->BindInt64(Param++, pReq->m_MailId);
	int NumDeleted = 0;
	return pSql->ExecuteUpdate(&NumDeleted, pError, ErrorSize);
}

bool CAccountsWorker::NewMail(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccNewMail *>(pData);
	auto *pRes = dynamic_cast<CAccMailAcknowledge *>(pData->m_pResult.get());
	if(!pReq || !pRes)
	{
		str_copy(pError, "NewMail: bad request/result type", ErrorSize);
		return false;
	}

	// The request is executed on the backup database as well, both before and
	// after the primary write. Only the primary may hand out the MailId: the
	// session caches the mail with that id and every later operation on it
	// (claiming its reward, marking it read, deleting it) looks the row up by
	// id on the primary database.
	const bool Primary = w == Write::NORMAL;

	// The pool retries a failed write. On the primary the next MailId is read
	// and then inserted, so without one atomic unit a connection lost between
	// the two would insert the mail a second time under a fresh id.
	CScopedAccTransaction Transaction(pSql);
	if(Primary && !Transaction.Begin(pError, ErrorSize))
		return false;

	// Acquire next MailId for user
	int64_t MailId = pReq->m_MailId;
	if(MailId == 0 && !Primary)
		MailId = pRes->m_MailId; // decided by the primary write this mirrors
	if(MailId == 0)
	{
		if(!Primary)
			return true; // the primary did not store the mail, nothing to mirror

		char aSelect[128];
		str_copy(aSelect, "SELECT COALESCE(MAX(MailId), 0) + 1 FROM foxnet_account_mailbox WHERE Username = ?", sizeof(aSelect));
		if(!pSql->PrepareStatement(aSelect, pError, ErrorSize))
			return false;
		pSql->BindString(1, pReq->m_aUsername);
		bool End = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
		MailId = End ? 1 : GetInt64OrDefault(pSql, 1);
	}

	char aInsert[512];
	str_copy(aInsert,
		"INSERT INTO foxnet_account_mailbox "
		"(Username, MailId, Subject, Message, Command, CommandName, UsedCommand) "
		"VALUES (?, ?, ?, ?, ?, ?, ?)",
		sizeof(aInsert));
	if(!pSql->PrepareStatement(aInsert, pError, ErrorSize))
		return false;

	int Param = 1;
	pSql->BindString(Param++, pReq->m_aUsername);
	pSql->BindInt64(Param++, MailId);
	pSql->BindString(Param++, pReq->m_aSubject);
	pSql->BindString(Param++, pReq->m_aMessage);
	pSql->BindString(Param++, pReq->m_aCmd);
	pSql->BindString(Param++, pReq->m_aCmdName);
	pSql->BindInt(Param++, pReq->m_UsedCmd); // UsedCommand = false

	int NumInserted = 0;
	if(!pSql->ExecuteUpdate(&NumInserted, pError, ErrorSize))
		return false;

	if(!Primary)
		return true;

	pRes->m_Success = (NumInserted == 1);
	if(!pRes->m_Success)
		return false;

	pRes->m_MailId = MailId;
	str_copy(pRes->m_aUsername, pReq->m_aUsername, sizeof(pRes->m_aUsername));
	pRes->m_MailBox.Clear();
	{
		CMailBox::CMail M{};
		M.m_MailId = MailId;
		str_copy(M.m_aSubject, pReq->m_aSubject, sizeof(M.m_aSubject));
		str_copy(M.m_aMessage, pReq->m_aMessage, sizeof(M.m_aMessage));
		str_copy(M.m_aCmd, pReq->m_aCmd, sizeof(M.m_aCmd));
		str_copy(M.m_aCmdName, pReq->m_aCmdName, sizeof(M.m_aCmdName));
		M.m_UsedCmd = false;
		M.m_Unread = true;
		pRes->m_MailBox.m_vMails.push_back(M);
	}

	if(Primary && !Transaction.Commit(pError, ErrorSize))
		return false;

	// The connection pool completes the result once every database has been
	// visited. Completing it here would hand the session a mail that the
	// primary database has not written yet.
	return true;
}

bool CAccountsWorker::NewGlobalMail(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccBulkNewMail *>(pData);
	auto *pRes = dynamic_cast<CBulkMailResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
	{
		str_copy(pError, "BulkNewMail: bad request/result type", ErrorSize);
		return false;
	}

	// Build conditional WHERE parts
	char aWhere[256];
	aWhere[0] = '\0';
	str_append(aWhere, "WHERE 1=1", sizeof(aWhere));
	if(!pReq->m_IncludeDisabled)
		str_append(aWhere, " AND Disabled = 0", sizeof(aWhere));
	if(pReq->m_OnlyLoggedIn)
		str_append(aWhere, " AND LoggedIn = 1", sizeof(aWhere));
	if(pReq->m_MinLevel >= 0)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), " AND Level >= %d", pReq->m_MinLevel);
		str_append(aWhere, aBuf, sizeof(aWhere));
	}

	// We rely on column defaults for Unread (1) and UsedCommand (0).
	// MailId: correlated subquery gives next id per Username.
	char aSql[1024];
	str_format(aSql, sizeof(aSql),
		"INSERT INTO foxnet_account_mailbox (Username, MailId, Subject, Message, Command, CommandName) "
		"SELECT a.Username, "
		"       COALESCE((SELECT MAX(m.MailId)+1 FROM foxnet_account_mailbox m WHERE m.Username = a.Username), 1), "
		"       ?, ?, ?, ? "
		"FROM foxnet_accounts a %s",
		aWhere);

	if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
		return false;

	int Param = 1;
	pSql->BindString(Param++, pReq->m_aSubject);
	pSql->BindString(Param++, pReq->m_aMessage);
	pSql->BindString(Param++, pReq->m_aCmd);
	pSql->BindString(Param++, pReq->m_aCmdName);

	int NumInserted = 0;
	if(!pSql->ExecuteUpdate(&NumInserted, pError, ErrorSize))
		return false;

	// Only the primary write may announce the mails. The backup pass runs
	// before the primary one, so acting on its result would tell the sessions
	// to refresh their mailbox from a database that has no such mail yet.
	if(w != Write::NORMAL)
		return true;

	pRes->m_Inserted = NumInserted;
	pRes->m_Success = NumInserted > 0;
	str_copy(pRes->m_aSubject, pReq->m_aSubject, sizeof(pRes->m_aSubject));
	return true;
}

bool CAccountsWorker::ClaimDailyReward(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccClaimDailyReward *>(pData);
	auto *pRes = dynamic_cast<CAccDailyRewardResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
	{
		str_copy(pError, "ClaimDailyReward: bad request/result type", ErrorSize);
		return false;
	}

	// A backup-only claim cannot guarantee exclusivity against servers that
	// still use the primary database, so it must never grant a reward.
	// NORMAL_SUCCEEDED is the pool replaying this on the backup; the period
	// guard already makes that a no-op, so skip the queries outright.
	if(w == Write::NORMAL_FAILED || w == Write::NORMAL_SUCCEEDED)
		return true;

	const bool Primary = w == Write::NORMAL;

	char aSelect[160];
	str_copy(aSelect, "SELECT Period, Count FROM foxnet_account_timers WHERE Username = ? AND `Key` = ?", sizeof(aSelect));
	if(!pSql->PrepareStatement(aSelect, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_aUsername);
	pSql->BindString(2, DAILY_REWARD_TIMER_KEY);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;

	const bool Exists = !End;
	int64_t Period = -1;
	int64_t Count = 0;
	if(Exists)
	{
		Period = GetInt64OrDefault(pSql, 1, -1);
		Count = GetInt64OrDefault(pSql, 2);
		if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
			return false;
	}

	// Already claimed this period, or the clock moved backwards past it
	if(Exists && Period >= pReq->m_Period)
	{
		if(Primary)
		{
			pRes->m_PrimarySucceeded = true;
			pRes->m_Claimed = false;
			pRes->m_Streak = Count;
		}
		return true;
	}

	// A gap of more than one period breaks the streak
	const int64_t Streak = (Exists && Period == pReq->m_Period - 1) ? Count + 1 : 1;

	if(Exists)
	{
		char aUpdate[256];
		str_copy(aUpdate,
			"UPDATE foxnet_account_timers "
			"SET Period = ?, Count = ?, UpdatedAt = ? "
			"WHERE Username = ? AND `Key` = ? AND Period < ?",
			sizeof(aUpdate));
		if(!pSql->PrepareStatement(aUpdate, pError, ErrorSize))
			return false;

		int Param = 1;
		pSql->BindInt64(Param++, pReq->m_Period);
		pSql->BindInt64(Param++, Streak);
		pSql->BindInt64(Param++, pReq->m_Now);
		pSql->BindString(Param++, pReq->m_aUsername);
		pSql->BindString(Param++, DAILY_REWARD_TIMER_KEY);
		pSql->BindInt64(Param++, pReq->m_Period);

		int NumUpdated = 0;
		if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
			return false;

		if(Primary)
		{
			pRes->m_PrimarySucceeded = true;
			pRes->m_Claimed = NumUpdated == 1;
			pRes->m_Streak = Streak;
		}
		return true;
	}

	char aInsert[256];
	str_copy(aInsert,
		"INSERT INTO foxnet_account_timers (Username, `Key`, ExpiresAt, Period, Count, UpdatedAt) "
		"VALUES (?, ?, 0, ?, ?, ?)",
		sizeof(aInsert));
	if(!pSql->PrepareStatement(aInsert, pError, ErrorSize))
		return false;

	int Param = 1;
	pSql->BindString(Param++, pReq->m_aUsername);
	pSql->BindString(Param++, DAILY_REWARD_TIMER_KEY);
	pSql->BindInt64(Param++, pReq->m_Period);
	pSql->BindInt64(Param++, Streak);
	pSql->BindInt64(Param++, pReq->m_Now);

	int NumInserted = 0;
	if(!pSql->ExecuteUpdate(&NumInserted, pError, ErrorSize))
		return false;

	if(Primary)
	{
		pRes->m_PrimarySucceeded = true;
		pRes->m_Claimed = NumInserted == 1;
		pRes->m_Streak = Streak;
	}
	return true;
}

bool CAccountsWorker::MoneyDelta(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccMoneyDelta *>(pData);
	auto *pRes = dynamic_cast<CAccMoneyDeltaResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
	{
		str_copy(pError, "MoneyDelta: bad request/result type", ErrorSize);
		return false;
	}

	// NORMAL_FAILED means only the backup would get this, and it would be
	// applied again when the primary recovers. NORMAL_SUCCEEDED is the pool
	// replaying the job on the backup after the primary took it, which for a
	// relative update would double it there. Neither may move money.
	if(w == Write::NORMAL_FAILED || w == Write::NORMAL_SUCCEEDED)
		return true;

	const bool Primary = w == Write::NORMAL;

	// The pool retries a failed write, and this is a relative update, so the
	// change and the read-back have to be one atomic unit. Without this, a
	// connection lost between them would apply the delta twice.
	CScopedAccTransaction Transaction(pSql);
	if(!Transaction.Begin(pError, ErrorSize))
		return false;

	char aSql[256];
	int Param = 1;
	if(pReq->m_Absolute)
	{
		str_copy(aSql, "UPDATE foxnet_accounts SET Money = ? WHERE Username = ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt64(Param++, pReq->m_Delta);
		pSql->BindString(Param++, pReq->m_aUsername);
	}
	else if(pReq->m_RequireFunds)
	{
		// Checking and debiting in one statement leaves no window to overdraw in
		str_copy(aSql, "UPDATE foxnet_accounts SET Money = Money + ? WHERE Username = ? AND Money >= ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt64(Param++, pReq->m_Delta);
		pSql->BindString(Param++, pReq->m_aUsername);
		pSql->BindInt64(Param++, -pReq->m_Delta);
	}
	else if(pReq->m_Delta < 0)
	{
		// CASE rather than GREATEST/MAX, which differ between MySQL and SQLite
		str_copy(aSql, "UPDATE foxnet_accounts SET Money = CASE WHEN Money + ? < 0 THEN 0 ELSE Money + ? END WHERE Username = ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt64(Param++, pReq->m_Delta);
		pSql->BindInt64(Param++, pReq->m_Delta);
		pSql->BindString(Param++, pReq->m_aUsername);
	}
	else
	{
		// Refuse rather than wrap around when the balance cannot hold the credit
		str_copy(aSql, "UPDATE foxnet_accounts SET Money = Money + ? WHERE Username = ? AND Money <= ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt64(Param++, pReq->m_Delta);
		pSql->BindString(Param++, pReq->m_aUsername);
		pSql->BindInt64(Param++, std::numeric_limits<int64_t>::max() - pReq->m_Delta);
	}

	int NumUpdated = 0;
	if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
		return false;

	if(!Primary)
		return Transaction.Commit(pError, ErrorSize);

	pRes->m_Applied = NumUpdated == 1;

	// Hand the authoritative balance back, so the session stops guessing
	char aSelect[128];
	str_copy(aSelect, "SELECT Money FROM foxnet_accounts WHERE Username = ?", sizeof(aSelect));
	if(!pSql->PrepareStatement(aSelect, pError, ErrorSize))
		return false;
	pSql->BindString(1, pReq->m_aUsername);

	bool End = true;
	if(!pSql->Step(&End, pError, ErrorSize))
		return false;
	if(!End)
	{
		pRes->m_Money = GetInt64OrDefault(pSql, 1);
		if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
			return false;
	}

	if(!Transaction.Commit(pError, ErrorSize))
		return false;

	// Only reported once the change is committed, so a caller never acts on a
	// balance that got rolled back.
	pRes->m_PrimarySucceeded = true;
	return true;
}

bool CAccountsWorker::Pay(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize)
{
	const auto *pReq = dynamic_cast<const CAccPay *>(pData);
	auto *pRes = dynamic_cast<CAccPayResult *>(pData->m_pResult.get());
	if(!pReq || !pRes)
	{
		str_copy(pError, "Pay: bad request/result type", ErrorSize);
		return false;
	}

	// NORMAL_FAILED cannot be exclusive against servers still using the
	// primary, and NORMAL_SUCCEEDED is the pool replaying this on the backup
	// after the primary took it, which would transfer twice there.
	if(w == Write::NORMAL_FAILED || w == Write::NORMAL_SUCCEEDED)
		return true;

	const bool Primary = w == Write::NORMAL;
	auto Report = [&](CAccPayResult::EStatus Status) {
		if(!Primary)
			return;
		pRes->m_PrimarySucceeded = true;
		pRes->m_Status = Status;
	};

	CScopedAccTransaction Transaction(pSql);
	if(!Transaction.Begin(pError, ErrorSize))
		return false;

	// Cooldown, kept on the account so rejoining does not clear it
	int64_t CooldownEndsAt = 0;
	bool HasTimerRow = false;
	{
		char aSql[160];
		str_copy(aSql, "SELECT ExpiresAt FROM foxnet_account_timers WHERE Username = ? AND `Key` = ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindString(1, pReq->m_aPayer);
		pSql->BindString(2, PAY_TIMER_KEY);
		bool End = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
		if(!End)
		{
			HasTimerRow = true;
			CooldownEndsAt = GetInt64OrDefault(pSql, 1);
			if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
				return false;
		}
	}

	if(CooldownEndsAt > pReq->m_Now)
	{
		if(Primary)
			pRes->m_CooldownEndsAt = CooldownEndsAt;
		Report(CAccPayResult::EStatus::ON_COOLDOWN);
		return true; // transaction rolls back, nothing was changed
	}

	// Debit the payer. Checking and subtracting in one statement leaves no
	// window where two transfers each see the same balance.
	{
		char aSql[192];
		str_copy(aSql, "UPDATE foxnet_accounts SET Money = Money - ? WHERE Username = ? AND Money >= ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt64(1, pReq->m_Amount);
		pSql->BindString(2, pReq->m_aPayer);
		pSql->BindInt64(3, pReq->m_Amount);
		int NumUpdated = 0;
		if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
			return false;
		if(NumUpdated != 1)
		{
			Report(CAccPayResult::EStatus::INSUFFICIENT_FUNDS);
			return true;
		}
	}

	// Credit the receiver, refusing rather than wrapping an overfull balance
	{
		char aSql[192];
		str_copy(aSql, "UPDATE foxnet_accounts SET Money = Money + ? WHERE Username = ? AND Money <= ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt64(1, pReq->m_Amount);
		pSql->BindString(2, pReq->m_aReceiver);
		pSql->BindInt64(3, std::numeric_limits<int64_t>::max() - pReq->m_Amount);
		int NumUpdated = 0;
		if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
			return false;
		if(NumUpdated != 1)
		{
			Report(CAccPayResult::EStatus::NO_RECEIVER);
			return true;
		}
	}

	// Arm the cooldown
	{
		char aSql[256];
		if(HasTimerRow)
			str_copy(aSql, "UPDATE foxnet_account_timers SET ExpiresAt = ?, UpdatedAt = ? WHERE Username = ? AND `Key` = ?", sizeof(aSql));
		else
			str_copy(aSql, "INSERT INTO foxnet_account_timers (Username, `Key`, ExpiresAt, Period, Count, UpdatedAt) VALUES (?, ?, ?, -1, 0, ?)", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		const int64_t EndsAt = pReq->m_Now + pReq->m_Cooldown;
		int Param = 1;
		if(HasTimerRow)
		{
			pSql->BindInt64(Param++, EndsAt);
			pSql->BindInt64(Param++, pReq->m_Now);
			pSql->BindString(Param++, pReq->m_aPayer);
			pSql->BindString(Param++, PAY_TIMER_KEY);
		}
		else
		{
			pSql->BindString(Param++, pReq->m_aPayer);
			pSql->BindString(Param++, PAY_TIMER_KEY);
			pSql->BindInt64(Param++, EndsAt);
			pSql->BindInt64(Param++, pReq->m_Now);
		}
		int NumUpdated = 0;
		if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
			return false;
	}

	// Audit trail, so a transfer can be traced and reversed afterwards
	{
		char aSql[320];
		str_copy(aSql,
			"INSERT INTO foxnet_money_log (Id, Payer, Receiver, Amount, Timestamp) "
			"SELECT COALESCE(MAX(Id), 0) + 1, ?, ?, ?, ? FROM foxnet_money_log",
			sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindString(1, pReq->m_aPayer);
		pSql->BindString(2, pReq->m_aReceiver);
		pSql->BindInt64(3, pReq->m_Amount);
		pSql->BindInt64(4, pReq->m_Now);
		int NumUpdated = 0;
		if(!pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
			return false;
	}

	// Payer's balance after the transfer, for the session cache
	if(Primary)
	{
		char aSql[128];
		str_copy(aSql, "SELECT Money FROM foxnet_accounts WHERE Username = ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindString(1, pReq->m_aPayer);
		bool End = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
		if(!End)
		{
			pRes->m_PayerMoney = GetInt64OrDefault(pSql, 1);
			if(!ConsumeRemainingRows(pSql, &End, pError, ErrorSize))
				return false;
		}
	}

	if(!Transaction.Commit(pError, ErrorSize))
		return false;

	Report(CAccPayResult::EStatus::OK);
	return true;
}
