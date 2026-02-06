// Made by qxdFox, heavily inspired by Fokkonauts implementation
#include "votemenu.h"

#include "accounts.h"
#include "item_registry.h"
#include "shop.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/message.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/voting.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <base/log.h>

// Font: https://fsymbols.com/generators/smallcaps/

constexpr const char *SETTINGS_AUTO_LOGIN = "Auto Login";
constexpr const char *SETTINGS_HIDE_POWERUPS = "Hide PowerUps";
constexpr const char *SETTINGS_FAST_INPUTS = "Using Fast Inputs?";

constexpr const char *SETTINGS_COSMETICS_ANY = "Any Type";
constexpr const char *SETTINGS_COSMETICS_RAINBOW = "Rainbow";
constexpr const char *SETTINGS_COSMETICS_GUNS = "Guns";
constexpr const char *SETTINGS_COSMETICS_GUNHITS = "Gun Hits";
constexpr const char *SETTINGS_COSMETICS_DEATHS = "Deaths";
constexpr const char *SETTINGS_COSMETICS_TRAILS = "Trails";
constexpr const char *SETTINGS_COSMETICS_HATS = "Hats";
constexpr const char *SETTINGS_COSMETICS_EFFECTS = "Effects";

constexpr const char *SETTINGS_0_ROTATION = "0°";
constexpr const char *SETTINGS_90_ROTATION = "90°";
constexpr const char *SETTINGS_180_ROTATION = "180°";
constexpr const char *SETTINGS_270_ROTATION = "270°";

// Admin SubPages
constexpr const char *ADMIN_UTIL = "Util Page";
constexpr const char *ADMIN_MISC = "Misc Page";

// Admin Util
constexpr const char *ADMIN_UTIL_VANISH = "Vanish";

constexpr const char *ADMIN_UTIL_INVINCIBLE = "Invincible";
constexpr const char *ADMIN_UTIL_SPIDERHOOK = "Spider Hook";
constexpr const char *ADMIN_UTIL_PASSIVE = "Passive";

constexpr const char *ADMIN_UTIL_TELEKINESIS = "Telekinesis";
constexpr const char *ADMIN_UTIL_TELEK_IMMUNITY = "Telekinesis Immunity";

constexpr const char *ADMIN_UTIL_COLLIDABLE = "Collidable";
constexpr const char *ADMIN_UTIL_HITTABLE = "Hittable";
constexpr const char *ADMIN_UTIL_HOOKABLE = "Hookable";

// Admin Misc
constexpr const char *ADMIN_MISC_SNAKE = "Snake";
constexpr const char *ADMIN_MISC_UFO = "Ufo";

constexpr const char *ADMIN_MISC_OBFUSCATED = "Obfuscate Name";
constexpr const char *ADMIN_MISC_IGN_KILL_BORDER = "Ignore Kill Border";

constexpr const char *ADMIN_MISC_HEARTGUN = "Heart Gun";
constexpr const char *ADMIN_MISC_LIGHTSABER = "Lightsaber";
constexpr const char *ADMIN_MISC_PORTALGUN = "Portal gun";

// Mailbox
constexpr const char *MAIL_ONLY_UNREAD = "Only show unread mails";

constexpr const char *MAIL_MARK_ALL_READ = "✔ Mark all as read";
constexpr const char *MAIL_CLAIM_ALL_REWARDS = "⬇️ Claim all Rewards";
;
constexpr const char *MAIL_DELETE_ALL_READ = "✘ Delete all read Mails";

constexpr const char *MAIL_CLAIM_REWARD = "⬇️ Claim Reward";
constexpr const char *MAIL_DELETE = "✘ Delete Mail";

// Shop
constexpr const char *SHOP_ONLY_AFFORDABLE = "Only show Affordable Items";

// Navigation
constexpr const char *MAIN_MENU_PAGE = "↩ Main Menu ↩";
constexpr const char *BACKPAGE = "↩ Back ↩";

// Server Info Page

constexpr const char *SERVER_INFO_ACCOUNTS = "Accounts";
constexpr const char *SERVER_INFO_LEVELING = "Leveling";
constexpr const char *SERVER_INFO_CONTRIBUTE = "Have any Ideas?";
constexpr const char *SERVER_INFO_GITHUB = "Double click here to send the link to chat";

IServer *CVoteMenu::Server() const { return GameServer()->Server(); }

void CVoteMenu::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;

	str_copy(m_aPages[PAGE_MAIN], "Mᴀɪɴ Mᴇɴᴜ"); // Not shown

	str_copy(m_aPages[PAGE_SERVERINFO], "Sᴇʀᴠᴇʀ Iɴғᴏ");
	str_copy(m_aPages[PAGE_SETTINGS], "Sᴇᴛᴛɪɴɢs");
	str_copy(m_aPages[PAGE_MAILBOX], "Mᴀɪʟʙᴏx");
	str_copy(m_aPages[PAGE_SHOP], "Sʜᴏᴘ");
	str_copy(m_aPages[PAGE_INVENTORY], "Iɴᴠᴇɴᴛᴏʀʏ");
	str_copy(m_aPages[PAGE_VOTES], "Vᴏᴛᴇs");
	str_copy(m_aPages[PAGE_ADMIN], "Aᴅᴍɪɴ");
}

bool CVoteMenu::OnCallVote(const CNetMsg_Cl_CallVote *pMsg, int ClientId)
{
	if(!g_Config.m_SvCustomVoteMenu)
		return false;

	if(str_comp_nocase(pMsg->m_pType, "option") != 0)
		return false;

	const char *pVote = str_skip_voting_menu_prefixes(pMsg->m_pValue);

	if(!pVote || IsOption(pVote, ""))
		return true;

	for(int i = 0; i < NUM_PAGES; i++)
	{
		if(IsOptionWithSuffix(pVote, m_aPages[i]))
		{
			SetPage(ClientId, i);
			return true;
		}
	}

	if(IsCustomVoteOption(pMsg, ClientId))
	{
		GameServer()->ClearVotes(ClientId);
		return true;
	}
	return false;
}

