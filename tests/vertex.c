#include "nv2a_vertex.h"
#include <assert.h>
#include <string.h>
static void upload(unsigned address, const unsigned *code, unsigned count) {
    ssx_vertex_method(0x1e9c, address);
    for (unsigned i = 0; i < count; ++i) ssx_vertex_method(0xb00 + (i % 32) * 4, code[i]);
    ssx_vertex_method(0x1ea0, address);
}
static void value(unsigned method, float f) {
    unsigned bits; memcpy(&bits, &f, 4); ssx_vertex_method(method, bits);
}
int main(void) {
    float in[16][4] = {{21, 32, 43, 1}}, out[13][4];
    /* MOV oPos, v0; END. Tests real microcode, not a mocked interpreter. */
    unsigned mov[] = {0, 0x0020001b, 0x0836106c, 0x2070f801};
    const uint32_t (*snapshot)[4];unsigned snapshot_count;const float *snapshot_constants;
    assert(!ssx_vertex_snapshot(&snapshot,&snapshot_count,&snapshot_constants));
    assert(!ssx_vertex_active());
    ssx_vertex_method(0x1e94, 6); assert(ssx_vertex_active());
    upload(5, mov, 4);
    assert(ssx_vertex_snapshot(&snapshot,&snapshot_count,&snapshot_constants));
    assert(snapshot_count==1 && !memcmp(snapshot,mov,sizeof mov));
    ssx_vertex_method(0x1e9c,5);ssx_vertex_method(0xb00,mov[0]);
    assert(!ssx_vertex_snapshot(&snapshot,&snapshot_count,&snapshot_constants));
    upload(5,mov,4);
    assert(ssx_vertex_input_mask()==1);
    assert(ssx_vertex_execute(in, out));
    assert(!memcmp(in[0], out[0], sizeof(in[0])));
    /* MOV oPos, c58: viewport scale aliases this constant bank entry. */
    mov[1] = 0x0027401b; mov[2] = 0x0c36106c;
    for (int i = 0; i < 4; ++i) value(0xaf0 + i * 4, 100 + i);
    upload(0, mov, 4); assert(ssx_vertex_execute(in, out));
    assert(ssx_vertex_input_mask()==0);
    for (int i = 0; i < 4; ++i) assert(out[0][i] == 100 + i);
    mov[1] = 0x0027601b; /* c59: viewport offset */
    for (int i = 0; i < 4; ++i) value(0xa20 + i * 4, 200 + i);
    upload(0, mov, 4); assert(ssx_vertex_execute(in, out));
    for (int i = 0; i < 4; ++i) assert(out[0][i] == 200 + i);
    /* Partial constant writes use the method component, not arrival count. */
    ssx_vertex_method(0x1ea4, 59); value(0xb88, 999);
    assert(ssx_vertex_execute(in, out)); assert(out[0][2] == 999);
    assert(out[0][0] == 200);
    assert(ssx_vertex_snapshot(&snapshot,&snapshot_count,&snapshot_constants));
    assert(snapshot_constants[59*4+2]==999);
    /* Context-writing shaders retain private per-vertex state. A later
     * read-only program must still see uploaded constants, not that write. */
    unsigned context_write[] = {
        0,0x0020001b,0x0836106c,0x2070f1d8, /* MOV c59,v0 */
        0,0x0027601b,0x0c36106c,0x2070f801  /* MOV oPos,c59; END */
    };
    float saved_input[16][4];memcpy(saved_input,in,sizeof in);
    upload(0,context_write,8);assert(ssx_vertex_execute(in,out));
    assert(!memcmp(in[0],out[0],sizeof in[0]));
    assert(!memcmp(in,saved_input,sizeof in));
    in[0][0]=71;assert(ssx_vertex_execute(in,out));assert(out[0][0]==71);
    memcpy(in,saved_input,sizeof in);
    upload(0,mov,4);assert(ssx_vertex_execute(in,out));
    assert(out[0][0]==200 && out[0][2]==999);
    ssx_vertex_method(0x1ea0, 136); assert(!ssx_vertex_execute(in, out));
    assert(ssx_vertex_input_mask()==0xffff);
    assert(!ssx_vertex_snapshot(&snapshot,&snapshot_count,&snapshot_constants));
    mov[3] &= ~1u; upload(135, mov, 4); assert(!ssx_vertex_execute(in, out));
    assert(!ssx_vertex_snapshot(&snapshot,&snapshot_count,&snapshot_constants));
    ssx_vertex_method(0x1e9c, 0xffffffff); ssx_vertex_method(0xb00, 0);
    ssx_vertex_method(0x1e9c, 0x40000000); ssx_vertex_method(0xb00, 0);
    mov[1] = 15u << 21; mov[3] |= 1; upload(0, mov, 4);
    assert(!ssx_vertex_execute(in, out)); /* reserved opcode must not reach decoder */
    /* Replacing an invalid program must invalidate the cached rejection. */
    unsigned relative[] = {
        0, 0x01a00000, 0x0836106c, 0x20700ff8, /* ARL A0, v0.x */
        0, 0x0027601b, 0x0c36106c, 0x2070f803  /* MOV oPos, c[A0+59]; END */
    };
    upload(0, relative, 8);
    in[0][0] = 0;
    assert(ssx_vertex_execute(in, out)); assert(out[0][2] == 999);
    /* Reusing validated code must still check each vertex's relative index. */
    in[0][0] = 133; assert(!ssx_vertex_execute(in, out));
    in[0][0] = -60; assert(!ssx_vertex_execute(in, out));
    in[0][0] = 0; assert(ssx_vertex_execute(in, out));
    /* A partial upload invalidates a previously accepted instruction. */
    ssx_vertex_method(0x1e9c, 0); ssx_vertex_method(0xb04, 15u << 21);
    assert(!ssx_vertex_execute(in, out));
    return 0;
}
