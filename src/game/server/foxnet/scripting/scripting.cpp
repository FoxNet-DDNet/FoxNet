#include "scripting.h"

#include "impl.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <variant>

static const char *DetectOS()
{
#if defined(_WIN32) || defined(_WIN64)
	return "windows";
#elif defined(__APPLE__) && defined(__MACH__)
	return "macos";
#elif defined(__linux__)
	return "linux";
#elif defined(__FreeBSD__)
	return "freebsd";
#elif defined(__ANDROID__)
	return "android";
#else
	return "unknown";
#endif
}

class CScriptRunner
{
private:
	CGameContext *m_pGameServer;

	CScriptingCtx m_ScriptingCtx;

	void InitGameServer(CGameContext *pGameServer)
	{
		m_pGameServer = pGameServer;
	}

public:
	bool CheckClient(const int ClientId)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;
		if(m_pGameServer->Server()->ClientSlotEmpty(ClientId))
			return false;
		if(!m_pGameServer->PlayerExists(ClientId))
			return false;
		return true;
	}

	CScriptingCtx::Any ClientInfo(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		int ClientId = -1;
		if(std::holds_alternative<int>(Arg))
		{
			ClientId = std::get<int>(Arg);
		}
		else if(std::holds_alternative<std::string>(Arg))
		{
			const std::string &ArgStr = std::get<std::string>(Arg);
			if(!ArgStr.empty() && str_isallnum(ArgStr.c_str()))
			{
				ClientId = std::stoi(ArgStr);
			}
			else
			{
				return nullptr;
			}
		}
		else
		{
			return nullptr;
		}

		if(Str == "exists") // chai script throws if nullptr is returned, this is so you can iterate trough all clients
			return CheckClient(ClientId);

		if(!CheckClient(ClientId))
			return nullptr;

		if(Str == "name")
			return m_pGameServer->Server()->ClientName(ClientId);
		else if(Str == "clan")
			return m_pGameServer->Server()->ClientClan(ClientId);
		else if(Str == "country")
			return m_pGameServer->Server()->ClientCountry(ClientId);
		else if(Str == "auth_level")
			return m_pGameServer->Server()->GetAuthedState(ClientId);
		else if(Str == "ip")
		{
			if(!CheckClient(ClientId))
				return nullptr;

			return m_pGameServer->Server()->ClientAddrString(ClientId, false);
		}
		else if(Str == "client_name")
		{
			if(!CheckClient(ClientId))
				return nullptr;
			return m_pGameServer->Server()->GetCustomClient(ClientId);
		}
		else if(Str == "acc_logged_in")
			return m_pGameServer->m_aAccounts[ClientId].m_LoggedIn;
		else if(Str == "acc_username")
			return m_pGameServer->m_aAccounts[ClientId].m_aUsername;
		else if(Str == "acc_deaths")
			return m_pGameServer->m_aAccounts[ClientId].m_Deaths;
		else if(Str == "acc_disabled")
			return m_pGameServer->m_aAccounts[ClientId].m_Disabled;
		else if(Str == "acc_kills")
			return m_pGameServer->m_aAccounts[ClientId].m_Kills;
		else if(Str == "acc_last_login") // unix timestamp
			return m_pGameServer->m_aAccounts[ClientId].m_LastLogin;
		else if(Str == "acc_last_name")
			return m_pGameServer->m_aAccounts[ClientId].m_LastName;
		else if(Str == "acc_level")
			return m_pGameServer->m_aAccounts[ClientId].m_Level;
		else if(Str == "acc_xp")
			return m_pGameServer->m_aAccounts[ClientId].m_XP;
		else if(Str == "acc_money")
			return m_pGameServer->m_aAccounts[ClientId].m_Money;
		else if(Str == "acc_playtime")
			return m_pGameServer->m_aAccounts[ClientId].m_Playtime;
		else if(Str == "acc_register_date") // unix timestamp
			return m_pGameServer->m_aAccounts[ClientId].m_RegisterDate;
		else if(Str == "char_exists")
		{
			if(!CheckClient(ClientId))
				return false;

			return (bool)m_pGameServer->GetPlayerChar(ClientId);
		}
		else if(Str == "char_alive")
		{
			if(!CheckClient(ClientId))
				return nullptr;
			return m_pGameServer->GetPlayerChar(ClientId)->IsAlive();
		}
		else if(Str == "char_grounded")
		{
			if(!CheckClient(ClientId))
				return nullptr;
			return m_pGameServer->GetPlayerChar(ClientId)->IsGrounded();
		}
		else if(Str == "char_pos")
		{
			if(!CheckClient(ClientId))
				return nullptr;
			const vec2 Pos = m_pGameServer->GetPlayerChar(ClientId)->GetPos();
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "%.3f %.3f", Pos.x, Pos.y);
			return std::string(aBuf);
		}

		throw std::string("No state with name '") + Str + std::string("'");
	}

	CScriptingCtx::Any SendChatTarget(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		std::string UpperStr = Str;
		std::transform(UpperStr.begin(), UpperStr.end(), UpperStr.begin(), ::toupper);

		if(!std::holds_alternative<int>(Arg))
			return false;
		const int ClientId = std::get<int>(Arg);
		if(!CheckClient(ClientId))
			return false;
		if(!m_pGameServer->PlayerExists(ClientId))
			return false;

		m_pGameServer->SendChatTarget(ClientId, Str.c_str());
		return true;
	}

	static CScriptingCtx::Any EscapeString(const std::string &Str)
	{
		return EscapeMessage(Str.c_str());
	}
	static CScriptingCtx::Any ToLower(const std::string &Str)
	{
		if(Str.empty())
			return nullptr;

		std::string LowerStr = Str;
		std::transform(LowerStr.begin(), LowerStr.end(), LowerStr.begin(), ::tolower);
		return LowerStr;
	}
	static CScriptingCtx::Any ToUpper(const std::string &Str)
	{
		if(Str.empty())
			return nullptr;

		std::string UpperStr = Str;
		std::transform(UpperStr.begin(), UpperStr.end(), UpperStr.begin(), ::toupper);
		return UpperStr;
	}
	static CScriptingCtx::Any StrFind(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		if(!std::holds_alternative<std::string>(Arg))
			return nullptr;

		std::string SubStr = std::get<std::string>(Arg);
		if(Str.empty() || SubStr.empty())
			return nullptr;

		const char *pFound = str_find(Str.c_str(), SubStr.c_str());
		if(!pFound)
			return "";

		return pFound;
	}
	static CScriptingCtx::Any StrFindNocase(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		if(!std::holds_alternative<std::string>(Arg))
			return nullptr;

		std::string SubStr = std::get<std::string>(Arg);
		if(Str.empty() || SubStr.empty())
			return nullptr;

		const char *pFound = str_find_nocase(Str.c_str(), SubStr.c_str());
		if(!pFound)
			return "";

		return pFound;
	}
	static CScriptingCtx::Any StrComp(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		if(!std::holds_alternative<std::string>(Arg))
			return nullptr;

		std::string SubStr = std::get<std::string>(Arg);
		if(Str.empty() || SubStr.empty())
			return nullptr;

		return str_comp(Str.c_str(), SubStr.c_str());
	}
	static CScriptingCtx::Any StrCompNocase(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		if(!std::holds_alternative<std::string>(Arg))
			return nullptr;

		std::string SubStr = std::get<std::string>(Arg);
		if(Str.empty() || SubStr.empty())
			return nullptr;
		return str_comp_nocase(Str.c_str(), SubStr.c_str());
	}
	static CScriptingCtx::Any ParseArgument(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		if(!std::holds_alternative<int>(Arg))
			return std::string();

		const int RequestedIndex = std::get<int>(Arg);
		if(RequestedIndex < 0)
			return std::string();

		if(Str.empty())
			return std::string();

		const char *pArg = GetParsedArgument(Str.c_str(), RequestedIndex, false);
		if(!pArg)
			return std::string();

		return std::string(pArg);
	}

	static CScriptingCtx::Any GetOs()
	{
		return std::string(DetectOS());
	}
	CScriptingCtx::Any GetPort()
	{
		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d", m_pGameServer->Server()->Port());
		return std::string(aBuf);
	}

	// ToDo: add a getter for all g_Config. server variables

