#include "connection.h"

#include <base/str.h>

IDbConnection::IDbConnection(const char *pPrefix)
{
	str_copy(m_aPrefix, pPrefix);
}

std::optional<float> IDbConnection::GetOptionalFloat(int Col)
{
	if(IsNull(Col))
		return std::nullopt;
	return GetFloat(Col);
}

std::optional<int> IDbConnection::GetOptionalInt(int Col)
{
	if(IsNull(Col))
		return std::nullopt;
	return GetInt(Col);
}

std::optional<int64_t> IDbConnection::GetOptionalInt64(int Col)
{
	if(IsNull(Col))
		return std::nullopt;
	return GetInt64(Col);
}

void IDbConnection::FormatCreateRace(char *aBuf, unsigned int BufferSize, bool Backup) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_race%s ("
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Name VARCHAR(%d) COLLATE %s NOT NULL, "
		"  Timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
		"  Time FLOAT DEFAULT 0, "
		"  Server CHAR(4), "
		"  cp1 FLOAT DEFAULT 0, cp2 FLOAT DEFAULT 0, cp3 FLOAT DEFAULT 0, "
		"  cp4 FLOAT DEFAULT 0, cp5 FLOAT DEFAULT 0, cp6 FLOAT DEFAULT 0, "
		"  cp7 FLOAT DEFAULT 0, cp8 FLOAT DEFAULT 0, cp9 FLOAT DEFAULT 0, "
		"  cp10 FLOAT DEFAULT 0, cp11 FLOAT DEFAULT 0, cp12 FLOAT DEFAULT 0, "
		"  cp13 FLOAT DEFAULT 0, cp14 FLOAT DEFAULT 0, cp15 FLOAT DEFAULT 0, "
		"  cp16 FLOAT DEFAULT 0, cp17 FLOAT DEFAULT 0, cp18 FLOAT DEFAULT 0, "
		"  cp19 FLOAT DEFAULT 0, cp20 FLOAT DEFAULT 0, cp21 FLOAT DEFAULT 0, "
		"  cp22 FLOAT DEFAULT 0, cp23 FLOAT DEFAULT 0, cp24 FLOAT DEFAULT 0, "
		"  cp25 FLOAT DEFAULT 0, "
		"  GameId VARCHAR(64), "
		"  DDNet7 BOOL DEFAULT FALSE, "
		"  PRIMARY KEY (Map, Name, Time, Timestamp, Server)"
		")",
		GetPrefix(), Backup ? "_backup" : "",
		BinaryCollate(), MAX_NAME_LENGTH_SQL, BinaryCollate());
}

void IDbConnection::FormatCreateTeamrace(char *aBuf, unsigned int BufferSize, const char *pIdType, bool Backup) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_teamrace%s ("
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Name VARCHAR(%d) COLLATE %s NOT NULL, "
		"  Timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
		"  Time FLOAT DEFAULT 0, "
		"  ID %s NOT NULL, " // VARBINARY(16) for MySQL and BLOB for SQLite
		"  GameId VARCHAR(64), "
		"  DDNet7 BOOL DEFAULT FALSE, "
		"  PRIMARY KEY (Id, Name)"
		")",
		GetPrefix(), Backup ? "_backup" : "",
		BinaryCollate(), MAX_NAME_LENGTH_SQL, BinaryCollate(), pIdType);
}

void IDbConnection::FormatCreateMaps(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_maps ("
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Server VARCHAR(32) COLLATE %s NOT NULL, "
		"  Mapper VARCHAR(128) COLLATE %s NOT NULL, "
		"  Points INT DEFAULT 0, "
		"  Stars INT DEFAULT 0, "
		"  Size VARCHAR(8) DEFAULT NULL, "
		"  Timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
		"  PRIMARY KEY (Map)"
		")",
		GetPrefix(), BinaryCollate(), BinaryCollate(), BinaryCollate());
}

void IDbConnection::FormatCreateSaves(char *aBuf, unsigned int BufferSize, bool Backup) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_saves%s ("
		"  Savegame TEXT COLLATE %s NOT NULL, "
		"  Map VARCHAR(128) COLLATE %s NOT NULL, "
		"  Code VARCHAR(128) COLLATE %s NOT NULL, "
		"  Timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
		"  Server CHAR(4), "
		"  DDNet7 BOOL DEFAULT FALSE, "
		"  SaveId VARCHAR(36) DEFAULT NULL, "
		"  PRIMARY KEY (Map, Code)"
		")",
		GetPrefix(), Backup ? "_backup" : "",
		BinaryCollate(), BinaryCollate(), BinaryCollate());
}