bool CVoteMenu::IsCustomVoteOption(const CNetMsg_Cl_CallVote *pMsg, int ClientId)
{
	CVoteMenu::ClientData &Data = m_aClientData[ClientId];
	const int Page = Data.m_Page;
	const int SubPage = Data.m_SubPage[Page];
	const char *pVote = str_skip_voting_menu_prefixes(pMsg->m_pValue);
	const char *pReason = pMsg->m_pReason;

	std::optional<int> ReasonInt = std::nullopt;
	if(pReason[0] && str_isallnum(pReason))
		ReasonInt = str_toint(pReason);

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	CCharacter *pChr = pPlayer->GetCharacter();

	if(!pVote || !pPlayer)
		return false;

	if(Page < 0 || Page >= NUM_PAGES)
		return false;

	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];

	if(IsOption(pVote, MAIN_MENU_PAGE))
	{
		SetPage(ClientId, 0);
		SetSubPage(ClientId, 0);
		return true;
	}
	if(IsOption(pVote, BACKPAGE))
	{
		if(Page == PAGE_ADMIN)
		{
			SetPage(ClientId, PAGE_MAIN);
			return true;
		}

		if(Page == PAGE_SERVERINFO)
		{
			if(SubPage == SUB_SERVERINFO_MAIN)
				SetPage(ClientId, PAGE_MAIN);
			SetSubPage(ClientId, SUB_SERVERINFO_MAIN);
			return true;
		}

		int WantedSubPage = SubPage - 1;
		if(WantedSubPage < 0)
		{
			WantedSubPage = 0;
			SetPage(ClientId, PAGE_MAIN);
		}

		SetSubPage(ClientId, WantedSubPage);
		return true;
	}

	if(Page == PAGE_VOTES)
		return false;

	if(!IsPageAllowed(ClientId, Page))
	{
		return false;
	}

	if(Page == PAGE_MAIN)
	{
		// Nothing
	}
	else if(Page == PAGE_SETTINGS)
	{
		if(Acc.m_LoggedIn && IsOption(pVote, SETTINGS_AUTO_LOGIN))
		{
			Acc.m_Configs.m_AutoLogin = !Acc.m_Configs.m_AutoLogin;
			return true;
		}
		if(IsOption(pVote, SETTINGS_HIDE_POWERUPS))
		{
			pPlayer->SetHidePowerUps(!pPlayer->Acc()->m_Configs.m_HidePowerUps);
			return true;
		}
		if(IsOption(pVote, SETTINGS_FAST_INPUTS))
		{
			const char *pClient = Server()->ClientName(ClientId);

			if(!str_comp(pClient, "DDNet")) // DDNet doesn't have fast inputs
			{
				Acc.m_Configs.m_FastInputs = false;
				GameServer()->SendChatTarget(ClientId, "DDNet Client does not have Fast Inputs.");
				GameServer()->SendChatTarget(ClientId, "Download a Custom Client like:");
				GameServer()->SendChatTarget(ClientId, "'entityclient.net', 'tclient.app'");
				return true;
			}

			Acc.m_Configs.m_FastInputs = !Acc.m_Configs.m_FastInputs;
			return true;
		}

		if(IsOption(pVote, SETTINGS_COSMETICS_ANY))
		{
			bool NewState = !(Acc.m_Configs.m_Cosmetics.m_ShowRainbow && Acc.m_Configs.m_Cosmetics.m_ShowGuns &&
					  Acc.m_Configs.m_Cosmetics.m_ShowIndicators && Acc.m_Configs.m_Cosmetics.m_ShowDeaths &&
					  Acc.m_Configs.m_Cosmetics.m_ShowTrails && Acc.m_Configs.m_Cosmetics.m_ShowHats &&
					  Acc.m_Configs.m_Cosmetics.m_ShowEffects);
			Acc.m_Configs.m_Cosmetics.m_ShowRainbow = NewState;
			Acc.m_Configs.m_Cosmetics.m_ShowGuns = NewState;
			Acc.m_Configs.m_Cosmetics.m_ShowIndicators = NewState;
			Acc.m_Configs.m_Cosmetics.m_ShowDeaths = NewState;
			Acc.m_Configs.m_Cosmetics.m_ShowTrails = NewState;
			Acc.m_Configs.m_Cosmetics.m_ShowHats = NewState;
			Acc.m_Configs.m_Cosmetics.m_ShowEffects = NewState;
			return true;
		}
		if(IsOption(pVote, SETTINGS_COSMETICS_RAINBOW))
		{
			Acc.m_Configs.m_Cosmetics.m_ShowRainbow = !Acc.m_Configs.m_Cosmetics.m_ShowRainbow;
			return true;
		}
		if(IsOption(pVote, SETTINGS_COSMETICS_GUNS))
		{
			Acc.m_Configs.m_Cosmetics.m_ShowGuns = !Acc.m_Configs.m_Cosmetics.m_ShowGuns;
			return true;
		}
		if(IsOption(pVote, SETTINGS_COSMETICS_GUNHITS))
		{
			Acc.m_Configs.m_Cosmetics.m_ShowIndicators = !Acc.m_Configs.m_Cosmetics.m_ShowIndicators;
			return true;
		}
		if(IsOption(pVote, SETTINGS_COSMETICS_DEATHS))
		{
			Acc.m_Configs.m_Cosmetics.m_ShowDeaths = !Acc.m_Configs.m_Cosmetics.m_ShowDeaths;
			return true;
		}
		if(IsOption(pVote, SETTINGS_COSMETICS_TRAILS))
		{
			Acc.m_Configs.m_Cosmetics.m_ShowTrails = !Acc.m_Configs.m_Cosmetics.m_ShowTrails;
			return true;
		}
		if(IsOption(pVote, SETTINGS_COSMETICS_HATS))
		{
			Acc.m_Configs.m_Cosmetics.m_ShowHats = !Acc.m_Configs.m_Cosmetics.m_ShowHats;
			return true;
		}
		if(IsOption(pVote, SETTINGS_COSMETICS_EFFECTS))
		{
			Acc.m_Configs.m_Cosmetics.m_ShowEffects = !Acc.m_Configs.m_Cosmetics.m_ShowEffects;
			return true;
		}

		if(IsOption(pVote, SETTINGS_0_ROTATION))
		{
			Acc.m_Configs.m_HatItemFlags = 0;
			return true;
		}
		if(IsOption(pVote, SETTINGS_90_ROTATION))
		{
			Acc.m_Configs.m_HatItemFlags = PICKUPFLAG_ROTATE | PICKUPFLAG_XFLIP | PICKUPFLAG_YFLIP;
			return true;
		}
		if(IsOption(pVote, SETTINGS_180_ROTATION))
		{
			Acc.m_Configs.m_HatItemFlags = PICKUPFLAG_XFLIP | PICKUPFLAG_YFLIP;
			return true;
		}
		if(IsOption(pVote, SETTINGS_270_ROTATION))
		{
			Acc.m_Configs.m_HatItemFlags = PICKUPFLAG_ROTATE;
			return true;
		}
	}
	else if(Page == PAGE_MAILBOX)
	{
		if(!g_Config.m_SvAccounts)
			return false;

		if(SubPage == SUB_MAILBOX_MAIN)
		{
			if(IsOption(pVote, MAIL_ONLY_UNREAD))
			{
				Data.m_OnlyUnreadMails = !Data.m_OnlyUnreadMails;
				return true;
			}

			if(IsOption(pVote, MAIL_MARK_ALL_READ))
			{
				for(auto &Mail : Acc.m_MailBox.m_vMails)
					Mail.m_Unread = false;
				GameServer()->m_AccountManager.MarkAllMailsRead(Acc.m_aUsername);
				return true;
			}

			if(IsOption(pVote, MAIL_CLAIM_ALL_REWARDS))
			{
				for(auto &Mail : Acc.m_MailBox.m_vMails)
				{
					const bool HasUnclaimedReward = !Mail.m_UsedCmd && Mail.m_aCmdName[0] && Mail.m_aCmd[0];
					if(HasUnclaimedReward)
					{
						ExecMailCmd(ClientId, Mail);
						Mail.m_UsedCmd = true;
					}
				}
				GameServer()->m_AccountManager.ClaimAllMailRewards(Acc.m_aUsername);
				return true;
			}
			if(IsOption(pVote, MAIL_DELETE_ALL_READ))
			{
				auto &Mails = Acc.m_MailBox.m_vMails;
				for(int i = (int)Mails.size() - 1; i >= 0; --i)
				{
					const CMailBox::CMail &Mail = Mails[i];
					const bool HasReward = Mail.m_aCmd[0] != '\0';
					const bool RewardClaimed = Mail.m_UsedCmd;

					if(!Mail.m_Unread && (!HasReward || RewardClaimed))
					{
						Mails.erase(Mails.begin() + i);
					}
				}
				GameServer()->m_AccountManager.DeleteAllReadMails(Acc.m_aUsername);
				return true;
			}

			for(int i = 0; i < (int)Acc.m_MailBox.m_vMails.size(); i++)
			{
				char aBuf[VOTE_DESC_LENGTH];
				str_format(aBuf, sizeof(aBuf), "%d.", i + 1);

				if(str_startswith(pVote, aBuf))
				{
					str_copy(Data.m_aMetaData, std::to_string(i).c_str());
					SetSubPage(ClientId, SUB_MAILBOX_VIEW);
					Acc.m_MailBox.m_vMails[i].m_Unread = false;
					GameServer()->m_AccountManager.SetMailRead(Acc.m_aUsername, Acc.m_MailBox.m_vMails[i].m_MailId, false);
					return true;
				}
			}
		}
		else if(SubPage == SUB_MAILBOX_VIEW)
		{
			if(!Data.m_aMetaData[0])
				return false;

			int MailIdx = str_toint(Data.m_aMetaData);
			if(MailIdx < 0 || MailIdx >= (int)Acc.m_MailBox.m_vMails.size())
				return false;

			CMailBox::CMail &TargetMail = Acc.m_MailBox.m_vMails[MailIdx];

			if(IsOption(pVote, MAIL_CLAIM_REWARD))
			{
				if(TargetMail.m_UsedCmd)
				{
					GameServer()->SendChatTarget(ClientId, "This mails rewards have already been claimed.");
				}
				else
				{
					ExecMailCmd(ClientId, TargetMail);
					TargetMail.m_UsedCmd = true;
					GameServer()->m_AccountManager.SetMailUsedCmd(Acc.m_aUsername, TargetMail.m_MailId, TargetMail.m_UsedCmd);
				}

				return true;
			}
			if(IsOption(pVote, MAIL_DELETE))
			{
				GameServer()->m_AccountManager.DeleteMail(Acc.m_aUsername, TargetMail.m_MailId);
				Acc.m_MailBox.m_vMails.erase(Acc.m_MailBox.m_vMails.begin() + MailIdx);
				SetSubPage(ClientId, SUB_MAILBOX_MAIN);
				return true;
			}
		}
	}
	else if(Page == PAGE_SHOP)
	{
		if(!g_Config.m_SvAccounts)
			return false;

		if(IsOption(pVote, SHOP_ONLY_AFFORDABLE))
		{
			Data.m_OnlyAffordable = !Data.m_OnlyAffordable;
			return true;
		}
		if(SubPage == SUB_SHOP_MAIN)
		{
			for(int i = 0; i < (int)EItemType::COUNT; i++)
			{
				EItemType Type = static_cast<EItemType>(i);
				const char *pTypeName = ItemTypeToName(Type);
				if(IsOption(pVote, pTypeName))
				{
					str_copy(Data.m_aMetaData, pTypeName);
					SetSubPage(ClientId, SUB_SHOP_SELECT);
				}
			}
		}
		else if(SubPage == SUB_SHOP_SELECT)
		{
			for(const auto &kv : GameServer()->m_Shop.Registry().Map())
			{
				const CItemConfig &Item = kv.second;
				if(IsOption(Item.m_Name, ""))
					continue;
				if(Item.m_Price == -1)
					continue;
				const char *pVoteName = Item.m_Name;

				if(IsOption(pVote, pVoteName))
				{
					Data.m_pLastItemInfo = &Item;
					SetSubPage(ClientId, SUB_SHOP_ITEMINFO);
					return true;
				}
			}
		}
		else if(SubPage == SUB_SHOP_ITEMINFO)
		{
			long Price = pPlayer->GetDiscountedPrice(Data.m_pLastItemInfo->m_Price);

			if(IsOption(pVote, FormatItemVote(Price)))
			{
				GameServer()->m_Shop.BuyItem(ClientId, Data.m_pLastItemInfo->m_Name);
				SetSubPage(ClientId, SUB_SHOP_MAIN);
				return true;
			}
		}
	}
	else if(Page == PAGE_INVENTORY)
	{
		if(!g_Config.m_SvAccounts)
			return false;
		if(IsOptionWithSuffix(pVote, "Rainbow Speed"))
		{
			if(ReasonInt.has_value())
				pPlayer->Cosmetics()->m_RainbowSpeed = ReasonInt.value();
			else
				GameServer()->SendChatTarget(ClientId, "Please specify the rainbow speed using the reason field");
			return true;
		}
		const char *pEmoticonGunName = GameServer()->m_Shop.GetItemName(EItemId::EmoticonGun);
		if(IsOptionWithSuffix(pVote, pEmoticonGunName))
		{
			if(ReasonInt.has_value())
				pPlayer->UseItem(pEmoticonGunName, ReasonInt.value());
			else
				GameServer()->SendChatTarget(ClientId, "Please specify the emote type using the reason field");
			return true;
		}

		// Options that use the reason field go above

		for(const auto &kv : GameServer()->m_Shop.Registry().Map())
		{
			const CItemConfig &Item = kv.second;

			if(IsOption(Item.m_Name, ""))
				continue;
			if(Item.m_Price == -1)
				continue;

			if(IsOptionWithSuffix(pVote, Item.m_Name))
			{
				pPlayer->UseItem(Item.m_Name, -1);
				return true;
			}
		}
	}
	if(Page == PAGE_SERVERINFO)
	{
		if(g_Config.m_SvVoteMenuServerInfoRulesOnly)
			return true;

		if(g_Config.m_SvAccounts)
		{
			if(IsOption(pVote, SERVER_INFO_ACCOUNTS))
			{
				SetSubPage(ClientId, SUB_SERVERINFO_ACCOUNTS);
				return true;
			}
			if(IsOption(pVote, SERVER_INFO_LEVELING))
			{
				SetSubPage(ClientId, SUB_SERVERINFO_LEVELING);
				return true;
			}
		}
		if(IsOption(pVote, SERVER_INFO_CONTRIBUTE))
		{
			SetSubPage(ClientId, SUB_SERVERINFO_CONTRIBUTE);
			return true;
		}
		if(IsOption(pVote, SERVER_INFO_GITHUB))
		{
			GameServer()->SendChatTarget(ClientId, g_Config.m_SvGithubRepo);
			return true;
		}
	}
	if(Page == PAGE_ADMIN)
	{
		if(IsOption(pVote, ADMIN_UTIL))
		{
			SetSubPage(ClientId, SUB_ADMIN_UTIL);
			return true;
		}
		if(IsOption(pVote, ADMIN_MISC))
		{
			SetSubPage(ClientId, SUB_ADMIN_MISC);
			return true;
		}

		if(SubPage == SUB_ADMIN_UTIL)
		{
			if(IsOption(pVote, ADMIN_UTIL_INVINCIBLE))
			{
				if(pChr)
					pChr->SetInvincible(!pChr->Core()->m_Invincible);
				return true;
			}
			if(IsOption(pVote, ADMIN_UTIL_VANISH))
			{
				pPlayer->m_Vanish = !pPlayer->m_Vanish;
				return true;
			}
			if(IsOption(pVote, ADMIN_UTIL_SPIDERHOOK))
			{
				pPlayer->m_SpiderHook = !pPlayer->m_SpiderHook;
				return true;
			}
			if(IsOption(pVote, ADMIN_UTIL_PASSIVE))
			{
				if(pChr)
					pChr->SetPassive(!pChr->Core()->m_Passive);
				return true;
			}

			if(IsOption(pVote, ADMIN_UTIL_TELEKINESIS))
			{
				if(pChr)
					pChr->GiveWeapon(WEAPON_TELEKINESIS, pChr->GetWeaponGot(WEAPON_TELEKINESIS));
				return true;
			}
			if(IsOption(pVote, ADMIN_UTIL_TELEK_IMMUNITY))
			{
				pPlayer->SetTelekinesisImmunity(!pPlayer->m_TelekinesisImmunity);
				return true;
			}

			if(IsOption(pVote, ADMIN_UTIL_COLLIDABLE))
			{
				if(pChr)
					pChr->SetCollidable(!pChr->Core()->m_Collidable);
				return true;
			}
			if(IsOption(pVote, ADMIN_UTIL_HITTABLE))
			{
				if(pChr)
					pChr->SetHittable(!pChr->Core()->m_Hittable);
				return true;
			}
			if(IsOption(pVote, ADMIN_UTIL_HOOKABLE))
			{
				if(pChr)
					pChr->SetHookable(!pChr->Core()->m_Hookable);
				return true;
			}
		}

		if(SubPage == SUB_ADMIN_MISC)
		{
			if(IsOption(pVote, ADMIN_MISC_SNAKE) && pChr)
			{
				pChr->SetSnake(!pChr->m_Snake.Active());
				return true;
			}

			if(IsOption(pVote, ADMIN_MISC_UFO) && pChr)
			{
				pChr->SetUfo(!pChr->m_Ufo.Active());
				return true;
			}

			if(IsOption(pVote, ADMIN_MISC_IGN_KILL_BORDER))
			{
				pPlayer->m_IgnoreGamelayer = !pPlayer->m_IgnoreGamelayer;
				return true;
			}
			if(IsOption(pVote, ADMIN_MISC_OBFUSCATED))
			{
				pPlayer->SetObfuscated(!pPlayer->m_Obfuscated);
				return true;
			}
			if(IsOption(pVote, ADMIN_MISC_HEARTGUN))
			{
				if(pChr)
				{
					pChr->GiveWeapon(WEAPON_HEARTGUN, pChr->GetWeaponGot(WEAPON_HEARTGUN));
					pChr->SetActiveWeapon(WEAPON_HEARTGUN);
				}
				return true;
			}
			if(IsOption(pVote, ADMIN_MISC_LIGHTSABER))
			{
				if(pChr)
				{
					pChr->GiveWeapon(WEAPON_LIGHTSABER, pChr->GetWeaponGot(WEAPON_LIGHTSABER));
					pChr->SetActiveWeapon(WEAPON_LIGHTSABER);
				}
				return true;
			}
			if(IsOption(pVote, ADMIN_MISC_PORTALGUN))
			{
				if(pChr)
				{
					pChr->GiveWeapon(WEAPON_PORTALGUN, pChr->GetWeaponGot(WEAPON_PORTALGUN));
					pChr->SetActiveWeapon(WEAPON_PORTALGUN);
				}
				return true;
			}
		}
	}

	if(Page != PAGE_VOTES)
		return true;
	return false;
}