public:
	CScriptRunner(CGameContext *pGameServer)
	{
		InitGameServer(pGameServer);

		m_ScriptingCtx.AddFunction("exec", [this](const std::string &Str) {
			m_pGameServer->Console()->ExecuteLine(Str.c_str(), IConsole::CLIENT_ID_SCRIPTING);
		});
		m_ScriptingCtx.AddFunction("say", [this](const std::string &Str) {
			m_pGameServer->SendChat(-1, TEAM_ALL, Str.c_str());
		});
		m_ScriptingCtx.AddFunction("system", [this](const std::string &Str) {
			m_pGameServer->Server()->SystemCall(Str.c_str());
		});

		m_ScriptingCtx.AddFunction("say_target", [this](const std::string &Str, const CScriptingCtx::Any &Arg) {
			return SendChatTarget(Str, Arg);
		});
		m_ScriptingCtx.AddFunction("client", [this](const std::string &Str, const CScriptingCtx::Any &Arg) {
			return ClientInfo(Str, Arg);
		});

		m_ScriptingCtx.AddFunction("log_info", [](const std::string &Str) {
			log_info(SCRIPTING_IMPL, "%s", Str.c_str());
		});
		m_ScriptingCtx.AddFunction("print", [](const std::string &Str) {
			log_info(SCRIPTING_IMPL, "%s", Str.c_str());
		});

		m_ScriptingCtx.AddFunction("escape_message", [](const std::string &Str, const CScriptingCtx::Any &) {
			return EscapeString(Str);
		});
		m_ScriptingCtx.AddFunction("to_lower", [](const std::string &Str, const CScriptingCtx::Any &) {
			return ToLower(Str);
		});
		m_ScriptingCtx.AddFunction("to_upper", [](const std::string &Str, const CScriptingCtx::Any &) {
			return ToUpper(Str);
		});
		m_ScriptingCtx.AddFunction("str_find", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
			return StrFind(Str, Arg);
		});
		m_ScriptingCtx.AddFunction("str_find_nocase", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
			return StrFindNocase(Str, Arg);
		});
		m_ScriptingCtx.AddFunction("str_comp", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
			return StrComp(Str, Arg);
		});
		m_ScriptingCtx.AddFunction("str_comp_nocase", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
			return StrCompNocase(Str, Arg);
		});

		m_ScriptingCtx.AddFunction("parse_argument", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
			return ParseArgument(Str, Arg);
		});

		// Return current OS as a string
		m_ScriptingCtx.AddFunction("os", []() {
			return GetOs();
		});
		m_ScriptingCtx.AddFunction("port", [this]() {
			return GetPort();
		});
	}

	void Run(const char *pFilename, const char *pArgs)
	{
		m_ScriptingCtx.Run(m_pGameServer->Storage(), pFilename, pArgs);
	}
};

