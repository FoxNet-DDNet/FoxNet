#ifndef GAME_SERVER_FOXNET_INTEGRATION_ANTIBOB_H
#define GAME_SERVER_FOXNET_INTEGRATION_ANTIBOB_H

#include <base/dynamic.h>

class IConsole;

class CAntibobContext
{
public:
	IConsole *m_pConsole = nullptr;
};

extern CAntibobContext g_AntibobContext;

#ifndef ANTIBOBAPI
#define ANTIBOBAPI DYNAMIC_EXPORT
#endif

extern "C" {
ANTIBOBAPI int AntibobVersion();
ANTIBOBAPI void AntibobRcon(const char *pLine);
}

#endif
