#include "../Core.h"
#if CC_GFX_BACKEND == CC_GFX_BACKEND_WASM_EXPORT

#define OVERRIDE_BEGEND2D_FUNCTIONS
#include "../_GraphicsBase.h"
#include "../Errors.h"
#include "../Window.h"
#include <stdlib.h>

// WASM exports/imports declarations
#define WASM_EXPORT(name) __attribute__((export_name(name)))

extern void host_create_texture(int id, int width, int height, const void* pixels);
extern void host_bind_texture(int id);
extern void host_delete_texture(int id);
extern void host_create_vb(int id, int format, int count);
extern void host_upload_vb(int id, const void* data, int count);
extern void host_bind_vb(int id);
extern void host_delete_vb(int id);
extern void host_draw_indexed_tris(int verticesCount, int startVertex, int hints);
extern void host_load_matrix(int type, const float* matrix);
extern void host_clear_color(float r, float g, float b, float a);
extern void host_clear_buffers(int buffers);
extern void host_set_depth_write(int enabled);
extern void host_set_depth_test(int enabled);
extern void host_set_alpha_blend(int enabled);
extern void host_set_alpha_test(int enabled);
extern void host_set_face_culling(int enabled);
extern void host_enable_texture_offset(float x, float y);
extern void host_disable_texture_offset(void);

static int next_tex_id = 1;
static int next_vb_id = 1;

struct GfxVb {
    int id;
    int format;
    int count;
    void* data;
};

static void Gfx_RestoreState(void) {
    InitDefaultResources();
}

static void Gfx_FreeState(void) {
    FreeDefaultResources();
}

void Gfx_Create(void) {
    Gfx.MaxTexWidth  = 4096;
    Gfx.MaxTexHeight = 4096;
    Gfx.Created      = true;
    Gfx.BackendType  = CC_GFX_BACKEND_WASM_EXPORT;
    Gfx.Limitations  = GFX_LIMIT_MINIMAL;
    
    Gfx_RestoreState();
}

void Gfx_Free(void) { 
    Gfx_FreeState();
}

void Gfx_BindTexture(GfxResourceID texId) {
    host_bind_texture(texId ? (int)(uintptr_t)texId : 0);
}

void Gfx_DeleteTexture(GfxResourceID* texId) {
    GfxResourceID data = *texId;
    if (data) {
        host_delete_texture((int)(uintptr_t)data);
    }
    *texId = NULL;
}

GfxResourceID Gfx_AllocTexture(struct Bitmap* bmp, int rowWidth, cc_uint8 flags, cc_bool mipmaps) {
    int id = next_tex_id++;
    void* tmp = malloc(bmp->width * bmp->height * 4);
    if (!tmp) return NULL;
    CopyPixels(tmp, bmp->width * 4, bmp->scan0, rowWidth * 4, bmp->width, bmp->height);
    host_create_texture(id, bmp->width, bmp->height, tmp);
    free(tmp);
    return (GfxResourceID)(uintptr_t)id;
}

void Gfx_UpdateTexture(GfxResourceID texId, int x, int y, struct Bitmap* part, int rowWidth, cc_bool mipmaps) {
    extern void host_update_texture(int id, int x, int y, int w, int h, const void* pixels);
    void* tmp = malloc(part->width * part->height * 4);
    if (tmp) {
        CopyPixels(tmp, part->width * 4, part->scan0, rowWidth * 4, part->width, part->height);
        host_update_texture((int)(uintptr_t)texId, x, y, part->width, part->height, tmp);
        free(tmp);
    }
}

void Gfx_EnableMipmaps(void)  { }
void Gfx_DisableMipmaps(void) { }

void Gfx_Begin2D(int width, int height) {
    gfx_rendering2D = true;
    Gfx_SetAlphaBlending(true);
    Gfx_SetDepthTest(false);
    Gfx_SetDepthWrite(false);
    
    struct Matrix ortho;
    Gfx_CalcOrthoMatrix(&ortho, (float)width, (float)height, -100.0f, 1000.0f);
    Gfx_LoadMatrix(MATRIX_PROJ, &ortho);
    Gfx_LoadMatrix(MATRIX_VIEW, &Matrix_Identity);
}

