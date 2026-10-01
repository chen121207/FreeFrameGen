#include "fgds/fgds_vk.h"

int main(void)
{
    FgdsVkPair pair = {0};
    pair.structSize = sizeof(pair);
    pair.version = FGDS_VK_VERSION;
    FgdsVkCapabilities capabilities = {0};
    capabilities.structSize = sizeof(capabilities);
    FgdsVkPairV2 pairV2 = {0};
    pairV2.structSize = sizeof(pairV2);
    pairV2.version = FGDS_VK_VERSION_0_2;
    FgdsVkCapabilitiesV3 capabilitiesV3 = {0};
    capabilitiesV3.structSize = sizeof(capabilitiesV3);
    capabilitiesV3.version = FGDS_VK_VERSION_0_3;
    return pair.structSize == 0 || pair.version == 0 || capabilities.structSize == 0 ||
           pairV2.structSize == 0 || pairV2.version != FGDS_VK_VERSION_0_2 ||
           capabilitiesV3.structSize == 0 || capabilitiesV3.version != FGDS_VK_VERSION_0_3;
}
