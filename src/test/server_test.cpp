#include <engine/server/server.h>

#include <game/server/foxnet/integration/motion_peer.h>
#include <game/teamscore.h>

#include <gtest/gtest.h>

TEST(Server, StrHideIps)
{
	char aLine[512];
	char aLineWithoutIps[512];

	CServer::StrHideIps("hello world", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "hello world");
	EXPECT_STREQ(aLineWithoutIps, "hello world");

	CServer::StrHideIps("hello <{127.0.0.1}>", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "hello 127.0.0.1");
	EXPECT_STREQ(aLineWithoutIps, "hello XXX");

	CServer::StrHideIps("<{127.0.0.1}> <{127.0.0.1}>", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "127.0.0.1 <{127.0.0.1}>");
	EXPECT_STREQ(aLineWithoutIps, "XXX <{127.0.0.1}>");

	CServer::StrHideIps("o <{127.0.0.1}>", aLine, 8, aLineWithoutIps, 8);
	EXPECT_STREQ(aLine, "o 127.0");
	EXPECT_STREQ(aLineWithoutIps, "o XXX");

	CServer::StrHideIps("o <{127.0.0.1}>", aLine, 4, aLineWithoutIps, 4);
	EXPECT_STREQ(aLine, "o 1");
	EXPECT_STREQ(aLineWithoutIps, "o X");

	CServer::StrHideIps("<{}><{}> hello", aLine, 10, aLineWithoutIps, 10);
	EXPECT_STREQ(aLine, "<{}> hell");
	EXPECT_STREQ(aLineWithoutIps, "XXX<{}> h");

	CServer::StrHideIps("<{", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "<{");
	EXPECT_STREQ(aLineWithoutIps, "<{");

	CServer::StrHideIps("<{}>", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "");
	EXPECT_STREQ(aLineWithoutIps, "XXX");

	CServer::StrHideIps("<{<{<{<{<{<{<{", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "<{<{<{<{<{<{<{");
	EXPECT_STREQ(aLineWithoutIps, "<{<{<{<{<{<{<{");

	CServer::StrHideIps("<{<{<{<{<{<{<{a}>", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "<{<{<{<{<{<{a");
	EXPECT_STREQ(aLineWithoutIps, "XXX");

	CServer::StrHideIps("<{}><{}>", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "<{}>");
	EXPECT_STREQ(aLineWithoutIps, "XXX<{}>");

	CServer::StrHideIps("<{}>}>}>}>}>}>", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "}>}>}>}>}>");
	EXPECT_STREQ(aLineWithoutIps, "XXX}>}>}>}>}>");

	CServer::StrHideIps("<{<{<{a}>}>}>", aLine, sizeof(aLine), aLineWithoutIps, sizeof(aLineWithoutIps));
	EXPECT_STREQ(aLine, "<{<{a}>}>");
	EXPECT_STREQ(aLineWithoutIps, "XXX}>}>");
}

TEST(Server, MotionSoloPeerIsolation)
{
	CTeamsCore Teams;
	Teams.Team(0, 0);
	Teams.Team(1, 0);
	Teams.SetSolo(0, false);
	Teams.SetSolo(1, false);
	CAntibobMotionPeer Core, Peer;
	auto Isolated = [&]() {
		Core.m_CanCollide = Teams.CanCollide(0, 1);
		Peer.m_CanCollide = Teams.CanCollide(1, 0);
		return AntibobMotionSoloPeer(Core, Peer, Teams.GetSolo(0));
	};
	EXPECT_FALSE(Isolated()); // Normal team movement needs peer physics.
	Core.m_Solo = true;
	EXPECT_FALSE(Isolated()); // Core/team state disagreement is not certified.
	Teams.SetSolo(0, true);
	EXPECT_TRUE(Isolated());
	Peer.m_Super = true;
	EXPECT_FALSE(Isolated());
	Peer.m_Super = false;
	Teams.Team(1, Teams.TeamSuper());
	EXPECT_FALSE(Isolated()); // Native super-team overrides solo restrictions.
	Teams.Team(1, 0);
	Core.m_Solo = false;
	Teams.SetSolo(0, false);
	Peer.m_Solo = true;
	Teams.SetSolo(1, true);
	EXPECT_FALSE(Isolated()); // A peer could leave solo during the forecast.
	Core.m_Solo = true;
	EXPECT_FALSE(Isolated()); // Peer solo must not hide a mismatched own solo flag.
	Teams.SetSolo(0, true);
	EXPECT_TRUE(Isolated());
	Core.m_Super = true;
	EXPECT_FALSE(Isolated());
	Core.m_Super = false;
	Core.m_Solo = false;
	Teams.SetSolo(0, false);
	Peer.m_Solo = false;
	Teams.SetSolo(1, false);
	Teams.Team(1, 1);
	EXPECT_FALSE(Isolated()); // Different teams alone are intentionally unchanged.
	EXPECT_FALSE(AntibobMotionSoloPeer({false, false, false}, {false, false, false}, false)); // No-body-collision alone does not isolate hooks/hammer.
	Core.m_Solo = true;
	EXPECT_FALSE(AntibobMotionSoloPeer({true, false, false}, {false, false, true}, true));
	EXPECT_FALSE(AntibobMotionSoloPeer({true, false, true}, {false, false, false}, true));
}