void IDbConnection::FormatCreatePoints(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS %s_points ("
		"  Name VARCHAR(%d) COLLATE %s NOT NULL, "
		"  Points INT DEFAULT 0, "
		"  PRIMARY KEY (Name)"
		")",
		GetPrefix(), MAX_NAME_LENGTH_SQL, BinaryCollate());
}
// <FoxNet
void IDbConnection::FormatCreateAccounts(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS foxnet_accounts ("
		"  Version INTEGER NOT NULL DEFAULT 6, "
		"  Username VARCHAR(32) COLLATE %s NOT NULL, "
		"  Password VARCHAR(128) COLLATE %s NOT NULL, "
		"  RegisterDate %s NOT NULL, "
		"  PlayerName VARCHAR(%d) COLLATE %s DEFAULT '', "
		"  LastPlayerName VARCHAR(%d) COLLATE %s DEFAULT '', "
		"  CurrentIP VARCHAR(45) COLLATE %s DEFAULT '', "
		"  LastIP VARCHAR(45) COLLATE %s DEFAULT '', "
		"  LoggedIn INTEGER DEFAULT 0, "
		"  LastLogin %s DEFAULT 0, "
		"  Port INTEGER DEFAULT 0, "
		"  ClientId INTEGER DEFAULT -1, "
		"  ServerInstance VARCHAR(16) COLLATE %s DEFAULT '', "
		"  Playtime %s DEFAULT 0, "
		"  Deaths %s DEFAULT 0, "
		"  Kills %s DEFAULT 0, "
		"  Level %s DEFAULT 0, "
		"  XP INTEGER DEFAULT 0, "
		"  Money %s DEFAULT 0, "
		"  Disabled INTEGER DEFAULT 0, "
		"  PRIMARY KEY (Username)"
		")",
		BinaryCollate(),
		BinaryCollate(),
		Int64Type(),
		MAX_NAME_LENGTH_SQL, BinaryCollate(),
		MAX_NAME_LENGTH_SQL, BinaryCollate(),
		BinaryCollate(),
		BinaryCollate(),
		Int64Type(),
		BinaryCollate(),
		Int64Type(),
		Int64Type(),
		Int64Type(),
		Int64Type(),
		Int64Type());
}

void IDbConnection::FormatCreateAccountRegistrations(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS foxnet_account_registrations ("
		"  IP VARCHAR(45) COLLATE %s NOT NULL, "
		"  Slot INTEGER NOT NULL, "
		"  Username VARCHAR(32) COLLATE %s NOT NULL, "
		"  PRIMARY KEY (Username), "
		"  UNIQUE (IP, Slot)"
		")",
		BinaryCollate(),
		BinaryCollate());
}

void IDbConnection::FormatCreateAccountInventory(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS foxnet_account_inventory ("
		"  Username VARCHAR(32) COLLATE %s NOT NULL,"
		"  ItemName VARCHAR(64) COLLATE %s NOT NULL,"
		"  Quantity INTEGER NOT NULL DEFAULT 1,"
		"  AcquiredAt %s NOT NULL,"
		"  ExpiresAt %s NOT NULL,"
		"  Meta TEXT COLLATE %s DEFAULT '',"
		"  Value INTEGER NOT NULL DEFAULT 0,"
		"  PRIMARY KEY (Username, ItemName)"
		")",
		BinaryCollate(),
		BinaryCollate(),
		Int64Type(),
		Int64Type(),
		BinaryCollate());
}

void IDbConnection::FormatCreateAccountMailbox(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS foxnet_account_mailbox ("
		"  Username VARCHAR(32) COLLATE %s NOT NULL,"
		"  MailId %s NOT NULL,"
		"  Subject VARCHAR(64) COLLATE %s NOT NULL,"
		"  Message TEXT COLLATE %s NOT NULL,"
		"  Command VARCHAR(512) COLLATE %s DEFAULT ''," // Console command, will use %d for ClientId inside the command
		"  CommandName VARCHAR(512) COLLATE %s DEFAULT '',"
		"  UsedCommand INTEGER NOT NULL DEFAULT 0,"
		"  Unread INTEGER NOT NULL DEFAULT 1,"
		"  PRIMARY KEY (Username, MailId)"
		")",
		BinaryCollate(),
		Int64Type(),
		BinaryCollate(),
		BinaryCollate(),
		BinaryCollate(),
		BinaryCollate());
}

void IDbConnection::FormatCreateAccountConfig(char *aBuf, unsigned int BufferSize) const
{
	str_format(aBuf, BufferSize,
		"CREATE TABLE IF NOT EXISTS foxnet_account_config ("
		"  Username VARCHAR(32) COLLATE %s NOT NULL,"
		"  `Key` VARCHAR(64) COLLATE %s NOT NULL,"
		"  Value TEXT COLLATE %s NOT NULL,"
		"  Type VARCHAR(8) COLLATE %s NOT NULL DEFAULT 'str'," // 'str','int','float','bool','json'
		"  UpdatedAt %s NOT NULL DEFAULT 0,"
		"  PRIMARY KEY (Username, `Key`)"
		")",
		BinaryCollate(), BinaryCollate(), BinaryCollate(), BinaryCollate(), Int64Type());
}
// FoxNet>