CScripting::~CScripting() = default;

void CScriptRunnerDeleter::operator()(CScriptRunner *pRunner) const
{
	delete pRunner;
}
void CScripting::ConExecScript(IConsole::IResult *pResult, void *pUserData)
{
	CScripting *pThis = static_cast<CScripting *>(pUserData);
	pThis->ExecScript(pResult->GetString(0), pResult->GetString(1));
}

void CScripting::ExecScript(const char *pFilename, const char *pArgs)
{
	if(!m_pGameServer)
	{
		log_error(SCRIPTING_IMPL, "GameContext is null, %s %s", pFilename, pArgs);
		return;
	}

	if(!m_pRunner)
	{
		m_pRunner.reset(new CScriptRunner(m_pGameServer));
	}

	m_pRunner->Run(pFilename, pArgs);
}

void CScripting::OnInit(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	if(!m_pRunner)
		m_pRunner.reset(new CScriptRunner(m_pGameServer));
}

void CScripting::OnConsoleInit(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	if(!m_pRunner)
		m_pRunner.reset(new CScriptRunner(m_pGameServer));
	m_pGameServer->Console()->Register(SCRIPTING_IMPL, "s[file] ?r[args]", CFGFLAG_SERVER, ConExecScript, this, "Execute a " SCRIPTING_IMPL " script");
}