#ifndef GAME_SERVER_FOXNET_COMPONENTS_SCRIPTING_SCRIPTING_H
#define GAME_SERVER_FOXNET_COMPONENTS_SCRIPTING_SCRIPTING_H

#include <engine/console.h>

#include <game/server/foxnet/component.h>

#include <memory>

class CGameContext;
class CScriptRunner;

struct CScriptRunnerDeleter
{
	void operator()(CScriptRunner *pRunner) const;
};

class CScripting : public CServerComponent
{
	static void ConExecScript(IConsole::IResult *pResult, void *pUserData);

	std::unique_ptr<CScriptRunner, CScriptRunnerDeleter> m_pRunner;

public:
	~CScripting();
	void SetGameServer(CGameContext *pGameServer);
	void ExecScript(const char *pFilename, const char *pArgs);
	void OnInit() override;
	void OnConsoleInit() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_SCRIPTING_SCRIPTING_H