void CVoteMenu::Tick()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return;
		if(Server()->ClientSlotEmpty(ClientId) || !GameServer()->m_apPlayers[ClientId])
			continue;

		if(m_aClientData[ClientId].m_RetryTick == Server()->Tick() && m_aClientData[ClientId].m_RetryTick != -1)
		{
			if(GetPage(ClientId) == PAGE_VOTES)
				GameServer()->ClearVotes(ClientId);
			m_aClientData[ClientId].m_RetryTick = -1;
			continue;
		}

		if(GameServer()->m_apPlayers[ClientId]->m_PlayerFlags & PLAYERFLAG_IN_MENU)
			UpdatePages(ClientId);
	}
}

void CVoteMenu::OnClientDrop(int ClientId)
{
	CVoteMenu::ClientData &Data = m_aClientData[ClientId];
	Data.m_Page = PAGE_MAIN;
	for(int i = 0; i < NUM_PAGES; i++)
		Data.m_SubPage[i] = 0;
	Data.m_pLastItemInfo = nullptr;
	Data.m_OnlyAffordable = true;
}

void CVoteMenu::UpdatePages(int ClientId)
{
	const int Page = GetPage(ClientId);

	bool Changes = false;

	if(!IsPageAllowed(ClientId, Page))
	{
		if(Page != PAGE_MAIN)
			SetPage(ClientId, PAGE_MAIN);
		return;
	}
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	const CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	CAccountSession &OldAcc = m_aClientData[ClientId].m_Account;

	if(pAcc->m_LoggedIn != m_aClientData[ClientId].m_Account.m_LoggedIn) // Check login status change
		Changes = true;

	if(Page == PAGE_VOTES)
		return;

	if(Page == PAGE_SETTINGS)
	{
		if(memcmp(&pAcc->m_Configs, &OldAcc.m_Configs, sizeof(pAcc->m_Configs)) != 0)
			Changes = true;
	}
	if(Page == PAGE_MAIN)
	{
		if(pAcc->m_Level != OldAcc.m_Level)
			Changes = true;
		if(pAcc->m_XP != OldAcc.m_XP)
			Changes = true;
		if(pAcc->m_Playtime != OldAcc.m_Playtime)
			Changes = true;
		if(pAcc->m_Money != OldAcc.m_Money)
			Changes = true;
		if(pAcc->m_Deaths != OldAcc.m_Deaths)
			Changes = true;
		if(pAcc->m_Inventory.m_Map != OldAcc.m_Inventory.m_Map)
			Changes = true;
	}
	if(Page == PAGE_MAILBOX || Page == PAGE_MAIN)
	{
		if(pAcc->m_MailBox.m_vMails.size() != OldAcc.m_MailBox.m_vMails.size())
			Changes = true;
		else
		{
			for(size_t i = 0; i < pAcc->m_MailBox.m_vMails.size(); i++)
			{
				const CMailBox::CMail &NewMail = pAcc->m_MailBox.m_vMails[i];
				const CMailBox::CMail &OldMail = OldAcc.m_MailBox.m_vMails[i];
				if(NewMail.m_Unread != OldMail.m_Unread || NewMail.m_UsedCmd != OldMail.m_UsedCmd)
				{
					Changes = true;
					break;
				}
			}
		}
	}
	if(Page == PAGE_SHOP)
	{
		if(pAcc->m_Money != OldAcc.m_Money)
			Changes = true;
	}
	if(Page == PAGE_INVENTORY || Page == PAGE_ADMIN)
	{
		if(pAcc->m_Inventory.m_Map != OldAcc.m_Inventory.m_Map)
			Changes = true;
		if(memcmp(&pAcc->m_Inventory.m_Cosmetics, &OldAcc.m_Inventory.m_Cosmetics, sizeof(pAcc->m_Inventory.m_Cosmetics)) != 0)
			Changes = true;
	}

	if(Changes)
	{
		m_aClientData[ClientId].m_Account = GameServer()->m_aAccounts[ClientId];
		m_aClientData[ClientId].m_Cosmetics = *pPlayer->Cosmetics();
		GameServer()->ClearVotes(ClientId);
	}
}

