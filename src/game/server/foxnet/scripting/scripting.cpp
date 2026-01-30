#include "scripting.h"

#include "impl.h"

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/gamecontext.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <variant>
#include <base/log.h>
#include <base/system.h>
#include <base/str.h>
#include <game/server/entities/character.h>
#include <base/vmath.h>

static CScriptingCtx g_ScriptingCtx; // persistent VM
static bool g_ScriptingInitialized = false;

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

	CScriptingCtx *m_pScriptingCtx;

	void InitGameServer(CGameContext *pGameServer, CScriptingCtx *pSharedCtx)
	{
		m_pGameServer = pGameServer;
		m_pScriptingCtx = pSharedCtx;
	}

public:
	CScriptingCtx::Any ClientInfo(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		if(!std::holds_alternative<int>(Arg))
			return nullptr;
		const int ClientId = std::get<int>(Arg);

		if(Str == "exists")
			return CheckClientId(ClientId) && m_pGameServer->PlayerExists(ClientId);

		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return nullptr;

		if(Str == "player_exists")
			return m_pGameServer->PlayerExists(ClientId);
		if(Str == "acc_logged_in")
			return m_pGameServer->m_aAccounts[ClientId].m_LoggedIn;
		if(Str == "acc_username")
			return m_pGameServer->m_aAccounts[ClientId].m_aUsername;
		if(Str == "acc_deaths")
			return m_pGameServer->m_aAccounts[ClientId].m_Deaths;
		if(Str == "acc_disabled")
			return m_pGameServer->m_aAccounts[ClientId].m_Disabled;
		if(Str == "acc_kills")
			return m_pGameServer->m_aAccounts[ClientId].m_Kills;
		if(Str == "acc_last_login") // unix timestamp
			return m_pGameServer->m_aAccounts[ClientId].m_LastLogin;
		if(Str == "acc_last_name")
			return m_pGameServer->m_aAccounts[ClientId].m_LastName;
		if(Str == "acc_level")
			return m_pGameServer->m_aAccounts[ClientId].m_Level;
		if(Str == "acc_xp")
			return m_pGameServer->m_aAccounts[ClientId].m_XP;
		if(Str == "acc_money")
			return m_pGameServer->m_aAccounts[ClientId].m_Money;
		if(Str == "acc_playtime")
			return m_pGameServer->m_aAccounts[ClientId].m_Playtime;
		if(Str == "acc_register_date") // unix timestamp
			return m_pGameServer->m_aAccounts[ClientId].m_RegisterDate;
		else if(Str == "char_exists")
			return (bool)m_pGameServer->GetPlayerChar(ClientId);
		else if(Str == "char_alive")
		{
			if(!m_pGameServer->GetPlayerChar(ClientId))
				return false;
			return m_pGameServer->GetPlayerChar(ClientId)->IsAlive();
		}
		else if(Str == "char_grounded")
		{
			if(!m_pGameServer->GetPlayerChar(ClientId))
				return false;
			return m_pGameServer->GetPlayerChar(ClientId)->IsGrounded();
		}
		else if(Str == "char_pos")
		{
			if(!m_pGameServer->GetPlayerChar(ClientId))
				return false;
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
		if(!CheckClientId(ClientId))
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
		std::string LowerStr = Str;
		std::transform(LowerStr.begin(), LowerStr.end(), LowerStr.begin(), ::tolower);
		return LowerStr;
	}
	static CScriptingCtx::Any ToUpper(const std::string &Str)
	{
		std::string UpperStr = Str;
		std::transform(UpperStr.begin(), UpperStr.end(), UpperStr.begin(), ::toupper);
		return UpperStr;
	}
	static CScriptingCtx::Any StrFind(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		std::string SubStr = std::get<std::string>(Arg);
		return str_find(Str.c_str(), SubStr.c_str());
	}
	static CScriptingCtx::Any StrFindNocase(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		std::string SubStr = std::get<std::string>(Arg);
		return str_find_nocase(Str.c_str(), SubStr.c_str());
	}
	static CScriptingCtx::Any StrComp(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		std::string SubStr = std::get<std::string>(Arg);
		return str_comp(Str.c_str(), SubStr.c_str());
	}
	static CScriptingCtx::Any StrCompNocase(const std::string &Str, const CScriptingCtx::Any &Arg)
	{
		std::string SubStr = std::get<std::string>(Arg);
		return str_comp_nocase(Str.c_str(), SubStr.c_str());
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
	CScriptRunner(CGameContext *pGameServer, CScriptingCtx *pSharedCtx)
	{
		InitGameServer(pGameServer, pSharedCtx);
		// Functions are registered once in CScripting::OnConsoleInit
	}

	void Run(const char *pFilename, const char *pArgs)
	{
		m_pScriptingCtx->Run(m_pGameServer->Storage(), pFilename, pArgs);
	}
};

void CScripting::ConExecScript(IConsole::IResult *pResult, void *pUserData)
{
	CScripting *pThis = static_cast<CScripting *>(pUserData);
	pThis->ExecScript(pResult->GetString(0), pResult->GetString(1));
}

void CScripting::ExecScript(const char *pFilename, const char *pArgs)
{
	// Reuse the persistent VM; no per-call rebind/creation
	if(!m_pGameServer)
	{
		log_error(SCRIPTING_IMPL, "GameContext is null, %s %s", pFilename, pArgs);
		return;
	}

	CScriptRunner Runner(m_pGameServer, &g_ScriptingCtx);
	Runner.Run(pFilename, pArgs);
}

void CScripting::OnInit(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
}

void CScripting::OnConsoleInit(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pGameServer->Console()->Register(SCRIPTING_IMPL, "s[file] ?r[args]", CFGFLAG_SERVER, ConExecScript, this, "Execute a " SCRIPTING_IMPL " script");
	RegisterScriptingFunctions();
}

void CScripting::RegisterScriptingFunctions()
{
	if(g_ScriptingInitialized)
		return;

	g_ScriptingCtx.AddFunction("exec", [this](const std::string &Str) {
		m_pGameServer->Console()->ExecuteLine(Str.c_str(), IConsole::CLIENT_ID_UNSPECIFIED);
	});
	g_ScriptingCtx.AddFunction("say", [this](const std::string &Str) {
		m_pGameServer->SendChat(-1, TEAM_ALL, Str.c_str());
	});
	g_ScriptingCtx.AddFunction("system", [this](const std::string &Str) {
		m_pGameServer->Server()->SystemCall(Str.c_str());
	});

	g_ScriptingCtx.AddFunction("say_target", [this](const std::string &Str, const CScriptingCtx::Any &Arg) {
		return CScriptRunner(m_pGameServer, &g_ScriptingCtx).SendChatTarget(Str, Arg);
	});
	g_ScriptingCtx.AddFunction("client_info", [this](const std::string &Str, const CScriptingCtx::Any &Arg) {
		return CScriptRunner(m_pGameServer, &g_ScriptingCtx).ClientInfo(Str, Arg);
	});

	g_ScriptingCtx.AddFunction("log_info", [](const std::string &Str) {
		log_info(SCRIPTING_IMPL, "%s", Str.c_str());
	});

	g_ScriptingCtx.AddFunction("escape_message", [](const std::string &Str, const CScriptingCtx::Any &) {
		return CScriptRunner::EscapeString(Str);
	});
	g_ScriptingCtx.AddFunction("to_lower", [](const std::string &Str, const CScriptingCtx::Any &) {
		return CScriptRunner::ToLower(Str);
	});
	g_ScriptingCtx.AddFunction("to_upper", [](const std::string &Str, const CScriptingCtx::Any &) {
		return CScriptRunner::ToUpper(Str);
	});
	g_ScriptingCtx.AddFunction("str_find", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
		return CScriptRunner::StrFind(Str, Arg);
	});
	g_ScriptingCtx.AddFunction("str_find_nocase", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
		return CScriptRunner::StrFindNocase(Str, Arg);
	});
	g_ScriptingCtx.AddFunction("str_comp", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
		return CScriptRunner::StrComp(Str, Arg);
	});
	g_ScriptingCtx.AddFunction("str_comp_nocase", [](const std::string &Str, const CScriptingCtx::Any &Arg) {
		return CScriptRunner::StrCompNocase(Str, Arg);
	});

	// Return current OS as a string
	g_ScriptingCtx.AddFunction("os", []() {
		return CScriptRunner::GetOs();
	});
	g_ScriptingCtx.AddFunction("port", [this]() {
		return CScriptRunner(m_pGameServer, &g_ScriptingCtx).GetPort();
	});

	g_ScriptingInitialized = true;
}