#include <sysdolphin/baselib/archive.h>

#include <stdio.h>

int main(void)
{
    if (HSD_ArchiveGetPublicAddress(NULL, "root") != NULL) {
        fprintf(stderr, "empty public archive lookup returned a pointer\n");
        return 1;
    }
    if (HSD_ArchiveGetExtern(NULL, 0) != NULL) {
        fprintf(stderr, "empty external archive lookup returned a pointer\n");
        return 1;
    }
    HSD_ArchiveLocateExtern(NULL, "root", NULL);
    return 0;
}
