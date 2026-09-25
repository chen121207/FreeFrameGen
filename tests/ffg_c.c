#include "ffg/ffg.h"
int main(void)
{
    FgdsPair pair = {0};
    pair.structSize = sizeof(pair);
    pair.version = FGDS_VERSION;
    return pair.structSize == 0 || pair.version == 0;
}
