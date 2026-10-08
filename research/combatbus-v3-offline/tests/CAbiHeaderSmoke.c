#include "CombatBusCABI.h"

int CAbiHeaderSmoke(void)
{
	return sizeof(IIF_CB_InterfaceV3) == 64 &&
		offsetof(IIF_CB_InterfaceV3, dispatch_incoming) == 56 &&
		sizeof(IIF_CB_OutgoingProviderV3) == 40 ? 1 : 0;
}
