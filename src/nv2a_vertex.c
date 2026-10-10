/* Bounded NV2A vertex-program upload and execution for the CPU renderer. */
#include "nv2a_vertex.h"
#include "nv2a_vsh_emulator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SSX_NATIVE_VERTEX
#include "nv2a_vertex_native.h"
static SsxNativeVertexFn native_program;
static int native_dirty=1;
static int native_context_dirty=1,native_context_finite;
/* Classify every lane without an early-exit branch so the compiler can
 * vectorize the common finite-input path. Keep exceptional values on the
 * interpreter path, including values in unused input/output registers. */
static int all_finite(const float *values,unsigned count) {
    int finite=1;
    for(unsigned i=0;i<count;i++)finite &= !!isfinite(values[i]);
    return finite;
}
#endif
static uint32_t words[136][4], load_word, constant_word, start, mode;
static Nv2aVshStep steps[136];
static unsigned char valid[136],checked[136],relative[136],writes_context[136];
static float constants[192 * 4];
static unsigned input_mask_dirty=1,input_mask=0xffff;
int ssx_vertex_active(void) { return (mode & 3) == 2; }
int ssx_vertex_method(uint32_t method, uint32_t value) {
    if(method==0x1ea0 || (method>=0xb00 && method<0xb80))input_mask_dirty=1;
#ifdef SSX_NATIVE_VERTEX
    if(method==0x1ea0 || (method>=0xb00 && method<0xb80))native_dirty=1;
    if((method>=0xa20 && method<0xa30) || (method>=0xaf0 && method<0xb00) ||
       (method>=0xb80 && method<0xc00))native_context_dirty=1;
#endif
    if (method >= 0xa20 && method < 0xa30) {
        memcpy(&constants[59 * 4 + (method - 0xa20) / 4], &value, 4); return 1;
    }
    if (method >= 0xaf0 && method < 0xb00) {
        memcpy(&constants[58 * 4 + (method - 0xaf0) / 4], &value, 4); return 1;
    }
    switch (method) {
    case 0x1e94: mode = value; return 1;
    case 0x1e9c: load_word = value < 136 ? value * 4 : 136 * 4; return 1;
    case 0x1ea0: start = value; return 1;
    case 0x1ea4: constant_word = value < 192 ? value * 4 : 192 * 4; return 1;
    }
    if (method >= 0xb00 && method < 0xb80) {
        if (load_word < 136 * 4) {
            unsigned slot = load_word / 4;
            unsigned component = ((method - 0xb00) / 4) % 4;
            words[slot][component] = value;
            valid[slot] = checked[slot] = 0;
            if (component == 3) {
                valid[slot] = ((words[slot][1] >> 21) & 15) <= 13 &&
                    nv2a_vsh_parse_step(&steps[slot], words[slot]) == NV2AVPR_SUCCESS;
                /* The decoder marks ARL's implicit output with no mask, but
                 * the CPU executor requires X to commit the address value. */
                if (valid[slot] && steps[slot].mac.opcode == NV2AOP_ARL)
                    steps[slot].mac.outputs[0].writemask = NV2AWM_X;
            }
        }
        if (load_word < 136 * 4 && ((method - 0xb00) / 4) % 4 == 3) load_word += 4;
        return 1;
    }
    if (method >= 0xb80 && method < 0xc00) {
        unsigned component = ((method - 0xb80) / 4) % 4;
        if (constant_word < 192 * 4) {
            memcpy(&constants[constant_word + component], &value, 4);
            if (component == 3) constant_word += 4;
        }
        return 1;
    }
    return 0;
}
/* Only fetch vertex attributes that the uploaded program can read. Invalid
 * or unterminated programs retain the conservative full-input path. */
