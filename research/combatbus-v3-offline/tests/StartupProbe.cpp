#include "StartupTrace.h"

int wmain()
{
	if (!combatbus_test::WriteStartupMarker("probe.wmain.entered", true)) return 90;
	if (!combatbus_test::WriteStartupMarker("probe.before_return")) return 91;
	return 0;
}
