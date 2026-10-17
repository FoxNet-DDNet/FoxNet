#ifndef GAME_SERVER_FOXNET_INTEGRATION_MOTION_PEER_H
#define GAME_SERVER_FOXNET_INTEGRATION_MOTION_PEER_H

struct CAntibobMotionPeer
{
	bool m_Solo = false;
	bool m_Super = false;
	bool m_CanCollide = true;
};

// Only the forecasted tee's native solo state certifies the whole horizon.
// A solo peer might leave solo during a counterfactual. Different teams or body
// collision alone must not broaden the physics forecast's supported context.
inline bool AntibobMotionSoloPeer(const CAntibobMotionPeer &Core, const CAntibobMotionPeer &Peer, bool NativeSolo)
{
	return Core.m_Solo && NativeSolo && !Core.m_Super && !Peer.m_Super && !Core.m_CanCollide && !Peer.m_CanCollide;
}

#endif