uint32_t ssx_vertex_input_mask(void)
{
    static int fetch_all=-1;
    if(fetch_all<0)fetch_all=getenv("SSX_VSH_FETCH_ALL")!=NULL || getenv("SSX_VSH_TRACE")!=NULL;
    if(fetch_all || !ssx_vertex_active())return 0xffff;
    if(!input_mask_dirty)return input_mask;
    input_mask_dirty=0;input_mask=0xffff;
    unsigned mask=0;
    for(unsigned pc=start;pc<136;pc++) {
        if(!valid[pc])return input_mask;
        const Nv2aVshOperation *ops[2]={&steps[pc].mac,&steps[pc].ilu};
        for(unsigned op=0;op<2;op++) {
            if(ops[op]->opcode==NV2AOP_NOP)continue;
            for(unsigned i=0;i<3;i++) {
                const Nv2aVshInput *in=&ops[op]->inputs[i];
                if(in->type==NV2ART_INPUT) {
                    if(in->index>=16)return input_mask;
                    mask|=1u<<in->index;
                }
            }
        }
        if(steps[pc].is_final){input_mask=mask;break;}
    }
    return input_mask;
}
static int register_ok(Nv2aVshRegisterType type, long index) {
    switch (type) {
    case NV2ART_NONE: return 1;
    case NV2ART_TEMPORARY: case NV2ART_OUTPUT: return index >= 0 && index <= 12;
    case NV2ART_INPUT: return index >= 0 && index < 16;
    case NV2ART_CONTEXT: return index >= 0 && index < 192;
    case NV2ART_ADDRESS: return index == 0;
    }
    return 0;
}
static int operation_ok(const Nv2aVshOperation *op, float address, int static_check) {
    if (op->opcode == NV2AOP_NOP) return 1;
    for (int i = 0; i < 3; ++i) {
        const Nv2aVshInput *in = &op->inputs[i];
        if (in->type == NV2ART_NONE) continue;
        long index = in->index;
        if (in->type == NV2ART_CONTEXT && in->is_relative) {
            if (static_check) continue;
            if (!isfinite(address) || address < -192 || address > 192) return 0;
            index += (int)address;
        }
        if (!register_ok(in->type, index)) return 0;
    }
    for (int i = 0; i < 2; ++i) {
        if (op->outputs[i].type == NV2ART_NONE) continue;
        if (!register_ok(op->outputs[i].type, op->outputs[i].index) ||
            op->outputs[i].type == NV2ART_INPUT ||
            (op->outputs[i].type == NV2ART_TEMPORARY && op->outputs[i].index == 12)) return 0;
    }
    return 1;
}
/* Copy one complete bank instead of clearing it and revisiting every W lane.
 * Keep the same positive zeros and unit W for native and interpreter paths. */