void Gfx_End2D(void) {
    gfx_rendering2D = false;
    Gfx_SetAlphaBlending(false);
    Gfx_SetDepthTest(true);
    Gfx_SetDepthWrite(true);
}

void Gfx_SetFog(cc_bool enabled)      { }
void Gfx_SetFogCol(PackedCol col)   { }
void Gfx_SetFogDensity(float value) { }
void Gfx_SetFogEnd(float value)     { }
void Gfx_SetFogMode(FogFunc func)   { }

void Gfx_SetFaceCulling(cc_bool enabled) {
    host_set_face_culling(enabled);
}

static void SetAlphaTest(cc_bool enabled) {
    host_set_alpha_test(enabled);
}

static void SetAlphaBlend(cc_bool enabled) {
    host_set_alpha_blend(enabled);
}

void Gfx_SetAlphaArgBlend(cc_bool enabled) { }

void Gfx_ClearBuffers(GfxBuffers buffers) {
    host_clear_buffers(buffers);
}

void Gfx_ClearColor(PackedCol color) {
    float r = PackedCol_R(color) / 255.0f;
    float g = PackedCol_G(color) / 255.0f;
    float b = PackedCol_B(color) / 255.0f;
    float a = PackedCol_A(color) / 255.0f;
    host_clear_color(r, g, b, a);
}

void Gfx_SetDepthTest(cc_bool enabled) {
    host_set_depth_test(enabled);
}

void Gfx_SetDepthWrite(cc_bool enabled) {
    host_set_depth_write(enabled);
}

static void SetColorWrite(cc_bool r, cc_bool g, cc_bool b, cc_bool a) {
    extern void host_set_color_write(int r, int g, int b, int a);
    host_set_color_write(r, g, b, a);
}

void Gfx_DepthOnlyRendering(cc_bool depthOnly) {
    Gfx_SetColorWrite(!depthOnly, !depthOnly, !depthOnly, !depthOnly);
}

GfxResourceID Gfx_CreateIb2(int count, Gfx_FillIBFunc fillFunc, void* obj) {
    int id = next_vb_id++;
    host_create_vb(id, 2, count); 
    cc_uint16* temp = malloc(count * 2);
    if (temp) {
        fillFunc(temp, count, obj);
        host_upload_vb(id, temp, count);
        free(temp);
    }
    return (GfxResourceID)(uintptr_t)id;
}

void Gfx_BindIb(GfxResourceID ib) {
    extern void host_bind_ib(int id);
    host_bind_ib((int)(uintptr_t)ib);
}

void Gfx_DeleteIb(GfxResourceID* ib) {
    GfxResourceID data = *ib;
    if (data) {
        extern void host_delete_ib(int id);
        host_delete_ib((int)(uintptr_t)data);
    }
    *ib = NULL;
}

static GfxResourceID Gfx_AllocStaticVb(VertexFormat fmt, int count) {
    struct GfxVb* vb = malloc(sizeof(struct GfxVb));
    if (!vb) return NULL;
    vb->id = next_vb_id++;
    vb->format = fmt;
    vb->count = count;
    vb->data = malloc(count * (fmt == VERTEX_FORMAT_TEXTURED ? SIZEOF_VERTEX_TEXTURED : SIZEOF_VERTEX_COLOURED));
    host_create_vb(vb->id, fmt, count);
    return vb;
}

static GfxResourceID Gfx_AllocDynamicVb(VertexFormat fmt, int maxVertices) {
    return Gfx_AllocStaticVb(fmt, maxVertices);
}

void Gfx_BindVb(GfxResourceID vb) {
    struct GfxVb* v = vb;
    host_bind_vb(v ? v->id : 0);
}

void Gfx_DeleteVb(GfxResourceID* vb) {
    struct GfxVb* v = *vb;
    if (v) {
        host_delete_vb(v->id);
        free(v->data);
        free(v);
    }
    *vb = NULL;
}

void* Gfx_LockVb(GfxResourceID vb, VertexFormat fmt, int count) {
    struct GfxVb* v = vb;
    return v->data;
}

void Gfx_UnlockVb(GfxResourceID vb) {
    struct GfxVb* v = vb;
    host_upload_vb(v->id, v->data, v->count);
}