bool CVoteMenu::IsPageAllowed(int ClientId, int Page) const
{
	if(Page < 0 || Page >= NUM_PAGES)
		return true;

	const CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];

	if(Page == PAGE_MAIN || Page == PAGE_SERVERINFO || Page == PAGE_SETTINGS || Page == PAGE_VOTES)
		return true;

	if(Page == PAGE_ADMIN && Server()->GetAuthedState(ClientId) < AUTHED_MOD) // Allow Mod Access
		return false;

	if(!pAcc->m_LoggedIn)
		return false;

	return true;
}

void CVoteMenu::PrepareVoteOptions(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const int Page = GetPage(ClientId);
	if(Page < 0 || Page >= NUM_PAGES)
		return;

	if(Server()->ClientSlotEmpty(ClientId) || !GameServer()->m_apPlayers[ClientId])
		return;

	if(!g_Config.m_SvCustomVoteMenu)
	{
		m_vDescriptions.clear();
		PrepareNormalVotes(ClientId);
		SendVotes(ClientId, m_vDescriptions);
		return;
	}

	m_vDescriptions.clear();

	if(Page != PAGE_MAIN)
	{
		AddVoteText(MAIN_MENU_PAGE);
		AddVoteText(BACKPAGE);
		AddVoteSeparator();
	}

	switch(Page)
	{
	case PAGE_MAIN: PrepareMainMenu(ClientId); break;
	case PAGE_VOTES: PrepareNormalVotes(ClientId); break;
	case PAGE_SETTINGS: PrepareSettings(ClientId); break;
	case PAGE_MAILBOX: PrepareMailbox(ClientId); break;
	case PAGE_SHOP: PrepareShop(ClientId); break;
	case PAGE_INVENTORY: PrepareInventory(ClientId); break;
	case PAGE_SERVERINFO: PrepareServerInfo(ClientId); break;
	case PAGE_ADMIN: PrepareAdmin(ClientId); break;
	}

	if(Page != PAGE_MAIN)
	{
		AddVoteSeparator();
		AddVoteText(BACKPAGE);
	}

	SendVotes(ClientId, m_vDescriptions);
}

void CVoteMenu::SendVotes(int ClientId, const std::vector<std::string> &vDescriptions)
{
	const int NumVotesToSend = vDescriptions.size();
	int TotalVotesSent = 0;

	CMsgPacker Msg(NETMSGTYPE_SV_VOTEOPTIONLISTADD);
	while(TotalVotesSent < NumVotesToSend)
	{
		const int VotesLeft = std::min(NumVotesToSend - TotalVotesSent, g_Config.m_SvSendVotesPerTick);
		Msg.AddInt(VotesLeft);

		int CurIndex = 0;

		while(CurIndex < VotesLeft)
		{
			Msg.AddString(vDescriptions.at(TotalVotesSent).c_str(), VOTE_DESC_LENGTH);
			CurIndex++;
			TotalVotesSent++;
		}

		if(!Server()->IsSixup(ClientId))
		{
			while(CurIndex < 15)
			{
				Msg.AddString("", VOTE_DESC_LENGTH);
				CurIndex++;
			}
		}
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
		Msg.Reset();
	}
}

int CVoteMenu::GetPage(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return PAGE_VOTES;
	return m_aClientData[ClientId].m_Page;
}

void CVoteMenu::SetPage(int ClientId, int Page)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(Page < 0 || Page >= NUM_PAGES)
		return;

	m_aClientData[ClientId].m_Page = Page;

	GameServer()->ClearVotes(ClientId);
}

int CVoteMenu::GetSubPage(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return 0;
	const int Page = m_aClientData[ClientId].m_Page;
	return m_aClientData[ClientId].m_SubPage[Page];
}

void CVoteMenu::SetSubPage(int ClientId, int SubPage, bool SendVotes)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const int Page = m_aClientData[ClientId].m_Page;
	if(Page < 0 || Page >= NUM_PAGES)
		return;

	m_aClientData[ClientId].m_SubPage[Page] = SubPage;
	if(SendVotes)
		GameServer()->ClearVotes(ClientId);
}

void CVoteMenu::PrepareMainMenu(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(Server()->ClientSlotEmpty(ClientId))
		return;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];

	char aBuf[VOTE_DESC_LENGTH];
	if(g_Config.m_SvAccounts)
	{
		if(!pAcc->m_LoggedIn)
		{
			AddVoteText("You are not logged in.");
			AddVoteSeparator();
			AddVoteText("1 - use /register <Name> <Password>");
			AddVoteText("2 - login using /login <Name> <Password>");
			AddVoteSeparator();
		}
		else
		{
			AddVoteText("╭─────────    Pʀᴏғɪʟᴇ");
			str_format(aBuf, sizeof(aBuf), "│ Account Name: %s", pAcc->m_aUsername);
			AddVoteText(aBuf);
			str_format(aBuf, sizeof(aBuf), "│ Last Ign: %s", pAcc->m_LastName);
			AddVoteText(aBuf);
			// Register Date
			if(pAcc->m_RegisterDate > 0)
			{
				str_timestamp_ex(pAcc->m_RegisterDate, aBuf, sizeof(aBuf), "│ Register Date: %Y-%m-%d");
				AddVoteText(aBuf);
			}
			else
			{
				AddVoteText("│ Register Date: n/a");
			}
			AddVoteText("├─────────   Sᴛᴀᴛs");
			str_format(aBuf, sizeof(aBuf), "│ Level [%ld]", pAcc->m_Level);
			AddVoteText(aBuf);
			int CurXp = pAcc->m_XP;
			int NeededXp = GameServer()->m_AccountManager.NeededXP(pAcc->m_Level);
			str_format(aBuf, sizeof(aBuf), "│ XP [%d/%d]", CurXp, NeededXp);
			AddVoteText(aBuf);
			str_format(aBuf, sizeof(aBuf), "│ Playtime: %s", FormatPlaytime(pAcc->m_Playtime));
			AddVoteText(aBuf);
			str_format(aBuf, sizeof(aBuf), "│ Money: %ld%s", pAcc->m_Money, g_Config.m_SvCurrencyName);
			AddVoteText(aBuf);
			str_format(aBuf, sizeof(aBuf), "│ Deaths: %ld", pAcc->m_Deaths);
			AddVoteText(aBuf);
			AddVoteText("├─────────   Bᴏᴏsᴛᴇʀs");
			str_format(aBuf, sizeof(aBuf), "│ %.1fx XP & Money", pPlayer->StatMultiplier());
			AddVoteText(aBuf);
			AddVoteText("╰────────────────────");
			AddVoteSeparator();
		}
	}
	for(int i = 0; i < NUM_PAGES; i++)
	{
		if(!IsPageAllowed(ClientId, i))
			continue;
		if(i == PAGE_MAIN)
			continue;

		std::string PageName = m_aPages[i];

		if(i == PAGE_MAILBOX)
		{
			int UnreadMails = 0;
			for(const auto &Mail : pAcc->m_MailBox.m_vMails)
			{
				const bool HasUnclaimedReward = !Mail.m_UsedCmd && Mail.m_aCmdName[0] && Mail.m_aCmd[0];

				if(Mail.m_Unread || HasUnclaimedReward)
					UnreadMails++;
			}
			if(UnreadMails > 0)
				PageName += " [" + std::to_string(UnreadMails) + "]";
		}

		AddVoteText(PageName.c_str(), EPrefix::ARROWHEAD);
	}
}