static const float initial_output[13*4] = {
    [3]=1, [7]=1, [11]=1, [15]=1, [19]=1, [23]=1, [27]=1,
    [31]=1, [35]=1, [39]=1, [43]=1, [47]=1, [51]=1
};
int ssx_vertex_execute(const float input[16][4], float output[13][4]) {
    Nv2aVshCPUFullExecutionState full;
    /* Inputs are validated read-only. Constants normally are too: avoid
     * clearing/copying the 3 KiB context bank for every transformed vertex.
     * A program writing c[] gets a private copy immediately before its first
     * write, retaining the interpreter's existing per-vertex semantics. */
    memcpy(full.output_regs,initial_output,sizeof full.output_regs);
    Nv2aVshExecutionState state = {(float *)input,full.output_regs,
        full.temp_regs,constants,full.address_reg,NULL};
    static int trace,trace_enabled=-1;
    if(trace_enabled<0)trace_enabled=getenv("SSX_VSH_TRACE")!=NULL;
    int tracing = trace_enabled && trace++ == 0;
#ifdef SSX_NATIVE_VERTEX
    static int native_enabled=-1;
    if(native_enabled<0) {
        const char *option=getenv("SSX_VSH_NATIVE");
        native_enabled=!(option && !strcmp(option,"0"));
    }
    if(native_enabled && !trace_enabled) {
        if(native_dirty) {
            native_program=start<136?ssx_vertex_native_find(words+start,136-start):NULL;
            /* Partially uploaded instructions are never eligible. */
            if(native_program)for(unsigned pc=start;pc<136;pc++) {
                if(!valid[pc]){native_program=NULL;break;}
                if(steps[pc].is_final)break;
            }
            native_dirty=0;
        }
        if(native_program) {
            if(native_context_dirty) {
                native_context_finite=all_finite(constants,192*4);
                native_context_dirty=0;
            }
            int finite=native_context_finite;
            if(finite)finite=all_finite((const float *)input,16*4);
            /* Inlining can choose a different NaN sign/payload. Keep those
             * exceptional inputs/results on the original interpreter path. */
            if(finite) {
                if(!native_program((const float *)input,constants,full.output_regs))return 0;
                finite=all_finite(full.output_regs,13*4);
                if(finite){memcpy(output,full.output_regs,sizeof full.output_regs);return 1;}
                memcpy(full.output_regs,initial_output,sizeof full.output_regs);
            }
        }
    }
#endif
    /* Compiled programs keep their own temporary/address registers. Only
     * initialize the interpreter banks when that path is actually needed,
     * including a fallback after a nonfinite compiled result. */
    memset(full.temp_regs,0,sizeof(full.temp_regs));
    memset(full.address_reg,0,sizeof(full.address_reg));
    for (uint32_t pc = start; pc < 136; ++pc) {
        if (!checked[pc]) {
            checked[pc]=1;relative[pc]=0;writes_context[pc]=0;
            valid[pc]=valid[pc] && operation_ok(&steps[pc].mac,0,1) && operation_ok(&steps[pc].ilu,0,1);
            for(int i=0;i<3;i++) if(steps[pc].mac.inputs[i].is_relative || steps[pc].ilu.inputs[i].is_relative)relative[pc]=1;
            for(int i=0;i<2;i++) if(steps[pc].mac.outputs[i].type==NV2ART_CONTEXT ||
                steps[pc].ilu.outputs[i].type==NV2ART_CONTEXT)writes_context[pc]=1;
        }
        if (!valid[pc] || (relative[pc] &&
            (!operation_ok(&steps[pc].mac,full.address_reg[0],0) ||
             !operation_ok(&steps[pc].ilu,full.address_reg[0],0)))) {
            static int bad; if (bad++ < 8) fprintf(stderr, "[VSH] invalid start=%u pc=%u valid=%u words=%08x %08x %08x %08x\n", start, pc, valid[pc], words[pc][0], words[pc][1], words[pc][2], words[pc][3]);
            return 0;
        }
        if(writes_context[pc] && state.context_regs==constants) {
            memcpy(full.context_regs,constants,sizeof(constants));
            state.context_regs=full.context_regs;
        }
        nv2a_vsh_emu_apply(&state, &steps[pc]);
        if (tracing) { fprintf(stderr, "[VSHSTEP] %u %08x %08x %08x %08x pos=%g,%g,%g,%g\n",pc,words[pc][0],words[pc][1],words[pc][2],words[pc][3],full.output_regs[0],full.output_regs[1],full.output_regs[2],full.output_regs[3]);
        for(int n=0;n<3;n++) { const Nv2aVshInput *a=&steps[pc].mac.inputs[n]; if(a->type==NV2ART_CONTEXT) fprintf(stderr,"[VSHCONST] c%u=%g,%g,%g,%g\n",a->index,constants[a->index*4],constants[a->index*4+1],constants[a->index*4+2],constants[a->index*4+3]); }}
        if (steps[pc].is_final) {
            memcpy(output, full.output_regs, sizeof(full.output_regs));
            if (trace_enabled) {
                static uint32_t seen[64]; static unsigned seen_count;
                uint32_t hash = 2166136261u;
                for (unsigned j = start; j <= pc; ++j) for (int k = 0; k < 4; ++k) hash = (hash ^ words[j][k]) * 16777619u;
                unsigned j; for (j = 0; j < seen_count; ++j) if (seen[j] == hash) break;
                if (j == seen_count && seen_count < 64) {
                    seen[seen_count++] = hash;
                    fprintf(stderr,"[VSHPROGRAM] hash=%08x start=%u end=%u pos=%g,%g,%g,%g uv=%g,%g,%g,%g\n",hash,start,pc,output[0][0],output[0][1],output[0][2],output[0][3],output[9][0],output[9][1],output[9][2],output[9][3]);
                    for (unsigned a = start; a <= pc; ++a)
                        fprintf(stderr,"[VSHCODE] %u %08x %08x %08x %08x\n", a,words[a][0],words[a][1],words[a][2],words[a][3]);
                    for (int a = 0; a < 192; ++a)
                        fprintf(stderr,"[VSHCONTEXT] c%d=%.9g,%.9g,%.9g,%.9g\n",a,constants[a*4],constants[a*4+1],constants[a*4+2],constants[a*4+3]);
                    for (int a = 0; a < 16; ++a) fprintf(stderr,"[VSHINPUT] v%d=%g,%g,%g,%g\n",a,input[a][0],input[a][1],input[a][2],input[a][3]);
                }
            }
            static int shown; if (trace_enabled && shown++ < 8) fprintf(stderr, "[VSH] start=%u end=%u in=%g,%g,%g,%g out=%g,%g,%g,%g color=%g,%g,%g,%g\n", start, pc, input[0][0],input[0][1],input[0][2],input[0][3], output[0][0],output[0][1],output[0][2],output[0][3],output[3][0],output[3][1],output[3][2],output[3][3]);
            return isfinite(output[0][0]) && isfinite(output[0][1]);
        }
    }
    return 0;
}

/* Snapshot is valid only until the next guest shader/constant upload. */
int ssx_vertex_snapshot(const uint32_t (**code)[4], unsigned *count, const float **context)
{
    if(!ssx_vertex_active() || start>=136)return 0;
    for(unsigned pc=start;pc<136;pc++) {
        if(!valid[pc])return 0;
        if(steps[pc].is_final) {
            *code=(const uint32_t (*)[4])&words[start];
            *count=pc-start+1;*context=constants;return 1;
        }
    }
    return 0;
}