void Gfx_BindDynamicVb(GfxResourceID vb) {
    Gfx_BindVb(vb);
}

void* Gfx_LockDynamicVb(GfxResourceID vb, VertexFormat fmt, int count) {
    struct GfxVb* v = vb;
    v->count = count; 
    return v->data;
}

void Gfx_UnlockDynamicVb(GfxResourceID vb) {
    Gfx_UnlockVb(vb);
}

void Gfx_DeleteDynamicVb(GfxResourceID* vb) {
    Gfx_DeleteVb(vb);
}

void Gfx_SetVertexFormat(VertexFormat fmt) {
    gfx_format = fmt;
    gfx_stride = strideSizes[fmt];
    extern void host_set_vertex_format(int fmt);
    host_set_vertex_format(fmt);
}

void Gfx_DrawVb_Lines(int verticesCount) {
    extern void host_draw_lines(int count);
    host_draw_lines(verticesCount);
}

void Gfx_DrawVb_IndexedTris_Range(int verticesCount, int startVertex, DrawHints hints) {
    host_draw_indexed_tris(verticesCount, startVertex, hints);
}

void Gfx_DrawVb_IndexedTris(int verticesCount) {
    host_draw_indexed_tris(verticesCount, 0, DRAW_HINT_NONE);
}

void Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex, DrawHints hints) {
    host_draw_indexed_tris(verticesCount, startVertex, hints);
}

void Gfx_LoadMatrix(MatrixType type, const struct Matrix* matrix) {
    host_load_matrix(type, &matrix->row1.x);
}

void Gfx_LoadMVP(const struct Matrix* view, const struct Matrix* proj, struct Matrix* mvp) {
    Gfx_LoadMatrix(MATRIX_VIEW, view);
    Gfx_LoadMatrix(MATRIX_PROJ, proj);
}

void Gfx_EnableTextureOffset(float x, float y) {
    host_enable_texture_offset(x, y);
}

void Gfx_DisableTextureOffset(void) {
    host_disable_texture_offset();
}

void Gfx_CalcOrthoMatrix(struct Matrix* matrix, float width, float height, float zNear, float zFar) {
    *matrix = Matrix_Identity;
    matrix->row1.x =  2.0f / width;
    matrix->row2.y = -2.0f / height;
    matrix->row3.z = -2.0f / (zFar - zNear);
    matrix->row4.x = -1.0f;
    matrix->row4.y =  1.0f;
    matrix->row4.z = -(zFar + zNear) / (zFar - zNear);
}

static float Cotangent(float x) { return Math_CosF(x) / Math_SinF(x); }
void Gfx_CalcPerspectiveMatrix(struct Matrix* matrix, float fov, float aspect, float zFar) {
    float zNear = 0.1f;
    float c = Cotangent(0.5f * fov);
    *matrix = Matrix_Identity;
    matrix->row1.x =  c / aspect;
    matrix->row2.y =  c;
    matrix->row3.z = -(zFar + zNear) / (zFar - zNear);
    matrix->row3.w = -1.0f;
    matrix->row4.z = -(2.0f * zFar * zNear) / (zFar - zNear);
    matrix->row4.w =  0.0f;
}

void Gfx_SetViewport(int x, int y, int w, int h) {
    extern void host_set_viewport(int x, int y, int w, int h);
    host_set_viewport(x, y, w, h);
}

void Gfx_SetScissor(int x, int y, int w, int h) {
    extern void host_set_scissor(int x, int y, int w, int h);
    host_set_scissor(x, y, w, h);
}

cc_result Gfx_TakeScreenshot(struct Stream* output) { return ERR_NOT_SUPPORTED; }
cc_bool Gfx_WarnIfNecessary(void) { return false; }
cc_bool Gfx_GetUIOptions(struct MenuOptionsScreen* s) { return false; }
void Gfx_GetApiInfo(cc_string* info) {
    String_AppendConst(info, "-- WASM Render Export backend --\n");
}
void Gfx_OnWindowResize(void) { }

void Gfx_BeginFrame(void) { }
void Gfx_EndFrame(void) { }
void Gfx_SetVSync(cc_bool vsync) { }
cc_bool Gfx_TryRestoreContext(void) { return true; }

#endif
