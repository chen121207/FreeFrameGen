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
    FgdsHdrMetadata hdr = {0};
    hdr.structSize = sizeof(hdr);
    hdr.version = FGDS_HDR_VERSION_1;
    FgdsSharedPair shared = {0};
    shared.structSize = sizeof(shared);
    shared.version = FGDS_SHARED_VERSION_1;
    return pair.structSize == 0 || pair.version == 0 || capabilities.structSize == 0 ||
           pairV2.structSize == 0 || pairV2.version != FGDS_VERSION_0_2 ||
           capabilitiesV3.structSize == 0 || capabilitiesV3.version != FGDS_VERSION_0_3 ||
           hdr.structSize == 0 || shared.structSize == 0;
}
