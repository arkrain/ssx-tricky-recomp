/* Exercise the generated game's CRT without starting its game loop. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "recomp_funcs.h"

static void reset_call(void)
{
    g_esp = 0x400000;
    g_ebp = g_seh_ebp = 0x400100;
    g_fp_top = 0;
    g_fp_cc = 0;
    g_fp_control_word = 0x37f;
    memset(g_fp_stack, 0, sizeof(double) * 8);
    MEM32(g_esp) = 0x500000;
}

static int check_result(const char *name, double first, double second, double expected)
{
    double actual = g_fp_stack[g_fp_top];
    int equal = isnan(expected) ? isnan(actual) :
        actual == expected || fabs(actual - expected) <= 2e-14 * fmax(1, fabs(expected));
    if (actual == 0 && expected == 0 && signbit(actual) != signbit(expected)) equal = 0;
    if (!equal || g_fp_top != 7 || g_esp != 0x400004) {
        fprintf(stderr, "%s(%g,%g): %.17g expected %.17g top=%u esp=%x\n",
            name, first, second, actual, expected, g_fp_top, g_esp);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    unsigned char *ram = calloc(1, 0x800000);
    FILE *file = fopen(argv[1], "rb");
    if (!ram || !file || fread(ram, 1, 0x800000, file) != 0x800000) return 2;
    fclose(file);
    g_xbox_mem_offset = (ptrdiff_t)ram;
    if (!recomp_dispatch_init()) return 3;
    const double values[] = {0, -0.0, .049941278994083405, 1, -1, .25, -.25, 100, -100};
    int failed = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < sizeof(values)/sizeof(*values); ++i)
        for (unsigned j = 0; j < sizeof(values)/sizeof(*values); ++j) {
            double y = values[i], x = values[j];
            reset_call();
            MEMD(g_esp + 4) = y;
            MEMD(g_esp + 12) = x;
            sub_0015CA54();
            failed |= check_result("atan2", y, x, atan2(y, x));
            ++count;
        }
    for (unsigned i = 0; i < sizeof(values)/sizeof(*values); ++i) {
        double x = fabs(values[i]);
        reset_call();
        MEMD(g_esp + 4) = x;
        sub_0015CBC0();
        failed |= check_result("sqrt", x, 0, sqrt(x));
        ++count;
    }
    const double rounding_values[] = {-100.9,-2.5,-1.9,-1.5,-.9,-.5,-.1,
        -0.0,0,.1,.5,.9,1.5,1.9,2.5,100.9};
    for (unsigned i=0; i<sizeof(rounding_values)/sizeof(*rounding_values); ++i) {
        double x=rounding_values[i];
        reset_call();
        MEMD(g_esp+4)=x;
        sub_0015C826();
        failed |= check_result("floor",x,0,floor(x));
        ++count;
    }
    printf("%u generated CRT math cases: %s\n", count, failed ? "FAILED" : "passed");
    free(ram);
    return failed;
}
