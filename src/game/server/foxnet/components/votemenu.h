#ifndef GAME_SERVER_FOXNET_COMPONENTS_VOTEMENU_H
#define GAME_SERVER_FOXNET_COMPONENTS_VOTEMENU_H

#include "shop.h"

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/foxnet/component.h>
#include <game/server/foxnet/components/accounts/accounts.h>
#include <game/server/player.h>

#include <array>

constexpr int MAX_VOTE_LENGTH = 60; // if a vote exceeds this and has a prefix, it will be truncated

enum Pages
{
	PAGE_NONE = -1,

	PAGE_MAIN = 0,
	PAGE_SERVERINFO = 1,
	PAGE_SETTINGS = 2,
	PAGE_MAILBOX = 3,
	PAGE_SHOP = 4,
	PAGE_INVENTORY = 5,
	PAGE_VOTES = 6,
	PAGE_ADMIN = 7,
	NUM_PAGES = 8,
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

class CVoteMenu : public CServerComponent
{
	std::array<char[64], NUM_PAGES> m_aPages;
	class CClientData
	{
	public:
		int m_Page = PAGE_MAIN;
		int m_aSubPage[NUM_PAGES] = {0};

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
	CClientData m_aClientData[MAX_CLIENTS];
	std::vector<std::string> m_vDescriptions;

	bool IsPageAllowed(int ClientId, int Page) const;

	bool IsOptionWithSuffix(const char *pDesc, const char *pWantedOption) { return str_startswith(pDesc, pWantedOption) != 0; }
	bool IsOption(const char *pDesc, const char *pWantedOption) { return !str_comp(pDesc, pWantedOption); }

	void SendVotes(int ClientId, const std::vector<std::string> &vDescriptions);

	void ExecMailCmd(int ClientId, CMailBox::CMail &Mail);
	const char *FormatItemVote(CPlayer *pPlayer, const CItemConfig &Item);

	void AddVoteImpl(const char *pDesc);
	void AddVoteText(const char *pDesc, EPrefix Prefix = EPrefix::NONE);
	void AddVoteSeparator() { m_vDescriptions.emplace_back(" "); }
	void AddVoteSubheader(const char *pDesc);
	void AddVoteCheckBox(const char *pDesc, bool Checked);
	void AddVoteValueOption(const char *pDescription, int Value, int Max, EPrefix Prefix = EPrefix::NONE);
	void AddVoteValueOption(const char *pDescription, int Value, int Max, const char *pSuffixDesc);

	void PrepareMainMenu(int ClientId);
	void PrepareNormalVotes(int ClientId);
	void PrepareSettings(int ClientId);
	void PrepareMailbox(int ClientId);
	void PrepareShop(int ClientId);
	void PrepareInventory(int ClientId);
	void PrepareServerInfo(int ClientId);
	void PrepareAdmin(int ClientId);

	void UpdatePages(int ClientId);

	bool OwnsAnyOfType(int ClientId, EItemType ItemType) const;

public:
	void SetRetryTick(int ClientId, int64_t Tick) { m_aClientData[ClientId].m_RetryTick = Tick; }

	void PrepareVoteOptions(int ClientId);

	int GetSubPage(int ClientId) const;
	void SetSubPage(int ClientId, int Page, bool SendVotes = false);
	int GetPage(int ClientId) const;
	void SetPage(int ClientId, int Page);

	bool OnCallVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId);

	bool IsCustomVoteOption(const CNetMsg_Cl_CallVote *pMsg, int ClientId);

	void OnClientDrop(int ClientId, const char *pReason) override;
	void OnConsoleInit() override;
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_VOTEMENU_H
