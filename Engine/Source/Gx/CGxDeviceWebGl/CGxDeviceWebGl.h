// wow335modding: the browser's graphics device, a WebGL2 port of CGxDeviceOpenGl. It reports no shader support, so the
// client draws everything through the fixed-function path (CPU skinning for models), which this device emulates with
// one GLSL program: per-vertex lighting, four texture stages with the OpenGL device's texture-env ops, texgen,
// texture matrices, linear fog and alpha test, all fed from a uniform block.
#ifndef ENGINE_SOURCE_GX_CGXDEVICEWEBGL_CGXDEVICEWEBGL_H
#define ENGINE_SOURCE_GX_CGXDEVICEWEBGL_CGXDEVICEWEBGL_H

#include <cstdint>
#include <GLES3/gl3.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"

class CGxDeviceWebGl : public CGxDevice {
    public:
    static const uint32_t NUM_TMUS = 4;

    // The fixed-function state the shader reads, laid out for std140 (every member is a vec4, ivec4 or mat4).
    struct Uniforms {
        float modelView[16];
        float projection[16];
        float texMatrix[NUM_TMUS][16];
        float matDiffuse[4];
        float matEmissive[4];
        float currentColor[4];
        float sceneAmbient[4];
        float lightPos[4][4];
        float lightAmbient[4][4];
        float lightDiffuse[4][4];
        float lightAtten[4][4]; // constant, linear, quadratic, enabled
        float fog[4];           // start, end, enabled, alpha reference (0 = no alpha test)
        float fogColor[4];
        int32_t texEnable[4];   // 0 off, 1 2d, 2 cube map
        int32_t colorOp[4];
        int32_t alphaOp[4];
        int32_t texGen[4];
        int32_t flags[4];       // lighting, color material, has color array, unused
        float point[4];         // point size
    };

    Uniforms m_uniforms;
    int32_t m_glContext;
    GLuint m_program;
    GLuint m_ubo;
    GLuint m_vao;
    GLuint m_fbo;
    GLuint m_boundFbo;
    GLuint m_arrayBuffer;
    GLuint m_elementArrayBuffer;
    uintptr_t m_indexPtr;
    int32_t m_viewDirty;
    uint32_t m_attribEnabled;
    uint32_t m_colorMask;
    uint32_t m_depthMask;
    GLenum m_cullFace;
    int32_t m_lightingEnabled;
    int32_t m_ambientOnlyMode;

    virtual void ITexMarkAsUpdated(CGxTex* texId);
    virtual void IRsSendToHw(EGxRenderState which);
    virtual ~CGxDeviceWebGl();
    virtual int32_t DeviceCreate(GxWindowProc windowProc, const CGxFormat& format);
    virtual int32_t DeviceCreate(void* window, const CGxFormat& format);
    virtual void DeviceDestroy();
    virtual int32_t DeviceSetFormat(const CGxFormat& format);
    virtual void* DeviceWindow();
    virtual void DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels);
    virtual void DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths);
    virtual int32_t DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2);
    virtual void DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane);
    virtual void CapsWindowSize(CRect& rect);
    virtual void CapsWindowSizeInScreenCoords(CRect& rect);
    virtual void ScenePresent();
    virtual void SceneClear(uint32_t mask, CImVector color);
    virtual void XformSetProjection(const C44Matrix& matrix);
    virtual void XformSetView(const C44Matrix& matrix);
    virtual void Draw(CGxBatch* batch, int32_t count);
    virtual void PrimLineWidth(float width);
    virtual void PoolSizeSet(CGxPool* pool, uint32_t size);
    virtual void PoolDestroy(CGxPool* pool);
    virtual char* BufLock(CGxBuf* buf);
    virtual int32_t BufUnlock(CGxBuf* buf, uint32_t size);
    virtual void BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset);
    virtual void TexDestroy(CGxTex* texId);
    virtual int32_t TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane);
    virtual int32_t TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face);
    virtual bool QueryBegin(CGxQuery* query);
    virtual bool QueryEnd(CGxQuery* query);
    virtual bool QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data);
    virtual bool QueryGetData(CGxQuery* query, uint32_t& data);
    virtual void IShaderReload(CGxShader* shader, const char* path, const char* name);
    virtual void IShaderCreate(CGxShader* shader);
    virtual void StereoSetConvergence(float convergence);
    virtual float StereoGetConvergence();
    virtual void StereoSetSeparation(float separation);
    virtual float StereoGetSeparation();
    virtual bool StereoEnabled();
    virtual void DeviceUnk81(uint32_t a, uint32_t b);
    virtual void DeviceUnk82();
    virtual void DeviceUnk83(uint32_t a, uint32_t b);

    CGxDeviceWebGl();
    void BindBuffer(GLenum target, GLuint buffer);
    void BindTexture(CGxTex* texId, uint32_t tmu);
    void CreatePoolAPI(CGxPool* pool);
    void DestroyPoolAPI(CGxPool* pool);
    int32_t IDevAttachGlContext(const CGxFormat& format);
    void ISetCaps();
    void ISetTexGen(uint32_t tmu, int32_t texGen);
    void ISetTexture(uint32_t tmu, CGxTex* texId);
    void IStateSetContextDefaults();
    void IStateSync();
    void IStateSyncIndexPtr();
    void IStateSyncLights();
    void IStateSyncMaterial();
    void IStateSyncScissorRect();
    void IStateSyncVertexPtrs();
    void IStateSyncViewport();
    void IStateSyncXforms();
    void ITexDownload(CGxTex* texId, uint32_t width, uint32_t height, uint32_t face, uint32_t mip, uint32_t baseMip, uint32_t stride, const void* texels);
    void ITexMarkAsUpdated(CGxTex* texId, uint32_t tmu);
    void ITexSetFlags(CGxTex* texId);
    void IXformGLModelView(const C44Matrix& view, C44Matrix& world);
    void IXformSetProjection(const C44Matrix& matrix);
    void SetAttribEnabled(GLuint location, bool enabled);
    // wow335modding: FXAA, bloom, sharpening and vibrance over the finished 3D world (glow 0 = no bloom; postFxaa,
    // postSharpen, postVibrance)
    void PostProcessWorld(float glow);
};

CGxDevice* CreateCGxDeviceWebGl();

// wow335modding: the display's refresh rate in Hz as the page measures it, and the rates the video options offer
// (CGxDevice/Web/CGxDevice.cpp): the usual ones, and the display's own. The chosen one (gxRefresh) caps the frame rate.
uint32_t WebDisplayRefreshRate();
uint32_t WebRefreshRates(uint32_t displayRate, uint32_t (&rates)[16]);

#endif