void CVoteMenu::PrepareNormalVotes(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(Server()->ClientSlotEmpty(ClientId))
		return;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	char aBuf[VOTE_DESC_LENGTH];

	if(GameServer()->m_pVoteOptionFirst == nullptr)
	{
		AddVoteText("No vote options available.");
		return;
	}

	for(CVoteOptionServer *pOption = GameServer()->m_pVoteOptionFirst; pOption; pOption = pOption->m_pNext)
	{
		str_copy(aBuf, pOption->m_aDescription);
		if(!str_comp(pOption->m_aCommand, "map_vote_lock"))
			str_format(aBuf, sizeof(aBuf), "%s%s", pOption->m_aDescription, GameServer()->m_MapVoteLock ? "ALLOW Map Changing" : "LOCK Map Changing");
		AddVoteText(aBuf);
	}
}

void CVoteMenu::PrepareSettings(int ClientId)
{
	const CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];

	AddVoteText("Sᴇᴛᴛɪɴɢs:");
	if(pAcc->m_LoggedIn)
		AddVoteCheckBox(SETTINGS_AUTO_LOGIN, pAcc->m_Configs.m_AutoLogin);
	AddVoteCheckBox(SETTINGS_HIDE_POWERUPS, pAcc->m_Configs.m_HidePowerUps);
	AddVoteCheckBox(SETTINGS_FAST_INPUTS, pAcc->m_Configs.m_FastInputs);

	AddVoteSeparator();

	AddVoteText("Sʜᴏᴡ Cᴏsᴍᴇᴛɪᴄs:");
	AddVoteCheckBox(SETTINGS_COSMETICS_ANY, pAcc->m_Configs.m_Cosmetics.m_ShowRainbow && pAcc->m_Configs.m_Cosmetics.m_ShowGuns && pAcc->m_Configs.m_Cosmetics.m_ShowIndicators && pAcc->m_Configs.m_Cosmetics.m_ShowDeaths && pAcc->m_Configs.m_Cosmetics.m_ShowTrails && pAcc->m_Configs.m_Cosmetics.m_ShowHats && pAcc->m_Configs.m_Cosmetics.m_ShowEffects);
	AddVoteCheckBox(SETTINGS_COSMETICS_RAINBOW, pAcc->m_Configs.m_Cosmetics.m_ShowRainbow);
	AddVoteCheckBox(SETTINGS_COSMETICS_GUNS, pAcc->m_Configs.m_Cosmetics.m_ShowGuns);
	AddVoteCheckBox(SETTINGS_COSMETICS_GUNHITS, pAcc->m_Configs.m_Cosmetics.m_ShowIndicators);
	AddVoteCheckBox(SETTINGS_COSMETICS_DEATHS, pAcc->m_Configs.m_Cosmetics.m_ShowDeaths);
	AddVoteCheckBox(SETTINGS_COSMETICS_TRAILS, pAcc->m_Configs.m_Cosmetics.m_ShowTrails);
	AddVoteCheckBox(SETTINGS_COSMETICS_HATS, pAcc->m_Configs.m_Cosmetics.m_ShowHats);
	AddVoteCheckBox(SETTINGS_COSMETICS_EFFECTS, pAcc->m_Configs.m_Cosmetics.m_ShowEffects);

	int ClientVersion = Server()->GetClientVersion(ClientId);
	if(ClientVersion >= VERSION_DDNET_PICKUP_ROTATION || ClientVersion == -1)
	{
		AddVoteSeparator();

		AddVoteText("Hᴀᴛ Rᴏᴛᴀᴛɪᴏɴ:");
		AddVoteCheckBox(SETTINGS_0_ROTATION, !pAcc->m_Configs.m_HatItemFlags);
		AddVoteCheckBox(SETTINGS_90_ROTATION, pAcc->m_Configs.m_HatItemFlags == (PICKUPFLAG_ROTATE | PICKUPFLAG_XFLIP | PICKUPFLAG_YFLIP));
		AddVoteCheckBox(SETTINGS_180_ROTATION, pAcc->m_Configs.m_HatItemFlags == (PICKUPFLAG_XFLIP | PICKUPFLAG_YFLIP));
		AddVoteCheckBox(SETTINGS_270_ROTATION, pAcc->m_Configs.m_HatItemFlags == PICKUPFLAG_ROTATE);
	}
}

void CVoteMenu::PrepareMailbox(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	CVoteMenu::ClientData &Data = m_aClientData[ClientId];
	const CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];

	const int SubPage = GetSubPage(ClientId);

	if(SubPage == SUB_MAILBOX_MAIN)
	{
		AddVoteText("╭───────  Actions");
		AddVoteText(MAIL_MARK_ALL_READ, EPrefix::LONG_LINE);
		AddVoteText(MAIL_CLAIM_ALL_REWARDS, EPrefix::LONG_LINE);
		AddVoteText(MAIL_DELETE_ALL_READ, EPrefix::LONG_LINE);
		AddVoteText("╰────────────────────");
		AddVoteSeparator();

		if(pAcc->m_MailBox.m_vMails.empty())
		{
			AddVoteText("No mails in your mailbox.");
			return;
		}

		AddVoteSubheader("Fɪʟᴛᴇʀs");
		AddVoteCheckBox(MAIL_ONLY_UNREAD, Data.m_OnlyUnreadMails);
		AddVoteSeparator();

		AddVoteText("Mᴀɪʟs:");
		int Idx = 0;

		if(pAcc->m_MailBox.m_vMails.empty())
		{
			AddVoteText("You have no mails in your mailbox.");
			return;
		}

		int ShownMails = 0;

		for(const CMailBox::CMail &Mail : pAcc->m_MailBox.m_vMails)
		{
			const bool UsedCmd = Mail.m_UsedCmd || Mail.m_aCmd[0] == '\0';
			if(Data.m_OnlyUnreadMails && !Mail.m_Unread && UsedCmd)
			{
				Idx++;
				continue;
			}
			ShownMails++;

			char aSuffix[10] = "";
			if(Mail.m_Unread)
				str_copy(aSuffix, " [!]", sizeof(aSuffix));
			else if(!UsedCmd)
				str_copy(aSuffix, " [⬇️]", sizeof(aSuffix));

			char aBuf[VOTE_DESC_LENGTH];
			str_format(aBuf, sizeof(aBuf), "%d. %s%s", Idx + 1, Mail.m_aSubject, aSuffix);

			AddVoteText(aBuf);
			Idx++;
		}
		if(ShownMails == 0)
			AddVoteText("No Mails available with the current filters.");
	}
	else if(SubPage == SUB_MAILBOX_VIEW)
	{
		const char *pMeta = Data.m_aMetaData;
		if(!pMeta[0])
		{
			SetSubPage(ClientId, SUB_MAILBOX_MAIN, true);
			return;
		}
		int MailIdx = str_toint(pMeta);
		if(MailIdx < 0 || MailIdx >= (int)pAcc->m_MailBox.m_vMails.size())
		{
			SetSubPage(ClientId, SUB_MAILBOX_MAIN, true);
			return;
		}
		const auto &Mail = pAcc->m_MailBox.m_vMails[MailIdx];

		const bool HasReward = Mail.m_aCmdName[0] && Mail.m_aCmd[0];

		AddVoteText("╭───────── Mᴀɪʟ Dᴇᴛᴀɪʟs");
		if(!Mail.m_UsedCmd && HasReward)
			AddVoteText(MAIL_CLAIM_REWARD, EPrefix::LONG_LINE);
		AddVoteText(MAIL_DELETE, EPrefix::LONG_LINE);
		AddVoteText("╰────────────────────");
		AddVoteSeparator();
		AddVoteSubheader(Mail.m_aSubject);
		char aUnescaped[1024] = "";
		str_copy(aUnescaped, Mail.m_aMessage, sizeof(aUnescaped));
		UnescapeNewlines(aUnescaped);
		StrNewlineExceedLength(aUnescaped, MAX_VOTE_LENGTH);
		std::vector<const char *> Lines = StrSplit(aUnescaped, '\n');
		for(const auto &Line : Lines)
			AddVoteText(Line);

		if(HasReward)
		{
			AddVoteSeparator();
			AddVoteSubheader("Rᴇᴡᴀʀᴅ");

			str_copy(aUnescaped, Mail.m_aCmdName, sizeof(aUnescaped));
			UnescapeNewlines(aUnescaped);
			Lines = StrSplit(aUnescaped, '\n');

			for(const auto &Line : Lines)
				AddVoteText(Line);
		}
	}
}

