#ifndef GAME_ENVELOPES_H
#define GAME_ENVELOPES_H

#include <base/vmath.h>

class CFixedTime;
class CEnvPoint;
class CEnvPointBezier;
class CEnvPointBezier_upstream;

class IEnvelopeAccess
{
public:
	virtual ~IEnvelopeAccess() = default;
	virtual int NumPoints() const = 0;
	virtual const CEnvPoint *GetPoint(int Index) const = 0;
	virtual const CEnvPointBezier *GetBezier(int Index) const = 0;
	int FindPointIndex(CFixedTime Time) const;
};

class CMapBasedEnvelopeAccess : public IEnvelopeAccess
{
	int m_StartPoint;
	int m_NumPoints;
	int m_NumPointsMax;
	CEnvPoint *m_pPoints;
	CEnvPointBezier *m_pPointsBezier;
	CEnvPointBezier_upstream *m_pPointsBezierUpstream;

public:
	CMapBasedEnvelopeAccess(class IMap *pMap);
	void SetPointsRange(int StartPoint, int NumPoints);
	int StartPoint() const;
	int NumPoints() const override;
	int NumPointsMax() const;
	const CEnvPoint *GetPoint(int Index) const override;
	const CEnvPointBezier *GetBezier(int Index) const override;
};

float SolveBezier(float x, float p0, float p1, float p2, float p3);
#endif // GAME_ENVELOPES_H