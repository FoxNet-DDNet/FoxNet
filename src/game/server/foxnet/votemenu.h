#ifndef GAME_SERVER_FOXNET_VOTEMENU_H
#define GAME_SERVER_FOXNET_VOTEMENU_H
#include "accounts.h"
#include "shop.h"

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/player.h>

#include <array>

constexpr int MAX_VOTE_LENGTH = 60; // if a vote exceeds this and has a prefix, it will be truncated

class CGameContext;
class IServer;

enum Pages
{
	PAGE_NONE = -1,

	PAGE_MAIN = 0,
	PAGE_SERVERINFO,
	PAGE_SETTINGS,
	PAGE_MAILBOX,
	PAGE_SHOP,
	PAGE_INVENTORY,
	PAGE_VOTES,
	PAGE_ADMIN,
	NUM_PAGES,
};

enum AdminSubPages
{
	SUB_ADMIN_UTIL = 0,
	SUB_ADMIN_MISC,
};

enum MailboxSubPages
{
	SUB_MAILBOX_MAIN = 0,
	SUB_MAILBOX_VIEW,
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

enum class EPrefix
{
	NONE = 0,
	// •
	POINT,
	// ─
	DASH,
	// ➤
	ARROWHEAD,
	// >
	GREATER_THAN,
	// ⇨
	ARROW,
	// ‣
	TRIANGLE,
	// ⁃
	HYPHEN,
	// ◆
	BLACK_DIAMOND,
	// ◇
	WHITE_DIAMOND,
	// │
	LONG_LINE,
	NUM
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
	CItemConfig *m_pItem = nullptr;

	EItemType m_ItemType = EItemType::COUNT;
	int m_VoteType = 0;
	char m_aVoteName[64] = "";
	int m_Max = -1;
	EPrefix m_Prefix = EPrefix::NONE;
	int m_Value = 0;
	char m_aSuffixDesc[64] = "";

	CVoteData() = default;

	CVoteData(int VoteType, const char *pVoteName)
	{
		str_copy(m_aVoteName, pVoteName);
		m_VoteType = VoteType;
	}
	CVoteData(int VoteType, const char *pVoteName, EPrefix Prefix)
	{
		str_copy(m_aVoteName, pVoteName);
		m_VoteType = VoteType;
		m_Prefix = Prefix;
	}

	CVoteData(CItemConfig *pItem, const char *pVoteName, EPrefix Prefix)
	{
		str_copy(m_aVoteName, pVoteName);
		m_pItem = pItem;
		m_VoteType = VOTE_TYPE_CHECKBOX;
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

		bool m_OnlyUnreadMails = false;

		bool m_OnlyAffordable = false;

		char m_aMetaData[64] = "";
		const CItemConfig *m_pLastItemInfo = nullptr;
		// After executing a file with a bunch of votes, we need to resend after some ticks
		int64_t m_RetryTick = -1;
	};
	ClientData m_aClientData[MAX_CLIENTS];
	std::vector<std::string> m_vDescriptions;

	bool IsPageAllowed(int ClientId, int Page) const;

	bool IsOptionWithSuffix(const char *pDesc, const char *pWantedOption) { return str_startswith(pDesc, pWantedOption) != 0; }
	bool IsOption(const char *pDesc, const char *pWantedOption) { return !str_comp(pDesc, pWantedOption); }

	void AddVoteImpl(const char *pDesc);
	void AddVoteText(const char *pDesc, EPrefix Prefix = EPrefix::NONE);
	void AddVoteSeparator() { m_vDescriptions.emplace_back(" "); }
	void AddVoteSubheader(const char *pDesc);
	void AddVoteCheckBox(const char *pDesc, bool Checked);
	void AddVoteValueOption(const char *pDescription, int Value, int Max, EPrefix Prefix = EPrefix::NONE);
	void AddVoteValueOption(const char *pDescription, int Value, int Max, const char *pSuffixDesc);

	void SendPageMainMenu(int ClientId);
	void SendPageVotes(int ClientId);
	void SendPageSettings(int ClientId);
	void SendPageMailbox(int ClientId);
	void SendPageShop(int ClientId);
	void SendPageInventory(int ClientId);
	void SendPageServerInfo(int ClientId);
	void SendPageAdmin(int ClientId);

	void ExecMailCmd(int ClientId, const CMailBox::CMail Mail);

	const char *FormatItemVote(long Price);

	void UpdatePages(int ClientId);

	bool CanUseCmd(int ClientId, const char *pCmd) const;

	bool OwnsAnyOfType(int ClientId, EItemType ItemType) const;

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