void CVoteMenu::PrepareShop(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	if(!g_Config.m_SvAccounts)
	{
		SetPage(ClientId, PAGE_MAIN);
		return;
	}

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	CVoteMenu::ClientData &Data = m_aClientData[ClientId];
	const CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];

	const int SubPage = GetSubPage(ClientId);

	if(!pAcc->m_LoggedIn)
	{
		AddVoteText("You are not logged in.");
		AddVoteSeparator();
		AddVoteText("1 - use /register <Name> <Password>");
		AddVoteText("2 - login using /login <Name> <Password>");
		return;
	}

	char aBuf[VOTE_DESC_LENGTH];
	AddVoteText("╭─────── Aᴄᴄᴏᴜɴᴛ Iɴғᴏ");
	str_format(aBuf, sizeof(aBuf), "│ Money: %ld%s | Level %ld", pAcc->m_Money, g_Config.m_SvCurrencyName, pAcc->m_Level);
	AddVoteText(aBuf);
	AddVoteText("╰────────────");
	AddVoteSeparator();

	if(SubPage != SUB_SHOP_ITEMINFO)
	{
		AddVoteSubheader("Fɪʟᴛᴇʀs");
		AddVoteCheckBox(SHOP_ONLY_AFFORDABLE, Data.m_OnlyAffordable);
		AddVoteSeparator();
	}

	auto HasAnyItemOfType = [this](EItemType Type) -> bool {
		for(const auto &kv : GameServer()->m_Shop.Registry().Map())
		{
			const CItemConfig &Item = kv.second;
			if(Item.m_Type != Type)
				continue;
			if(Item.m_Price == -1)
				continue;
			return true;
		}
		return false;
	};

	auto CanBuyAnyOfType = [this, pAcc, pPlayer](EItemType Type) -> bool {
		for(const auto &kv : GameServer()->m_Shop.Registry().Map())
		{
			const CItemConfig &Item = kv.second;
			if(Item.m_Type != Type)
				continue;
			if(Item.m_Price == -1)
				continue;
			if(pPlayer->GetDiscountedPrice(Item.m_Price) <= pAcc->m_Money)
				return true;
		}
		return false;
	};

	if(SubPage == SUB_SHOP_MAIN)
	{
		const EPrefix Prefix = EPrefix::LONG_LINE;

		std::vector<std::string> AvailableCategories;

		for(int i = 0; i < (int)EItemType::COUNT; i++)
		{
			EItemType Type = static_cast<EItemType>(i);
			const char *pTypeName = ItemTypeToName(Type);

			if(!HasAnyItemOfType(Type))
				continue;

			if(Data.m_OnlyAffordable)
			{
				if(CanBuyAnyOfType(Type))
					AvailableCategories.push_back(pTypeName);
			}
			else
				AvailableCategories.push_back(pTypeName);
		}
		if(AvailableCategories.empty())
		{
			AddVoteText("No categories available with the current filters.");
			return;
		}
		AddVoteText("╭───────── Cᴀᴛᴇɢᴏʀɪᴇs");
		for(const auto &pCategory : AvailableCategories)
			AddVoteText(pCategory.c_str(), Prefix);
		AddVoteText("╰────────────");
	}
	else if(SubPage == SUB_SHOP_SELECT)
	{
		const char *pMeta = Data.m_aMetaData;
		if(!pMeta[0])
		{
			SetSubPage(ClientId, SUB_SHOP_MAIN, true);
			return;
		}
		int AmountShown = 0;

		for(const auto &kv : GameServer()->m_Shop.Registry().Map())
		{
			const CItemConfig &Item = kv.second;

			if(str_comp(ItemTypeToName(Item.m_Type), pMeta) != 0)
				continue;

			if(Item.m_Price == -1)
				continue;

			if(Data.m_OnlyAffordable && pPlayer->GetDiscountedPrice(Item.m_Price) > pAcc->m_Money)
				continue;
			AmountShown++;
			AddVoteText(Item.m_Name, EPrefix::ARROWHEAD);
		}
		if(AmountShown == 0)
		{
			AddVoteText("No items available in this category with the current filters.");
		}
	}
	else if(GetSubPage(ClientId) == SUB_SHOP_ITEMINFO)
	{
		if(!Data.m_pLastItemInfo)
		{
			SetSubPage(ClientId, SUB_SHOP_MAIN, true);
			return;
		}

		const CItemConfig *pItem = Data.m_pLastItemInfo;

		AddVoteText("╭─────── Iᴛᴇᴍ Iɴғᴏ");
		str_format(aBuf, sizeof(aBuf), "│ %s ⌬", pItem->m_Name);
		AddVoteText(aBuf);

		char aUnescaped[1024] = "";
		str_copy(aUnescaped, pItem->m_Description, sizeof(aUnescaped));
		UnescapeNewlines(aUnescaped);
		std::vector<const char *> Lines = StrSplit(aUnescaped, '\n');
		for(const auto &Line : Lines)
			AddVoteText(Line, EPrefix::LONG_LINE);

		AddVoteText("├────── Rarity");
		str_format(aBuf, sizeof(aBuf), "│ %s %s", RarityToName(pItem->m_Rarity), StarsString(pItem->m_Stars).c_str());
		AddVoteText(aBuf);
		AddVoteText("╰────────────────────");
		AddVoteSeparator();
		long Price = pPlayer->GetDiscountedPrice(Data.m_pLastItemInfo->m_Price);

		str_copy(aBuf, FormatItemVote(Price));
		AddVoteText(aBuf);

		str_format(aBuf, sizeof(aBuf), "↳ Requires level %d", pItem->m_MinLevel);
		AddVoteText(aBuf);
	}
}

