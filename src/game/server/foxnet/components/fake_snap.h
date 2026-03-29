#ifndef GAME_SERVER_FOXNET_COMPONENTS_FAKE_SNAP_H
#define GAME_SERVER_FOXNET_COMPONENTS_FAKE_SNAP_H

#include <engine/console.h>

#include <game/server/foxnet/component.h>
#include <game/server/foxnet/item_registry.h>

class CGameContext;
class IServer;

class CFakeSnap : public CServerComponent
{
	class CFakeSnapPlayer
	{
	public:
		int m_ClientId;

		char m_aName[16];
		char m_aClan[12];
		int m_Country;

		bool m_CustomColors;
		char m_aSkinName[24];
		int m_ColorBody;
		int m_ColorFeet;

		char m_aMessage[256];

		char m_aContext[24] = "fake-message";
	};

	std::vector<CFakeSnapPlayer> m_vFakeSnapPlayers;

	static void ConSendFakeMessage(IConsole::IResult *pResult, void *pUserData);

public:
	bool AddFakeMessage(const char *pName, const char *pMessage, const char *pSkinName, bool CustomColor = false, int ColorBody = 0, int ColorFeet = 0);

	void OnSnap(int ClientId, bool GlobalSnap, bool RecordingDemo) override;
	void OnPostGlobalSnap() override;
	void OnConsoleInit() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_FAKE_SNAP_H
