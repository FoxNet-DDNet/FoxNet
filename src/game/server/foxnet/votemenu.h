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
	PAGE_SERVERINFO,
	PAGE_SETTINGS,
	PAGE_VOTES,
	PAGE_SHOP,
	PAGE_INVENTORY,
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

enum VoteTypes
{
	VOTE_TYPE_TEXT = 0,
	VOTE_TYPE_SUBHEADER,
	VOTE_TYPE_PREFIX,
	VOTE_TYPE_CHECKBOX,
	VOTE_TYPE_VALUE_OPTION,
};

class CVoteData
{
public:
	CItem *m_pItem = nullptr;

	int m_ItemType = 0;
	int m_VoteType = 0;
	std::string m_sVoteName;
	int m_Max = -1;
	int m_Prefix = 0;
	int m_Value = 0;
	std::string m_sSuffixDesc;

	CVoteData() = default;
	
	CVoteData(int VoteType, const std::string &pVoteName)
	{
		m_VoteType = VoteType;
		m_sVoteName = pVoteName;
	}
	CVoteData(int VoteType, const std::string &pVoteName, int Prefix)
	{
		m_VoteType = VoteType;
		m_sVoteName = pVoteName;
		m_Prefix = Prefix;
	}

	CVoteData(CItem *pItem, const std::string &pVoteName, int Prefix)
	{
		m_pItem = pItem;
		m_VoteType = VOTE_TYPE_CHECKBOX;
		m_sVoteName = pVoteName;
		m_Prefix = Prefix;
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

		char m_aMetaData[64] = "";
		CItem *m_pLastItemInfo = nullptr;
		// After executing a file with a bunch of votes, we need to resend after some ticks
		int64_t m_RetryTick = -1;
	};
	ClientData m_aClientData[MAX_CLIENTS];
	std::vector<std::string> m_vDescriptions;

	bool IsPageAllowed(int ClientId, int Page) const;

	bool IsOptionWithSuffix(const char *pDesc, const char *pWantedOption) { return str_startswith(pDesc, pWantedOption) != 0; }
	bool IsOption(const char *pDesc, const char *pWantedOption) { return !str_comp(pDesc, pWantedOption); }

	void AddVoteText(const char *pDesc) { m_vDescriptions.emplace_back(pDesc); }
	void AddVoteSeparator() { m_vDescriptions.emplace_back(" "); }
	void AddVoteSubheader(const char *pDesc);
	void AddVotePrefix(const char *pDesc, int Prefix);
	void AddVoteCheckBox(const char *pDesc, bool Checked);
	void AddVoteValueOption(const char *pDescription, int Value, int Max, int Prefix = PREFIX_NONE);
	void AddVoteValueOption(const char *pDescription, int Value, int Max, const char *pSuffixDesc);

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

	bool CanUseCmd(int ClientId, const char *pCmd) const;

	bool CanBuyAnyOfType(int ClientId, int ItemType) const;
public:
	
	void SetRetryTick(int ClientId, int64_t Tick) { m_aClientData[ClientId].m_RetryTick = Tick; }

	void PrepareVoteOptions(int ClientId);

	int GetSubPage(int ClientId) const;
	void SetSubPage(int ClientId, int Page, bool SendVotes = false);
	int GetPage(int ClientId) const;
	void SetPage(int ClientId, int Page);

	void Tick();
	void OnClientDrop(int ClientId);
	void Init(CGameContext *pGameServer);
	bool OnCallVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId);

	bool IsCustomVoteOption(const CNetMsg_Cl_CallVote *pMsg, int ClientId);
};

#endif // GAME_SERVER_FOXNET_VOTEMENU_H