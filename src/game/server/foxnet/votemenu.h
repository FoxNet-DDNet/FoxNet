#ifndef GAME_SERVER_FOXNET_VOTEMENU_H
#define GAME_SERVER_FOXNET_VOTEMENU_H
#include "accounts.h"
#include "shop.h"

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/player.h>

#include <array>

class CGameContext;
class IServer;

enum Pages
{
	PAGE_NONE = -1,

	PAGE_MAIN = 0,
	PAGE_VOTES,
	PAGE_SETTINGS,
	PAGE_SHOP,
	PAGE_INVENTORY,
	PAGE_SERVERINFO,
	PAGE_ADMIN,
	NUM_PAGES,
};

enum AdminSubPages
{
	SUB_ADMIN_UTIL = 0,
	SUB_ADMIN_MISC,
	SUB_ADMIN_COSMETICS,
};

enum ShopSubPages
{
	SUB_SHOP_MAIN = 0,
	SUB_SHOP_SELECT,
	SUB_SHOP_ITEMINFO,
};
enum ServerInfoSubPages
{
	SUB_SERVERINFO_MAIN = 0,
	SUB_SERVERINFO_ACCOUNTS,
	SUB_SERVERINFO_LEVELING,
	SUB_SERVERINFO_CONTRIBUTE,
};

enum Prefixes
{
	PREFIX_NONE = 0,
	// •
	PREFIX_POINT,
	// ─
	PREFIX_DASH,
	// ➤
	PREFIX_ARROWHEAD,
	// >
	PREFIX_GREATER_THAN,
	// ⇨
	PREFIX_ARROW,
	// ‣
	PREFIX_TRIANGLE,
	// ⁃
	PREFIX_HYPHEN,
	// ◆
	PREFIX_BLACK_DIAMOND,
	// ◇
	PREFIX_WHITE_DIAMOND,
	// │
	PREFIX_LONG_LINE,
};

class CItemVoteData
{
public:
	std::string m_pItemName;
	std::string m_pVoteName;
	CItemVoteData(const std::string &pItemName, const std::string &pVoteName)
	{
		m_pItemName = pItemName;
		m_pVoteName = pVoteName;
	}
};

class CVoteMenu
{
	CGameContext *m_pGameServer;
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	std::array<char[64], NUM_PAGES> m_aPages;
	struct ClientData
	{
		int m_Page = PAGE_MAIN;
		int m_SubPage[NUM_PAGES] = {0};

		// Comparison data for auto updates
		CAccountSession m_Account = CAccountSession();
		CCosmetics m_Cosmetics;

		bool m_OnlyAffordable = false;

		char m_aMetaData[16] = "";
		CItem *m_pLastItemInfo = nullptr;
	};
	ClientData m_aClientData[MAX_CLIENTS];
	std::vector<std::string> m_vDescriptions;

	bool IsPageAllowed(int ClientId, int Page) const;

	bool IsOptionWithSuffix(const char *pDesc, const char *pWantedOption) { return str_startswith(pDesc, pWantedOption) != 0; }
	bool IsOption(const char *pDesc, const char *pWantedOption) { return !str_comp(pDesc, pWantedOption); }

	void AddVoteText(const char *pDesc) { m_vDescriptions.emplace_back(pDesc); }
	void AddVoteSeperator() { m_vDescriptions.emplace_back(" "); }
	void AddVoteSubheader(const char *pDesc);
	void AddVotePrefix(const char *pDesc, int Prefix);
	void AddVoteCheckBox(const char *pDesc, bool Checked);
	void AddVoteValueOption(const char *pDescription, int Value, int Max, int BulletPoint);

	void SendPageMainMenu(int ClientId);
	void SendPageVotes(int ClientId);
	void SendPageSettings(int ClientId);
	void SendPageShop(int ClientId);
	void SendPageInventory(int ClientId);
	void SendPageServerInfo(int ClientId);
	void SendPageAdmin(int ClientId);

	const char *FormatItemVote(const CItem *pItem);

	void DoCosmeticVotes(int ClientId, bool Authed);

	void UpdatePages(int ClientId);

	int GetSubPage(int ClientId) const;
	void SetSubPage(int ClientId, int Page, bool SendVotes = false);

	bool CanUseCmd(int ClientId, const char *pCmd) const;

	bool CanBuyAnyOfType(int ClientId, int ItemType) const;
	const char *ItemTypeToName(int Type) const;

	int64_t m_RetryTick = -1;
public:

	void PrepareVoteOptions(int ClientId);

	int GetPage(int ClientId) const;
	void SetPage(int ClientId, int Page);

	void Tick();
	void OnClientDrop(int ClientId);
	void Init(CGameContext *pGameServer);
	bool OnCallVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId);

	bool IsCustomVoteOption(const CNetMsg_Cl_CallVote *pMsg, int ClientId);
};

#endif // GAME_SERVER_FOXNET_VOTEMENU_H