void CVoteMenu::PrepareInventory(int ClientId)
{
	if(!g_Config.m_SvAccounts)
	{
		SetPage(ClientId, PAGE_MAIN);
		return;
	}

	// CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	CAccountSession Acc = GameServer()->m_aAccounts[ClientId];
	if(!Acc.m_LoggedIn)
	{
		AddVoteText("You are not logged in.");
		AddVoteSeparator();
		AddVoteText("1 - use /register <Name> <Password>");
		AddVoteText("2 - login using /login <Name> <Password>");
		return;
	}

	if(!g_Config.m_SvCosmetics)
	{
		AddVoteText("Cosmetics are currently disabled");
		return;
	}
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];

	std::vector<CVoteData> Votes;
	int OwnedItems = 0;

	const int NumItemTypes = (int)EItemType::COUNT;
	for(int i = 0; i < NumItemTypes; i++)
	{
		EItemType Type = static_cast<EItemType>(i);
		// Type | ItemType | ItemName | VoteName
		if(!OwnsAnyOfType(ClientId, Type))
			continue;

		{
			CVoteData Data;
			Data.m_ItemType = Type;
			Data.m_VoteType = VOTE_TYPE_SUBHEADER;
			str_copy(Data.m_aVoteName, ItemTypeToName(Type));
			Votes.push_back(Data);
		}

		if(Type == EItemType::Rainbow)
		{
			CVoteData Data;
			Data.m_ItemType = Type;
			Data.m_VoteType = VOTE_TYPE_VALUE_OPTION;
			str_copy(Data.m_aVoteName, "Rainbow Speed");
			Data.m_Value = pPlayer->Cosmetics()->m_RainbowSpeed;
			Data.m_Max = 20;
			Votes.push_back(Data);
		}

		for(const auto &kv : GameServer()->m_Shop.Registry().Map())
		{
			const char *pItemName = kv.first.c_str();
			CItemConfig Item = kv.second;
			if(!Acc.m_Inventory.Has(pItemName))
				continue;
			CInventoryEntry &InvEntry = Acc.m_Inventory.Entry(pItemName);

			if(Item.m_Type != Type)
				continue;
			if(!str_comp(pItemName, ""))
				continue;
			if(Item.m_Price == -1)
				continue;
			if(!(pPlayer->OwnsItem(pItemName)))
				continue;

			OwnedItems++;

			int64_t Now = time(0);
			int64_t Expiry = InvEntry.m_ExpiresAt;
			int64_t Remaining = Expiry - Now;

			char TimeBuf[20] = "";
			char aVoteName[VOTE_DESC_LENGTH];
			str_copy(aVoteName, pItemName);
			if(Remaining > 0)
			{
				FormatItemTime(Remaining, TimeBuf, sizeof(TimeBuf));
				str_format(aVoteName, sizeof(aVoteName), "%s [→ %s]", pItemName, TimeBuf);
			}

			if(HasFlag(Item.m_Flags, EItemFlag::Consumable))
			{
				int OwnsAmount = InvEntry.m_Quantity;
				CVoteData Data;
				Data.m_ItemType = Type;
				Data.m_pItem = &Item;
				Data.m_VoteType = VOTE_TYPE_TEXT;
				str_format(aVoteName, sizeof(aVoteName), "%s [%dx]", pItemName, OwnsAmount);
				str_copy(Data.m_aVoteName, aVoteName);
				Votes.push_back(Data);
			}
			else if(!HasFlag(Item.m_Flags, EItemFlag::Equippable))
			{
				CVoteData Data;
				Data.m_ItemType = Type;
				Data.m_pItem = &Item;
				Data.m_VoteType = VOTE_TYPE_TEXT;
				str_copy(Data.m_aVoteName, aVoteName);
				Votes.push_back(Data);
			}
			else if(Item.m_Id == EItemId::EmoticonGun)
			{
				CVoteData Data;
				Data.m_ItemType = Type;
				Data.m_pItem = &Item;
				Data.m_VoteType = VOTE_TYPE_VALUE_OPTION;
				str_copy(Data.m_aVoteName, pItemName);
				Data.m_Value = pPlayer->Cosmetics()->m_EmoticonGun;
				Data.m_Prefix = EPrefix::NONE;
				Data.m_Max = NUM_EMOTICONS;
				if(Remaining > 0)
				{
					char Suffix[VOTE_DESC_LENGTH];
					str_format(Suffix, sizeof(Suffix), "[→ %s]", TimeBuf);
					str_copy(Data.m_aSuffixDesc, Suffix);
				}
				else
				{
					Data.m_aSuffixDesc[0] = '\0';
				}
				Votes.push_back(Data);
			}
			else
			{
				CVoteData Data;
				Data.m_ItemType = Type;
				Data.m_pItem = &Item;
				Data.m_VoteType = VOTE_TYPE_CHECKBOX;
				str_copy(Data.m_aVoteName, aVoteName);
				Data.m_Value = pPlayer->ItemEnabled(pItemName);
				Votes.push_back(Data);
			}
		}

		if(i != NumItemTypes - 1)
		{
			CVoteData Data;
			Data.m_ItemType = Type;
			Data.m_VoteType = VOTE_TYPE_TEXT;
			str_copy(Data.m_aVoteName, "");
			Votes.push_back(Data);
		}
	}

	if(!OwnedItems)
	{
		AddVoteText("You don't own any cosmetics.");
		return;
	}

	for(const auto &Vote : Votes)
	{
		if(Vote.m_VoteType == VOTE_TYPE_TEXT)
		{
			AddVoteText(Vote.m_aVoteName);
		}
		else if(Vote.m_VoteType == VOTE_TYPE_SUBHEADER)
		{
			AddVoteSubheader(Vote.m_aVoteName);
		}
		else if(Vote.m_VoteType == VOTE_TYPE_CHECKBOX)
		{
			AddVoteCheckBox(Vote.m_aVoteName, Vote.m_Value);
		}
		else if(Vote.m_VoteType == VOTE_TYPE_VALUE_OPTION)
		{
			if(Vote.m_aSuffixDesc[0] != '\0')
				AddVoteValueOption(Vote.m_aVoteName, Vote.m_Value, Vote.m_Max, Vote.m_aSuffixDesc);
			else
				AddVoteValueOption(Vote.m_aVoteName, Vote.m_Value, Vote.m_Max, Vote.m_Prefix);
		}
	}
}

void CVoteMenu::PrepareServerInfo(int ClientId)
{
	const int SubPage = GetSubPage(ClientId);

	bool Printed = false;

	char *apRuleLines[] = {
		g_Config.m_SvRulesLine1,
		g_Config.m_SvRulesLine2,
		g_Config.m_SvRulesLine3,
		g_Config.m_SvRulesLine4,
		g_Config.m_SvRulesLine5,
		g_Config.m_SvRulesLine6,
		g_Config.m_SvRulesLine7,
		g_Config.m_SvRulesLine8,
		g_Config.m_SvRulesLine9,
		g_Config.m_SvRulesLine10,
	};

	if(SubPage == SUB_SERVERINFO_MAIN)
	{
		AddVoteText("╭───────    ʀᴜʟᴇꜱ");
		if(g_Config.m_SvDDRaceRules)
		{
			AddVoteText("Be nice.", EPrefix::LONG_LINE);
		}
		else
		{
			for(auto &pRuleLine : apRuleLines)
			{
				if(pRuleLine[0])
				{
					char aUnescaped[1024] = "";
					str_copy(aUnescaped, pRuleLine, sizeof(aUnescaped));
					UnescapeNewlines(aUnescaped);
					StrNewlineExceedLength(aUnescaped, MAX_VOTE_LENGTH);
					std::vector<const char *> Lines = StrSplit(aUnescaped, '\n');
					for(const auto &Line : Lines)
						AddVoteText(Line, EPrefix::LONG_LINE);
					Printed = true;
				}
			}
			if(!Printed)
			{
				AddVoteText("No Rules Defined.", EPrefix::LONG_LINE);
			}
		}
		AddVoteText("╰────────────────────");
		if(g_Config.m_SvVoteMenuServerInfoRulesOnly)
			return;

		AddVoteSeparator();

		if(g_Config.m_SvAccounts)
		{
			AddVoteText(SERVER_INFO_ACCOUNTS, EPrefix::ARROWHEAD);
			AddVoteText(SERVER_INFO_LEVELING, EPrefix::ARROWHEAD);
		}
		if(!g_Config.m_SvGithubRepo[0])
			AddVoteText(SERVER_INFO_CONTRIBUTE, EPrefix::ARROWHEAD);
	}
	else if(SubPage == SUB_SERVERINFO_ACCOUNTS)
	{
		AddVoteText("╭───────    Aᴄᴄᴏᴜɴᴛs");
		AddVoteText("│ Accounts are used to save your");
		AddVoteText("│ progress and earn rewards!");
		AddVoteText("├───────────────");
		AddVoteText("│ Having an account will grant you access");
		AddVoteText("│ to earning money and leveling up");
		AddVoteText("│ With money you can buy items from the shop");
		AddVoteText("├───────────────");
		AddVoteText("│ You can view your own account or someone");
		AddVoteText("│ elses by using '/profile <name>' in the chat");
		AddVoteText("╰────────────────────");
	}
	else if(SubPage == SUB_SERVERINFO_LEVELING)
	{
		AddVoteText("╭───────    Lᴇᴠᴇʟɪɴɢ");
		AddVoteText("│ 1. You Earn XP passively every minute you're playing");
		AddVoteText("│ 2. For Every finish, you get the amount of points as XP");
		AddVoteText("│ 3. Powerups will randomly spawn around the map:");
		AddVoteText("│    - They will either give you Money or XP");
		AddVoteText("├───────────────");
		AddVoteText("│ On weekends there is a global 2x multiplier on Money and XP");
		AddVoteText("╰────────────────────");
	}
	else if(SubPage == SUB_SERVERINFO_CONTRIBUTE)
	{
		if(!g_Config.m_SvGithubRepo[0])
		{
			AddVoteText("GitHub repository not set up by the server admin.");
			return;
		}

		AddVoteText("╭───────    Cᴏɴᴛʀɪʙᴜᴛɪɴɢ");
		AddVoteText("│ Want to contribute to the server?");
		AddVoteText("│ Or have an Idea for a feature?");
		AddVoteText("│ Check out our GitHub page!");
		AddVoteText("╰────────────────────");

		AddVoteSeparator();

		// AddVoteText(g_Config.m_SvGithubRepo, EPrefix::LONG_LINE);
		AddVoteText(SERVER_INFO_GITHUB, EPrefix::ARROWHEAD);
	}
}

