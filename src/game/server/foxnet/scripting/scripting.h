#ifndef GAME_SERVER_FOXNET_SCRIPTING_SCRIPTING_H
#define GAME_SERVER_FOXNET_SCRIPTING_SCRIPTING_H

#include <engine/console.h>

#include <memory>

class CGameContext;
class CScriptRunner;

struct CScriptRunnerDeleter
{
	void operator()(CScriptRunner *pRunner) const;
};

class CScripting
{
	static void ConExecScript(IConsole::IResult *pResult, void *pUserData);

	CGameContext *m_pGameServer = nullptr;
	std::unique_ptr<CScriptRunner, CScriptRunnerDeleter> m_pRunner;

public:
	~CScripting();
	void SetGameServer(CGameContext *pGameServer);
	void ExecScript(const char *pFilename, const char *pArgs);
	void OnInit(CGameContext *pGameServer);
	void OnConsoleInit(CGameContext *pGameServer);
};

#endif // GAME_SERVER_FOXNET_SCRIPTING_SCRIPTING_H