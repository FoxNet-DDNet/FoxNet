#ifndef GAME_SERVER_FOXNET_SCRIPTING_SCRIPTING_H
#define GAME_SERVER_FOXNET_SCRIPTING_SCRIPTING_H

#include <engine/console.h>

class CGameContext;

class CScripting
{
	static void ConExecScript(IConsole::IResult *pResult, void *pUserData);

	CGameContext *m_pGameServer = nullptr;

	void RegisterScriptingFunctions();

public:
	void SetGameServer(CGameContext *pGameServer);
	void ExecScript(const char *pFilename, const char *pArgs);
	void OnInit(CGameContext *pGameServer);
	void OnConsoleInit(CGameContext *pGameServer);
};

#endif // GAME_SERVER_FOXNET_SCRIPTING_SCRIPTING_H
