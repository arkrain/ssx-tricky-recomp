/* Independent interpreter instance for comparing the compiled-program path. */
#undef SSX_NATIVE_VERTEX
#define ssx_vertex_method reference_vertex_method
#define ssx_vertex_active reference_vertex_active
#define ssx_vertex_input_mask reference_vertex_input_mask
#define ssx_vertex_snapshot reference_vertex_snapshot
#define ssx_vertex_execute reference_vertex_execute
#include "../src/nv2a_vertex.c"
