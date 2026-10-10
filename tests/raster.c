/* Exercise the real pushbuffer interpreter, including its static raster path. */
#define xbox_GetMemoryOffset raster_test_memory_offset
#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"
#undef xbox_GetMemoryOffset
#include <assert.h>
static ptrdiff_t test_memory;
ptrdiff_t raster_test_memory_offset(void) { return test_memory; }
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
extern ptrdiff_t g_xbox_mem_offset;
int main(void)
{
    unsigned char *ram = calloc(1, 4 * 1024 * 1024);
    s_render_reg[0x2c0/4]=s_render_reg[0x2e0/4]=0x0fff0000;
    assert(ram); test_memory = g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)ram;
    /* Rectangular swizzled chains continue shrinking along their long axis;
     * compressed sub-4x4 levels still occupy a complete DXT block. */
    Texture mip={.offset=0x10000,.width=8,.height=2,.color=6,.valid=1,.addr_u=3,.addr_v=3};
    assert(texture_level_bytes(&mip)==64);
    Texture next=mip;texture_next_level(&next);
    assert(next.offset==0x10040 && next.width==4 && next.height==1);
    ((uint32_t *)(ram+next.offset))[swizzle_offset(3,0,4,1)]=0xff123456;
    uint32_t mip_color=0;assert(gl_sample_texture_level(&mip,1,3,0,&mip_color) && mip_color==0xff123456);
    mip.color=0x0c;mip.width=mip.height=4;
    assert(texture_level_bytes(&mip)==8);texture_next_level(&mip);
    assert(mip.width==2 && mip.height==2 && texture_level_bytes(&mip)==8);
    *(uint16_t *)(ram+mip.offset)=0xf800;
    assert(gl_sample_texture_level(&mip,0,0,0,&mip_color) && mip_color==0xffff0000);
    nv2a_pb_exec_method(0, 0x17fc, 6);
    nv2a_pb_exec_method(0, 0x1810, 0x03000010);
    assert(s_gpu.idx_count == 4 && s_gpu.idx[0] == 16 && s_gpu.idx[3] == 19);
    nv2a_pb_exec_method(0, 0x1808, 23);
    assert(s_gpu.idx_count == 5 && s_gpu.idx[4] == 23);
    assert(NV_PRIM_TRIANGLES == 5 && NV_PRIM_TRIANGLE_STRIP == 6 && NV_PRIM_QUADS == 8);
    s_gpu.idx_count = 0; s_gpu.prim = 0;
    s_gpu.clip_w = s_gpu.clip_h = 16; s_gpu.pitch = 64;
    s_gpu.color_offset = 0x300000; s_gpu.depth_offset = 0x310000; s_gpu.depth_pitch = 64;
    s_gpu.depth_clear = 0xffffff00; s_gpu.clear_color = 0xff000000;
    clear_surface(0xf1);
    s_gpu.depth_enable = s_gpu.depth_write = 1; s_gpu.depth_func = 0x201;
    const float a[4] = {1,1,100,1}, b[4] = {14,1,100,1}, c[4] = {1,14,100,1};
    raster_triangle(a,b,c,0xffff0000,NULL);
    uint32_t *pixels = (uint32_t *)(ram + dma_resolve(s_gpu.color_offset));
    assert(pixels[2*16+2] == 0xffff0000);
    float far_a[4]={1,1,200,1}, far_b[4]={14,1,200,1}, far_c[4]={1,14,200,1};
    raster_triangle(far_a,far_b,far_c,0xff00ff00,NULL);
    assert(pixels[2*16+2] == 0xffff0000); /* occluded */
    s_gpu.depth_enable = 0; s_gpu.alpha_enable = 1; s_gpu.alpha_func = 0x204; s_gpu.alpha_ref = 2;
    raster_triangle(a,b,c,0x0000ff00,NULL);
    assert(pixels[2*16+2] == 0xffff0000); /* alpha test rejects transparent texels */
    s_gpu.blend_enable=1; s_gpu.blend_src=0x302; s_gpu.blend_dst=0x303;
    raster_triangle(a,b,c,0x800000ff,NULL);
    assert((pixels[2*16+2] & 0xffffff) == 0x7f0080);
    VertexAttr attr = {0x1000, 5, 2, 4};
    int16_t pair[2] = {-123,456}; memcpy(ram+0x1000,pair,4);
    float value[4]; assert(fetch_attr(&attr,0,value) && value[0]==-123 && value[1]==456);
    attr.type=1; assert(fetch_attr(&attr,0,value) && value[0]<0 && value[1]>0 && value[1]<1);
    /* Zero-stride arrays broadcast constants to every vertex. They are not
     * disabled: the captured terrain matrix uses float4 arrays this way. */
    float matrix_row[4]={2,3,4,500};memcpy(ram+0x1100,matrix_row,16);
    VertexAttr uniform={0x1100,2,4,0};
    assert(fetch_attr(&uniform,0,value) && !memcmp(value,matrix_row,16));
    assert(fetch_attr(&uniform,32767,value) && !memcmp(value,matrix_row,16));
    uniform.size=0;assert(!fetch_attr(&uniform,0,value));
    /* Inline decoding must fit the entire attribute in the submitted
     * payload, not consume stale bytes beyond its last dword. Also accept
     * narrow attributes ending exactly at the payload boundary. */
    s_gpu.inline_active=1;s_gpu.inline_count=4;
    memset(s_gpu.inline_buf,0,sizeof s_gpu.inline_buf);
    const unsigned types[]={2,0,1,4,5,6};
    for(unsigned t=0;t<sizeof types/sizeof types[0];t++)for(unsigned n=1;n<=5;n++) {
        unsigned lanes=n<4?n:4;
        unsigned bytes=types[t]==0 || types[t]==6?4:
            types[t]==2?lanes*4:types[t]==4?lanes:lanes*2;
        VertexAttr inlined={16-bytes,types[t],n,0};
        assert(fetch_attr(&inlined,99,value));
        inlined.offset++;
        assert(!fetch_attr(&inlined,99,value));
        assert(value[0]==0 && value[1]==0 && value[2]==0 && value[3]==1);
    }
    s_gpu.inline_active=0;s_gpu.inline_count=0;
    /* Float attributes preserve payload bits and leave absent lanes at
     * (0,0,0,1), including non-finite values and nonaligned source data. */
    const uint32_t float_bits[4]={0x80000000,0x7fc01234,0x7f800000,0xff800000};
    memcpy(ram+0x12001,float_bits,16);
    for(unsigned n=1;n<=5;n++) {
        VertexAttr floats={0x12001,2,n,0};
        uint32_t expected[4]={0,0,0,0x3f800000};
        memcpy(expected,float_bits,(n<4?n:4)*4);
        assert(fetch_attr(&floats,42,value) && !memcmp(value,expected,16));
    }

    /* Sparse shader input assembly preserves payloads/fallbacks, refreshes
     * varying vertices, and clears lanes no longer consumed by a new batch.
     * In particular, an unused NaN must not leak into the native finite-input
     * guard after switching to a shader with a narrower input mask. */
    VertexAttr saved_input2=s_gpu.attr[2],saved_input5=s_gpu.attr[5];
    float saved_imm5[4];memcpy(saved_imm5,s_gpu.imm_attr[5],16);
    const float second_input[4]={1.5f,-2,3.25f,0};
    memcpy(ram+0x13001,float_bits,16);memcpy(ram+0x13011,second_input,16);
    s_gpu.attr[2]=(VertexAttr){0x13001,2,4,16};
    s_gpu.attr[5]=(VertexAttr){0,2,0,0};
    memcpy(s_gpu.imm_attr[5],float_bits,16);
    s_vertex_inputs=(1u<<2)|(1u<<5);++s_vertex_generation;
    float expected_inputs[16][4]={{0}};
    memcpy(expected_inputs[2],float_bits,16);memcpy(expected_inputs[5],float_bits,16);
    assert(!memcmp(fetch_shader_vertex_inputs(0),expected_inputs,sizeof expected_inputs));
    memcpy(expected_inputs[2],second_input,16);
    assert(!memcmp(fetch_shader_vertex_inputs(1),expected_inputs,sizeof expected_inputs));
    s_vertex_inputs=1u<<2;++s_vertex_generation;
    memset(expected_inputs[5],0,16);
    assert(!memcmp(fetch_shader_vertex_inputs(1),expected_inputs,sizeof expected_inputs));
    s_gpu.attr[2].stride=0;++s_vertex_generation;
    memcpy(expected_inputs[2],float_bits,16);
    assert(!memcmp(fetch_shader_vertex_inputs(99),expected_inputs,sizeof expected_inputs));
    /* Both kinds of constant input refresh on the next batch. Disabled
     * arrays remain constant even when their old stride is nonzero. */
    memcpy(ram+0x13001,second_input,16);
    s_gpu.attr[5].stride=32;
    memcpy(s_gpu.imm_attr[5],second_input,16);
    s_vertex_inputs|=1u<<5;++s_vertex_generation;
    memcpy(expected_inputs[2],second_input,16);
    memcpy(expected_inputs[5],second_input,16);
    assert(!memcmp(fetch_shader_vertex_inputs(0),expected_inputs,sizeof expected_inputs));
    assert(!memcmp(fetch_shader_vertex_inputs(32767),expected_inputs,sizeof expected_inputs));
    /* The varying float fast path must match decoding for every format,
     * absent array and short vector, including raw exceptional float bits. */
    uint32_t input_seed=0x204feed;
    for(unsigned k=0;k<256;k++) {
        input_seed=1664525*input_seed+1013904223;
        ram[0x14000+k]=input_seed>>24;
    }
    for(unsigned type=0;type<8;type++)for(unsigned size=0;size<6;size++)
    for(unsigned stride=0;stride<=32;stride+=16)for(unsigned missing=0;missing<2;missing++) {
        s_gpu.attr[2]=(VertexAttr){missing?0:0x14001,type,size,stride};
        s_vertex_inputs=1u<<2;++s_vertex_generation;
        for(unsigned index=0;index<4;index++) {
            float expected[16][4]={{0}};
            if(!fetch_attr(&s_gpu.attr[2],index,expected[2]))
                memcpy(expected[2],s_gpu.imm_attr[2],16);
            assert(!memcmp(fetch_shader_vertex_inputs(index),expected,sizeof expected));
        }
    }
    /* Short float arrays preserve exceptional payload bits and reset the
     * missing lanes even after a prior four-component array populated them. */
    const uint32_t short_patterns[]={0x80000000u,0x00000001u,0x7f800000u,
        0xff800000u,0x7fc12345u,0x7f812345u,0x7f7fffffu};
    for(unsigned pattern=0;pattern<sizeof short_patterns/sizeof short_patterns[0];pattern++) {
        uint32_t source_bits[4]={short_patterns[pattern],0x80000000u,0x00000001u,0xdeadbeefu};
        memcpy(ram+0x14001,source_bits,sizeof source_bits);
        for(unsigned size=1;size<=3;size++) {
            s_gpu.attr[2]=(VertexAttr){0x14001,2,4,16};
            s_vertex_inputs=1u<<2;++s_vertex_generation;
            fetch_shader_vertex_inputs(0);
            s_gpu.attr[2].size=size;++s_vertex_generation;
            uint32_t expected_bits[4]={0,0,0,0x3f800000u};
            memcpy(expected_bits,source_bits,size*4);
            const float (*bank)[4]=fetch_shader_vertex_inputs(0);
            assert(!memcmp(bank[2],expected_bits,16));
        }
    }
    /* Inline offset zero is legal; a nonzero offset must still use payload
     * memory, and a truncated final element must use the immediate fallback. */
    for(unsigned offset=0;offset<=4;offset+=4) {
        s_gpu.inline_active=1;s_gpu.inline_count=(offset+32)/4;
        memcpy((uint8_t *)s_gpu.inline_buf+offset,ram+0x14001,32);
        s_gpu.attr[2]=(VertexAttr){offset,2,4,16};
        s_vertex_inputs=1u<<2;++s_vertex_generation;
        for(unsigned index=0;index<3;index++) {
            float expected[16][4]={{0}};
            if(!fetch_attr(&s_gpu.attr[2],index,expected[2]))
                memcpy(expected[2],s_gpu.imm_attr[2],16);
            assert(!memcmp(fetch_shader_vertex_inputs(index),expected,sizeof expected));
        }
    }
    s_gpu.inline_active=0;s_gpu.inline_count=0;
    s_gpu.attr[2]=saved_input2;s_gpu.attr[5]=saved_input5;
    memcpy(s_gpu.imm_attr[5],saved_imm5,16);s_vertex_inputs=0xffff;++s_vertex_generation;

    assert(wrap_coord(-1, 16, 1) == 15 && wrap_coord(-17,16,1)==15);
    assert(wrap_coord(-1,16,2)==0 && wrap_coord(16,16,2)==15);
    assert(wrap_coord(-1,16,3)==0 && wrap_coord(16,16,3)==15);
    s_gpu.alpha_enable=s_gpu.blend_enable=0;
    s_gpu.tex=(Texture){.offset=0x2000,.width=16,.height=1,.pitch=64,.color=0x12,.valid=1,.addr_u=3,.addr_v=3};
    for(int i=0;i<16;++i) ((uint32_t*)(ram+0x2000))[i]=0xff000000u|i;
    float pa[4]={0,0,0,1},pb[4]={16,0,0,4},pc[4]={0,16,0,1};
    float uv[3][2]={{0,0},{16,0},{0,0}};
    raster_triangle(pa,pb,pc,0xffffffff,uv);
    assert(pixels[2*16+4]==0xff000001u); /* perspective gives texel 1, affine gives 4 */
    pb[3]=1; raster_triangle(pa,pb,pc,0xffffffff,uv);
    assert(pixels[2*16+4]==0xff000004u);
    /* An enabled second texture must work even when stage zero is disabled.
     * The final combiner selects t1 directly. */
    s_triangle_colors=1;
    memset(s_triangle_tex,0,sizeof s_triangle_tex);
    for(int i=0;i<3;i++) for(int st=0;st<4;st++) s_triangle_tex[i][st][3]=1;
    s_triangle_tex[1][1][0]=16;
    s_extra_texture[1]=s_gpu.tex;
    s_render_reg[0x1e60/4]=1;
    s_render_reg[0x1e70/4]=1<<5;
    s_render_reg[0x288/4]=9;
    s_render_reg[0x28c/4]=0x1900;
    raster_triangle(pa,pb,pc,0xffffffff,uv);
    assert(pixels[2*16+4]==0xff000004u);
    s_render_reg[0x1e70/4]=4<<15; /* stage-three pass-through */
    s_render_reg[0x288/4]=11;
    s_render_reg[0x28c/4]=0x1b00;
    for(int i=0;i<3;i++) { s_triangle_tex[i][3][0]=1;s_triangle_tex[i][3][2]=0.5f; }
    raster_triangle(pa,pb,pc,0xffffffff,uv);
    assert(pixels[2*16+4]==0xffff0080u);
    /* Diffuse and specular RGBA use perspective weights, just like texture
     * coordinates. At (2.5,2.5), normalized weights are80:3:6. */
    float perspective_pos[3][4]={{1,1,0,1},{14,1,0,4},{1,14,0,2}};
    const float perspective_diffuse[3][4]={{1,0,0,1},{0,1,0,.5f},{0,0,1,0}};
    const float perspective_specular[3][4]={{0,0,1,.25f},{1,0,0,.75f},{0,1,0,1}};
    for(unsigned i=0;i<3;i++) {
        memcpy(s_triangle_color[i][0],perspective_diffuse[i],16);
        memcpy(s_triangle_color[i][1],perspective_specular[i],16);
    }
    memset(s_render_reg,0,sizeof s_render_reg);s_render_reg[0x2c0/4]=s_render_reg[0x2e0/4]=0x0fff0000;s_render_reg[0x1e60/4]=1;
    s_render_reg[0x288/4]=4;s_render_reg[0x28c/4]=0x1400;
    raster_triangle(perspective_pos[0],perspective_pos[1],perspective_pos[2],0,NULL);
    assert(pixels[34]==0xeae50911);
    s_render_reg[0x288/4]=5;s_render_reg[0x28c/4]=0x1500;
    raster_triangle(perspective_pos[0],perspective_pos[1],perspective_pos[2],0,NULL);
    assert(pixels[34]==0x510911e5);
    /* The post-transform cache must not retain guest vertex edits across
     * draw batches, even when the same indices and shader are reused. */
    memset(s_render_reg,0,sizeof s_render_reg);s_render_reg[0x2c0/4]=s_render_reg[0x2e0/4]=0x0fff0000;
    unsigned mov[4]={0,0x0020001b,0x0836106c,0x2070f801};
    ssx_vertex_method(0x1e94,6);ssx_vertex_method(0x1e9c,0);ssx_vertex_method(0x1ea0,0);
    for(int i=0;i<4;i++)ssx_vertex_method(0xb00+i*4,mov[i]);
    float verts[3][4]={{1,1,0,1},{14,1,0,1},{1,14,0,1}};
    memcpy(ram+0x1000,verts,sizeof verts);
    s_gpu.attr[0]=(VertexAttr){0x1000,2,4,16};
    s_gpu.idx_count=3;s_gpu.idx[0]=0;s_gpu.idx[1]=1;s_gpu.idx[2]=2;s_gpu.prim=5;
    raster_batch(); assert(s_vertex_cache[0].out[0][0]==1);
    *(float*)(ram+0x1000)=3;
    raster_batch(); assert(s_vertex_cache[0].out[0][0]==3);
    /* Clip metadata is also batch-local: viewport/depth edits must be seen
     * even when vertex positions and indices have not changed. */
    assert(s_vertex_cache[0].clip.code==0);
    s_gpu.clip_x=8;
    raster_batch();assert(s_vertex_cache[0].clip.code&2);
    s_gpu.clip_x=0;
    raster_batch();assert(s_vertex_cache[0].clip.code==0);
    *(float*)(ram+0x1008)=2;
    float clip_limit=1;memcpy(&s_render_reg[0x398/4],&clip_limit,4);
    raster_batch();assert(s_vertex_cache[0].clip.code&64);
    clip_limit=3;memcpy(&s_render_reg[0x398/4],&clip_limit,4);
    raster_batch();assert(!(s_vertex_cache[0].clip.code&64));
    *(float*)(ram+0x1008)=0;s_render_reg[0x398/4]=0;
    ShaderClipVertex invalid_clip;
    float invalid_position[4]={NAN,1,0,1};
    prepare_shader_clip_vertex(invalid_position,&invalid_clip);
    assert(!invalid_clip.valid);
    /* Broadcast and disabled inputs must refresh on every batch, including
     * generation wrap; inline broadcasts follow the same rule. */
    s_gpu.attr[0].stride=0;
    raster_batch();assert(s_vertex_cache[2].out[0][0]==3);
    *(float*)(ram+0x1000)=5;
    raster_batch();assert(s_vertex_cache[2].out[0][0]==5);
    s_gpu.attr[0].size=0;memcpy(s_gpu.imm_attr[0],verts[0],16);
    raster_batch();assert(s_vertex_cache[2].out[0][0]==1);
    s_gpu.imm_attr[0][0]=7;s_vertex_generation=UINT32_MAX;
    raster_batch();assert(s_vertex_cache[2].out[0][0]==7);
    assert(s_vertex_cache[2].clip.homogeneous[0]==7);
    s_gpu.inline_active=1;s_gpu.inline_count=4;
    s_gpu.attr[0]=(VertexAttr){0,2,4,0};memcpy(s_gpu.inline_buf,verts[1],16);
    raster_batch();assert(s_vertex_cache[2].out[0][0]==14);
    float inline_changed=9;memcpy(s_gpu.inline_buf,&inline_changed,4);
    raster_batch();assert(s_vertex_cache[2].out[0][0]==9);
    s_gpu.inline_active=0;s_gpu.inline_count=0;
    s_gpu.attr[0]=(VertexAttr){0x1000,2,4,16};
    s_gpu.attr[11]=(VertexAttr){0x1100,2,4,0};
    draw_primitive();
    assert(!memcmp(s_gpu.imm_attr[11],matrix_row,16));
    assert(!memcmp(s_gpu.imm_attr[0],verts[2],16));
    s_gpu.attr[11].size=0;*(float*)(ram+0x1100)=999;
    draw_primitive();assert(!memcmp(s_gpu.imm_attr[11],matrix_row,16));
    s_skip_frame=1;s_gpu.attr[11].size=4;
    draw_primitive();assert(s_gpu.imm_attr[11][0]==999);
    s_skip_frame=0;

    s_triangle_colors=0;memset(s_render_reg,0,sizeof s_render_reg);s_render_reg[0x2c0/4]=s_render_reg[0x2e0/4]=0x0fff0000;
    nv2a_pb_exec_method(0,0x358,0);
    uint32_t old=pixels[2*16+2];
    raster_triangle(a,b,c,0xffffffff,NULL);assert(pixels[2*16+2]==old);
    nv2a_pb_exec_method(0,0x358,0x100);
    raster_triangle(a,b,c,0xffffffff,NULL);assert(pixels[2*16+2]==((old&~0xff00u)|0xff00));
    nv2a_pb_exec_method(0,0x358,0x1010101);
    /* Back-face culling obeys winding, including front-and-back rejection. */
    nv2a_pb_exec_method(0,0x308,1);
    nv2a_pb_exec_method(0,0x3a0,0x901);
    nv2a_pb_exec_method(0,0x39c,0x405);
    pixels[2*16+2]=0;
    raster_triangle(a,c,b,0xff112233,NULL);assert(pixels[2*16+2]==0);
    raster_triangle(a,b,c,0xff112233,NULL);assert(pixels[2*16+2]==0xff112233);
    nv2a_pb_exec_method(0,0x39c,0x408);
    raster_triangle(a,b,c,0xffffffff,NULL);assert(pixels[2*16+2]==0xff112233);
    /* Adjacent strip triangles have alternating index order but the same
     * facing. Both halves of this square must survive back-face culling. */
    nv2a_pb_exec_method(0,0x39c,0x405);
    float strip[4][4]={{1,1,0,1},{14,1,0,1},{1,14,0,1},{14,14,0,1}};
    memcpy(ram+0x1000,strip,sizeof strip);
    s_gpu.idx_count=4;for(int i=0;i<4;i++)s_gpu.idx[i]=i;s_gpu.prim=6;
    s_gpu.tex.valid=0;
    pixels[2*16+2]=pixels[12*16+12]=0;
    raster_batch();
    assert(pixels[2*16+2]!=0 && pixels[12*16+12]!=0);
    nv2a_pb_exec_method(0,0x308,0);
    /* A triangle crossing W=0 must be clipped, not projected across the
     * viewport or discarded wholesale. Here its visible portion is at y<1. */
    s_gpu.depth_enable=0;s_gpu.alpha_enable=0;s_gpu.blend_enable=0;s_gpu.tex.valid=0;
    s_triangle_colors=0;
    float clipped[3][4]={{1,1,0,1},{14,1,0,1},{1,14,0,-1}};
    pixels[2*16+2]=0;pixels[8]=0;
    raster_shader_triangle(clipped,0xff123456);
    assert(pixels[2*16+2]==0 && pixels[8]==0xff123456);
    for(int v=0;v<3;v++)clipped[v][3]=-1;
    raster_shader_triangle(clipped,0xffffffff);
    assert(pixels[2*16+2]==0 && pixels[8]==0xff123456);
    /* Clip near/far geometry before depth testing, including when depth is
     * disabled. Crossing the near plane retains only its visible portion. */
    float depth_clipped[3][4]={{1,1,-1,1},{14,1,1,1},{1,14,1,1}};
    float far_clip=10;memcpy(&s_render_reg[0x398/4],&far_clip,4);
    pixels[2*16+2]=pixels[2*16+8]=0;
    raster_shader_triangle(depth_clipped,0xffabcdef);
    assert(pixels[2*16+2]==0 && pixels[2*16+8]==0xffabcdef);
    for(int v=0;v<3;v++)depth_clipped[v][2]=11;
    raster_shader_triangle(depth_clipped,0xffffffff);
    assert(pixels[2*16+2]==0 && pixels[2*16+8]==0xffabcdef);
    /* Only the left plane changes this polygon. Pass-through planes must
     * preserve its ordering and all 24 interpolated color/texture lanes. */
    float left_clipped[3][4]={{-4,4,2,1},{12,4,2,1},{4,12,2,1}};
    s_triangle_colors=1;s_gl_collect=1;s_gl_count=0;
    for(int v=0;v<3;v++) {
        float varying[24];
        for(int k=0;k<24;k++)
            varying[k]=(left_clipped[v][0]+2*left_clipped[v][1]+k)/64;
        memcpy(s_triangle_color[v],varying,32);
        memcpy(s_triangle_tex[v],varying+8,64);
    }
    raster_shader_triangle(left_clipped,0xffffffff);
    const float expected_xy[6][2]={{0,4},{12,4},{4,12},{0,4},{4,12},{0,8}};
    assert(s_gl_count==6);
    for(int v=0;v<6;v++) {
        SsxGlVertex expected={0};
        expected.position[0]=expected_xy[v][0];expected.position[1]=expected_xy[v][1];
        expected.position[2]=2;expected.position[3]=1;
        float varying[24];
        for(int k=0;k<24;k++)varying[k]=(expected_xy[v][0]+2*expected_xy[v][1]+k)/64;
        memcpy(expected.diffuse,varying,16);memcpy(expected.specular,varying+4,16);
        memcpy(expected.tex,varying+8,64);
        assert(!memcmp(&s_gl_vertices[v],&expected,sizeof expected));
    }
    /* No original plane rejects all vertices, but clipping the corner must
     * still reject the polygon after an earlier plane creates new edges. */
    float outside_corner[3][4]={{-10,1,2,1},{-1,-10,2,1},{1,-10,2,1}};
    s_gl_count=0;raster_shader_triangle(outside_corner,0xffffffff);
    assert(s_gl_count==0);s_gl_collect=0;s_triangle_colors=0;
    /* The CPU fallback counts only fragments surviving alpha/depth. */
    nv2a_pb_exec_method(0,0x17c8,1);nv2a_pb_exec_method(0,0x17cc,1);
    s_gpu.depth_enable=0;s_gpu.alpha_enable=0;s_color_write_mask=0;
    fragment(ram,4,2,2,1,0xffffffff);assert(s_zpass_count==1);
    s_gpu.alpha_enable=1;s_gpu.alpha_func=0x200;
    fragment(ram,4,2,2,1,0xffffffff);assert(s_zpass_count==1);
    s_gpu.alpha_enable=0;s_gpu.depth_enable=1;s_gpu.depth_func=0x200;
    fragment(ram,4,2,2,1,0xffffffff);assert(s_zpass_count==1);
    nv2a_pb_exec_method(0,0x17cc,0);s_gpu.depth_enable=0;
    fragment(ram,4,2,2,1,0xffffffff);assert(s_zpass_count==1);
    nv2a_pb_exec_method(0,0x17c8,1);assert(s_zpass_count==0);
    s_color_write_mask=0xffffffff;
    s_render_every=2;s_render_frame=0;
    s_gpu.clear_color=0xff123456;clear_surface(0xf0);
    assert(pixels[0]==0xff123456);
    s_gpu.clear_color=0xffabcdef;clear_surface(0xf0);
    assert(s_skip_frame && pixels[0]==0xff123456);
    unsigned generation=s_vertex_generation;raster_batch();assert(s_vertex_generation==generation);
    clear_surface(0xf0);assert(!s_skip_frame && pixels[0]==0xffabcdef);
    /* A shader's output color replaces the diffuse input. The GL path keeps
     * its full float varyings; the CPU legacy path still packs that output. */
    unsigned mov_position[]={0,0x0020001b,0x0836106c,0x2070f801};
    ssx_vertex_method(0x1e94,6);ssx_vertex_method(0x1e9c,0);
    for(unsigned i=0;i<4;i++)ssx_vertex_method(0xb00+i*4,mov_position[i]);
    ssx_vertex_method(0x1ea0,0);
    float positions[3][4]={{1,1,1,1},{14,1,1,1},{1,14,1,1}};
    float input_colors[3][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}};
    memcpy(ram+0x10000,positions,sizeof positions);
    memcpy(ram+0x11000,input_colors,sizeof input_colors);
    memset(s_render_reg,0,sizeof s_render_reg);s_render_reg[0x2c0/4]=s_render_reg[0x2e0/4]=0x0fff0000;s_gpu.tex=(Texture){0};
    s_gpu.attr[0]=(VertexAttr){0x10000,2,4,16};
    s_gpu.attr[3]=(VertexAttr){0x11000,2,4,16};
    s_vertex_inputs=ssx_vertex_input_mask();s_vertex_generation++;
    s_gl_collect=0;s_gpu.alpha_enable=s_gpu.blend_enable=s_gpu.depth_enable=0;
    memset(pixels,0,16*16*4);
    raster_indexed(0,1,2,vertex_color(0));assert(pixels[34]==0xff000000);
    s_gl_collect=1;s_gl_count=0;
    raster_indexed(0,1,2,vertex_color(0));assert(s_gl_count==3);
    const float output_color[4]={0,0,0,1};
    for(unsigned i=0;i<3;i++)assert(!memcmp(s_gl_vertices[i].diffuse,output_color,16));
    /* Shader colors saturate before interpolation, including NaN -> one.
     * Keep the shader executor itself raw, then check both GL submission and
     * the CPU fragment gradient rather than only a packed flat color. */
    unsigned color_program[]={0,0x0020001b,0x0836106c,0x2070f800,
        0,0x0020061b,0x0836106c,0x2070f818,
        0,0x0020081b,0x0836106c,0x2070f821};
    ssx_vertex_method(0x1e9c,0);
    for(unsigned i=0;i<12;i++)ssx_vertex_method(0xb00+i*4,color_program[i]);
    float excess[3][4]={{2,-1,.25f,2},{-1,2,.5f,-1},{NAN,INFINITY,-INFINITY,.5f}};
    const float saturated[3][4]={{1,0,.25f,1},{0,1,.5f,0},{1,1,0,.5f}};
    memcpy(ram+0x11000,excess,sizeof excess);
    s_gpu.attr[4]=s_gpu.attr[3];
    float raw_in[16][4]={{0}},raw_out[13][4];
    memcpy(raw_in[0],positions[0],16);memcpy(raw_in[3],excess[0],16);memcpy(raw_in[4],excess[0],16);
    assert(ssx_vertex_execute(raw_in,raw_out) && raw_out[3][0]==2 && raw_out[4][1]==-1);
    s_vertex_inputs=ssx_vertex_input_mask();s_vertex_generation++;
    s_gl_collect=1;s_gl_count=0;raster_indexed(0,1,2,vertex_color(0));
    assert(s_gl_count==3);
    for(unsigned i=0;i<3;i++) {
        assert(!memcmp(s_gl_vertices[i].diffuse,saturated[i],16));
        assert(!memcmp(s_gl_vertices[i].specular,saturated[i],16));
    }
    s_gl_collect=0;s_render_reg[0x1e60/4]=1;
    s_render_reg[0x288/4]=4;s_render_reg[0x28c/4]=0x1400;
    memset(pixels,0,16*16*4);raster_indexed(0,1,2,vertex_color(0));
    assert(pixels[34]==0xd3e23b40);
    memset(s_render_reg,0,sizeof s_render_reg);s_render_reg[0x2c0/4]=s_render_reg[0x2e0/4]=0x0fff0000;
    s_gl_collect=0;
    /* Cinematic letterbox clears must preserve the scene between the bars.
     * CLEAR_RECT bounds are inclusive, independently of surface dimensions. */
    s_render_every=1;s_skip_frame=0;
    for(unsigned i=0;i<256;i++)pixels[i]=0xff123456;
    nv2a_pb_exec_method(0,0x1d98,15u<<16);
    nv2a_pb_exec_method(0,0x1d9c,1u<<16);
    nv2a_pb_exec_method(0,0x1d90,0xff000000);
    nv2a_pb_exec_method(0,0x1d94,0xf0);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)
        assert(pixels[y*16+x]==(y<2?0xff000000:0xff123456));
    nv2a_pb_exec_method(0,0x1d9c,(15u<<16)|14);
    nv2a_pb_exec_method(0,0x1d94,0xf0);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)
        assert(pixels[y*16+x]==(y<2 || y>=14?0xff000000:0xff123456));
    nv2a_pb_exec_method(0,0x1d98,(3u<<16)|3);
    nv2a_pb_exec_method(0,0x1d9c,(7u<<16)|7);
    nv2a_pb_exec_method(0,0x1d90,0xffabcdef);
    nv2a_pb_exec_method(0,0x1d94,0xf0);
    assert(pixels[7*16+3]==0xffabcdef && pixels[7*16+2]==0xff123456 && pixels[8*16+3]==0xff123456);
    nv2a_pb_exec_method(0,0x1d98,(2u<<16)|3); /* inverted: empty */
    nv2a_pb_exec_method(0,0x1d90,0xffeeeeee);
    nv2a_pb_exec_method(0,0x1d94,0xf0);
    assert(pixels[7*16+3]==0xffabcdef);
    uint32_t *zrect=(uint32_t *)(ram+dma_resolve(s_gpu.depth_offset));
    for(unsigned i=0;i<256;i++)zrect[i]=0x123456aa;
    s_gpu.depth_clear=0x654321bb;
    nv2a_pb_exec_method(0,0x1d98,(3u<<16)|3);
    nv2a_pb_exec_method(0,0x1d94,1);
    for(unsigned i=0;i<256;i++)assert(zrect[i]==(i==7*16+3?0x654321aa:0x123456aa));
    assert(pixels[7*16+3]==0xffabcdef); /* depth-only leaves color intact */
    nv2a_pb_exec_method(0,0x1d94,2); /* stencil-only preserves depth */
    assert(zrect[7*16+3]==0x654321bb && zrect[7*16+2]==0x123456aa);
    nv2a_pb_exec_method(0,0x1d8c,0x11223344);
    nv2a_pb_exec_method(0,0x1d94,3);
    assert(zrect[7*16+3]==0x11223344 && pixels[7*16+3]==0xffabcdef);
    nv2a_pb_exec_method(0,0x1d90,0x12345678);
    const uint32_t channel_values[]={0xff34cdef,0xff3456ef,0xff345678,0x12345678};
    for(unsigned c=0;c<4;c++) {
        nv2a_pb_exec_method(0,0x1d94,0x10u<<c);
        assert(pixels[7*16+3]==channel_values[c]);
        assert(pixels[7*16+2]==0xff123456 && zrect[7*16+3]==0x11223344);
    }
    /* Clamp oversized rectangles to the target, including the RGB565 path. */
    s_gpu.pitch=32;
    uint16_t *small=(uint16_t *)pixels;
    for(unsigned i=0;i<258;i++)small[i]=0x1234;
    nv2a_pb_exec_method(0,0x1d98,(100u<<16)|14);
    nv2a_pb_exec_method(0,0x1d9c,(100u<<16)|14);
    nv2a_pb_exec_method(0,0x1d90,0xffff0000);
    nv2a_pb_exec_method(0,0x1d94,0xf0);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)
        assert(small[y*16+x]==(x>=14 && y>=14?0xf800:0x1234));
    assert(small[256]==0x1234 && small[257]==0x1234);
    nv2a_pb_exec_method(0,0x1d90,0xff00ff00);
    nv2a_pb_exec_method(0,0x1d94,0x20); /* green only: retain red */
    assert(small[14*16+14]==0xffe0 && small[14*16+13]==0x1234);
    nv2a_pb_exec_method(0,0x1d90,0);
    nv2a_pb_exec_method(0,0x1d94,0x80); /* RGB565 has no alpha channel */
    assert(small[14*16+14]==0xffe0 && small[256]==0x1234);
    s_gpu.pitch=64;
    free(ram); return 0;
}
