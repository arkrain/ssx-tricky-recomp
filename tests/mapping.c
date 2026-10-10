/* Regression: a requested mapping must never replace occupied host memory. */
#include "platform/win32_compat.h"
#include <stdint.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)
int main(void)
{
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
    CHECK(mapping != NULL);
    uint32_t *first = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
    CHECK(first != NULL);
    first[0] = 0x1234abcd;
    CHECK(MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096, first) == NULL);
    CHECK(first[0] == 0x1234abcd);
    uint32_t *alias = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
    CHECK(alias != NULL && alias != first);
    CHECK(alias[0] == first[0]);
    alias[0] = 0x5678ef01;
    CHECK(first[0] == 0x5678ef01);
    CHECK(UnmapViewOfFile(alias));
    CHECK(UnmapViewOfFile(first));
    CHECK(CloseHandle(mapping));
    return 0;
}
