#include "ffg/ffg.h"
int main(void)
{
    FgdsPair pair = {0};
    pair.structSize = sizeof(pair);
    pair.version = FGDS_VERSION;
    FgdsCapabilities capabilities = {0};
    capabilities.structSize = sizeof(capabilities);
    FgdsPairV2 pairV2 = {0};
    pairV2.structSize = sizeof(pairV2);
    pairV2.version = FGDS_VERSION_0_2;
    FgdsCapabilitiesV3 capabilitiesV3 = {0};
    capabilitiesV3.structSize = sizeof(capabilitiesV3);
    capabilitiesV3.version = FGDS_VERSION_0_3;
    return pair.structSize == 0 || pair.version == 0 || capabilities.structSize == 0 ||
           pairV2.structSize == 0 || pairV2.version != FGDS_VERSION_0_2 ||
           capabilitiesV3.structSize == 0 || capabilitiesV3.version != FGDS_VERSION_0_3;
}