bool CVoteMenu::CanUseCmd(int ClientId, const char *pCmd) const
{
	const IConsole::ICommandInfo *pInfo = GameServer()->Console()->GetCommandInfo(pCmd, CFGFLAG_SERVER, false);
	if(!pInfo)
		return false;

	const IConsole::EAccessLevel Required = pInfo->GetAccessLevel();
	IConsole::EAccessLevel ClientLevel = IConsole::EAccessLevel::USER;
	switch(Server()->GetAuthedState(ClientId))
	{
	case AUTHED_ADMIN: ClientLevel = IConsole::EAccessLevel::ADMIN; break;
	case AUTHED_MOD: ClientLevel = IConsole::EAccessLevel::MODERATOR; break;
	case AUTHED_HELPER: ClientLevel = IConsole::EAccessLevel::HELPER; break;
	default: ClientLevel = IConsole::EAccessLevel::USER; break;
	}
	return Required >= ClientLevel;
}

void CVoteMenu::PrepareAdmin(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);

	AddVoteSubheader("Aᴅᴍɪɴ Pᴀɢᴇs");
	AddVoteText(ADMIN_UTIL, GetSubPage(ClientId) == SUB_ADMIN_UTIL ? EPrefix::BLACK_DIAMOND : EPrefix::WHITE_DIAMOND);
	AddVoteText(ADMIN_MISC, GetSubPage(ClientId) == SUB_ADMIN_MISC ? EPrefix::BLACK_DIAMOND : EPrefix::WHITE_DIAMOND);
	AddVoteSeparator();
	if(GetSubPage(ClientId) == SUB_ADMIN_UTIL)
	{
		if(pChr && CanUseCmd(ClientId, "invincible"))
			AddVoteCheckBox(ADMIN_UTIL_INVINCIBLE, pChr->Core()->m_Invincible);
		if(CanUseCmd(ClientId, "spider_hook"))
			AddVoteCheckBox(ADMIN_UTIL_SPIDERHOOK, pPlayer->m_SpiderHook);
		if(CanUseCmd(ClientId, "vanish"))
			AddVoteCheckBox(ADMIN_UTIL_VANISH, pPlayer->m_Vanish);
		if(pChr)
		{
			if(CanUseCmd(ClientId, "telekinesis"))
				AddVoteCheckBox(ADMIN_UTIL_TELEKINESIS, pChr->GetWeaponGot(WEAPON_TELEKINESIS));
			if(CanUseCmd(ClientId, "telekinesis_immunity"))
				AddVoteCheckBox(ADMIN_UTIL_TELEK_IMMUNITY, pPlayer->m_TelekinesisImmunity);
			AddVoteSeparator();

			if(CanUseCmd(ClientId, "passive"))
			{
				AddVoteCheckBox(ADMIN_UTIL_PASSIVE, pChr->Core()->m_Passive);
				AddVoteSeparator();
			}

			if(CanUseCmd(ClientId, "collidable") && CanUseCmd(ClientId, "hittable") && CanUseCmd(ClientId, "hookable"))
			{
				AddVoteText("Should your own character be:");
				AddVoteCheckBox(ADMIN_UTIL_COLLIDABLE, pChr->Core()->m_Collidable);
				AddVoteCheckBox(ADMIN_UTIL_HITTABLE, pChr->Core()->m_Hittable);
				AddVoteCheckBox(ADMIN_UTIL_HOOKABLE, pChr->Core()->m_Hookable);
			}
		}
	}
	if(GetSubPage(ClientId) == SUB_ADMIN_MISC)
	{
		AddVoteSubheader("Mɪsᴄᴇʟʟᴀɴᴇᴏᴜs");
		if(pChr)
		{
			if(CanUseCmd(ClientId, "snake"))
				AddVoteCheckBox(ADMIN_MISC_SNAKE, pChr->m_Snake.Active());
			if(CanUseCmd(ClientId, "ufo"))
				AddVoteCheckBox(ADMIN_MISC_UFO, pChr->m_Ufo.Active());
		}

		AddVoteSeparator();
		if(CanUseCmd(ClientId, "obfuscate"))
			AddVoteCheckBox(ADMIN_MISC_OBFUSCATED, pPlayer->m_Obfuscated);

		if(CanUseCmd(ClientId, "ignore_gamelayer"))
			AddVoteCheckBox(ADMIN_MISC_IGN_KILL_BORDER, pPlayer->m_IgnoreGamelayer);

		AddVoteSeparator();
		if(pChr)
		{
			if(CanUseCmd(ClientId, "heartgun"))
				AddVoteCheckBox(ADMIN_MISC_HEARTGUN, pChr->GetWeaponGot(WEAPON_HEARTGUN));
			if(CanUseCmd(ClientId, "lightsaber"))
				AddVoteCheckBox(ADMIN_MISC_LIGHTSABER, pChr->GetWeaponGot(WEAPON_LIGHTSABER));
			if(CanUseCmd(ClientId, "Portalgun"))
				AddVoteCheckBox(ADMIN_MISC_PORTALGUN, pChr->GetWeaponGot(WEAPON_PORTALGUN));
		}
	}
}

bool CVoteMenu::OwnsAnyOfType(int ClientId, EItemType ItemType) const
{
	const CAccountSession *pAcc = &GameServer()->m_aAccounts[ClientId];
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer || !pAcc || !pAcc->m_LoggedIn)
		return false;
	for(const auto &kv : GameServer()->m_Shop.Registry().Map())
	{
		const CItemConfig &Item = kv.second;

		if(Item.m_Type != ItemType)
			continue;
		if(pPlayer->OwnsItem(Item.m_Name))
			return true;
	}
	return false;
}

const char *CVoteMenu::FormatItemVote(long Price)
{
	static char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "Buy Item for 30 days [%ld%s]", Price, g_Config.m_SvCurrencyName);
	return aBuf;
}

void CVoteMenu::ExecMailCmd(int ClientId, const CMailBox::CMail Mail)
{
	const char *pTemplate = Mail.m_aCmd;
	char aCmd[256] = "";
	char *pDst = aCmd;
	size_t DstRemain = sizeof(aCmd) - 1;
	for(const char *p = pTemplate; *p && DstRemain;)
	{
		if(*p == '%')
		{
			if(p[1] == '%')
			{
				if(DstRemain)
				{
					*pDst++ = '%';
					--DstRemain;
				}
				p += 2;
				continue;
			}
			else if(p[1] == 'd' || p[1] == 'i')
			{
				std::string Id = std::to_string(ClientId);
				size_t IdLen = str_length(Id.c_str());
				if(IdLen > DstRemain)
					IdLen = DstRemain;
				if(IdLen)
				{
					mem_copy(pDst, Id.c_str(), IdLen);
					pDst += IdLen;
					DstRemain -= IdLen;
				}
				p += 2;
				continue;
			}
			if(DstRemain)
			{
				*pDst++ = *p++;
				--DstRemain;
			}
			continue;
		}

		*pDst++ = *p++;
		--DstRemain;
	}
	*pDst = '\0';

	GameServer()->Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
}

void CVoteMenu::AddVoteImpl(const char *pDesc)
{
	const int Length = str_length(pDesc);
	dbg_assert(Length < VOTE_DESC_LENGTH, "Vote description too long '%s'", pDesc);
	m_vDescriptions.emplace_back(pDesc);
}

void CVoteMenu::AddVoteText(const char *pDesc, EPrefix Prefix)
{
	const char *pPrefixes[] = {"", "•", "─", "➤", ">", "⇨", "‣", "⁃", "◆", "◇", "│"};

	if((int)Prefix <= (int)EPrefix::NONE || (int)Prefix >= (int)EPrefix::NUM)
	{
		AddVoteImpl(pDesc);
		return;
	}
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s %s", pPrefixes[(int)Prefix], pDesc);
	AddVoteImpl(aBuf);
}

void CVoteMenu::AddVoteValueOption(const char *pDescription, int Value, int Max, EPrefix Prefix)
{
	char aBuf[VOTE_DESC_LENGTH];
	if(Max == -1)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d", pDescription, Value);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d/%d", pDescription, Value, Max);
	}
	AddVoteText(aBuf, Prefix);
}

void CVoteMenu::AddVoteValueOption(const char *pDescription, int Value, int Max, const char *pSuffixDesc)
{
	char aBuf[VOTE_DESC_LENGTH];
	if(Max == -1)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d %s", pDescription, Value, pSuffixDesc);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d/%d %s", pDescription, Value, Max, pSuffixDesc);
	}
	AddVoteText(aBuf);
}

void CVoteMenu::AddVoteSubheader(const char *pDesc)
{
	char aBuf[128];
	// str_format(aBuf, sizeof(aBuf), "═─═ %s ═─═", pDesc);
	str_format(aBuf, sizeof(aBuf), "─── %s ───", pDesc);
	AddVoteText(aBuf);
}

void CVoteMenu::AddVoteCheckBox(const char *pDesc, bool Checked)
{
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s %s", Checked ? "☒" : "☐", pDesc);
	AddVoteText(aBuf);
}