#include <stdio.h>
#include <stdlib.h>

#include "../assets/events.h"

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main(void)
{
    NativeArchiveError error = { 0 };
    void* output = (void*) 1;

    CHECK(NativeEventArchiveOpen(NULL) == NULL);
    CHECK(NativeEventArchiveRead(NULL, "sqEventInitDataLevelTbl", 0, &output,
                                 &error) == NATIVE_ARCHIVE_INVALID);
    CHECK(output == NULL);
    CHECK(error.status == NATIVE_ARCHIVE_INVALID);
    return 0;
}
