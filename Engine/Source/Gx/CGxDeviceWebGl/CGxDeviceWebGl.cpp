// wow335modding: see CGxDeviceWebGl.h. The structure follows CGxDeviceOpenGl function by function; where OpenGL set
// fixed-function state (texture env, lights, material, fog, matrices, client arrays) this device fills m_uniforms and
// vertex attributes instead.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <GLES3/gl3.h>
#include "Engine/Source/Gx/CGxDeviceWebGl/CGxDeviceWebGl.h"
#include "Engine/Source/BlizzardCore/Source/System/File/Web/WebFile.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Time.h"
#include "Engine/Source/Storm/H/storm.h"

extern "C" {
int32_t wowGlCreate(int32_t width, int32_t height);
void wowGlResize(int32_t width, int32_t height);
void wowGlPresent();
}

#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

static const GLenum s_glBufferTarget[GxPoolTargets_Last] = { GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER };
static const GLenum s_glBufferUsage[3] = { GL_STATIC_DRAW, GL_DYNAMIC_DRAW, GL_STREAM_DRAW };
static const GLenum s_glTexTarget[4] = { GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_2D, GL_TEXTURE_2D };
static const GLenum s_glCubeFaces[6] = { 0x8515, 0x8516, 0x8517, 0x8518, 0x8519, 0x851A };
static const GLint s_glMinFilter[GxTexFilters_Last] = { GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR };
static const GLint s_glMagFilter[GxTexFilters_Last] = { GL_NEAREST, GL_LINEAR, GL_LINEAR, GL_LINEAR, GL_LINEAR, GL_LINEAR };
static const GLint s_glAttribSize[7] = { 4, 4, 4, 2, 3, 2, 1 };
static const GLenum s_glAttribType[7] = { GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE, GL_FLOAT, GL_FLOAT, GL_SHORT, GL_FLOAT };
static const GLboolean s_glAttribNormalized[7] = { GL_TRUE, GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
static const GLenum s_glBlendSrc[12] = { GL_ONE, GL_ONE, GL_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_COLOR, GL_DST_COLOR, GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE, GL_CONSTANT_ALPHA };
static const GLenum s_glBlendDst[12] = { GL_ZERO, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO, GL_SRC_COLOR, GL_ONE, GL_ONE, GL_ZERO, GL_ZERO, GL_ONE, GL_ONE_MINUS_CONSTANT_ALPHA };
static const GLenum s_glDepthFunc[4] = { GL_LEQUAL, GL_EQUAL, GL_GEQUAL, GL_LESS };
static const GLenum s_glPrim[GxPrims_Last] = { GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN };

// How each texture format is stored. WebGL has no BGRA, so the client's Argb formats are swizzled to RGBA on upload.
struct GlTexFormat {
    GLint m_internalFormat;
    GLenum m_format;
    GLenum m_type;
    uint32_t m_bytesPerPixel;
};

static const GlTexFormat s_glTexFormat[GxTexFormats_Last] = {
    { 0, 0, 0, 0 },
    { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4 },                 // Abgr8888: already RGBA bytes
    { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4 },                 // Argb8888: BGRA bytes, swizzled
    { GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2 },        // Argb4444: nibbles rotated
    { GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2 },      // Argb1555: bits rotated
    { GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2 },          // Rgb565
    { GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 0 },
    { GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0, 0 },
    { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 4 },
};

static EmergencyMem s_emergencyMem[GxPoolTargets_Last];
static TSGrowableArray<uint8_t> s_texBuffer;
static bool s_s3tc;
static bool s_anisotropic;

// The fixed-function pipeline as one program. Both stages declare the same uniform block (see CGxDeviceWebGl::Uniforms).
static const char* s_uniformBlock = R"(#version 300 es
precision highp float;
precision highp int;
layout(std140) uniform Ff {
    mat4 u_modelView;
    mat4 u_projection;
    mat4 u_texMatrix[4];
    vec4 u_matDiffuse;
    vec4 u_matEmissive;
    vec4 u_currentColor;
    vec4 u_sceneAmbient;
    vec4 u_lightPos[4];
    vec4 u_lightAmbient[4];
    vec4 u_lightDiffuse[4];
    vec4 u_lightAtten[4];
    vec4 u_fog;
    vec4 u_fogColor;
    ivec4 u_texEnable;
    ivec4 u_colorOp;
    ivec4 u_alphaOp;
    ivec4 u_texGen;
    ivec4 u_flags;
    vec4 u_point;
};
)";

static const char* s_vertexShader = R"(
layout(location = 0) in vec4 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_tex0;
layout(location = 4) in vec4 a_tex1;
layout(location = 5) in vec4 a_tex2;
layout(location = 6) in vec4 a_tex3;
out vec4 v_color;
out vec4 v_tex0;
out vec4 v_tex1;
out vec4 v_tex2;
out vec4 v_tex3;
out float v_fogDepth;

// EGxTexGen: 1 object linear, 2/3 eye linear, 4 reflection map, 5 normal map, 6 sphere map.
vec4 TexCoord(int texGen, vec4 tc, vec3 eye, vec3 n, mat4 m) {
    if (texGen == 1) {
        tc.xyz = a_position.xyz;
    } else if (texGen == 2 || texGen == 3) {
        tc.xyz = eye;
    } else if (texGen == 4) {
        tc.xyz = reflect(normalize(eye), n);
    } else if (texGen == 5) {
        tc.xyz = n;
    } else if (texGen == 6) {
        vec3 r = reflect(normalize(eye), n);
        float m2 = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
        tc.xy = r.xy / m2 + 0.5;
    }
    return m * tc;
}

void main() {
    vec4 eye4 = u_modelView * a_position;
    vec3 eye = eye4.xyz;
    gl_Position = u_projection * eye4;
    vec3 n = normalize(mat3(u_modelView) * a_normal);

    vec4 current = u_flags.z != 0 ? a_color : u_currentColor;
    vec4 color = current;

    if (u_flags.x != 0) {
        // Color material: 0 tracks ambient and diffuse, 2 tracks emission (1, specular, is not lit here).
        vec4 ambDiff = u_flags.y == 0 ? current : u_matDiffuse;
        vec3 c = (u_flags.y == 2 ? current.rgb : u_matEmissive.rgb) + u_sceneAmbient.rgb * ambDiff.rgb;

        for (int i = 0; i < 4; i++) {
            if (u_lightAtten[i].w == 0.0) {
                continue;
            }

            vec3 l;
            float atten = 1.0;

            if (u_lightPos[i].w == 0.0) {
                l = normalize(u_lightPos[i].xyz);
            } else {
                vec3 d = u_lightPos[i].xyz - eye;
                float dist = length(d);
                l = d / max(dist, 0.000001);
                atten = 1.0 / (u_lightAtten[i].x + u_lightAtten[i].y * dist + u_lightAtten[i].z * dist * dist);
            }

            c += atten * (u_lightAmbient[i].rgb + max(dot(n, l), 0.0) * u_lightDiffuse[i].rgb) * ambDiff.rgb;
        }

        color = vec4(c, ambDiff.a);
    }

    v_color = clamp(color, 0.0, 1.0);
    v_tex0 = TexCoord(u_texGen.x, a_tex0, eye, n, u_texMatrix[0]);
    v_tex1 = TexCoord(u_texGen.y, a_tex1, eye, n, u_texMatrix[1]);
    v_tex2 = TexCoord(u_texGen.z, a_tex2, eye, n, u_texMatrix[2]);
    v_tex3 = TexCoord(u_texGen.w, a_tex3, eye, n, u_texMatrix[3]);
    v_fogDepth = abs(eye.z);
    gl_PointSize = u_point.x;
}
)";

static const char* s_fragmentShaderHead = R"(
uniform sampler2D u_tex2d0;
uniform sampler2D u_tex2d1;
uniform sampler2D u_tex2d2;
uniform sampler2D u_tex2d3;
uniform samplerCube u_texCube0;
uniform samplerCube u_texCube1;
uniform samplerCube u_texCube2;
uniform samplerCube u_texCube3;
in vec4 v_color;
in vec4 v_tex0;
in vec4 v_tex1;
in vec4 v_tex2;
in vec4 v_tex3;
in float v_fogDepth;
out vec4 o_color;

// CGxDeviceOpenGl's texture env ops: 0 modulate, 1 modulate 2x, 2 add, 3 pass through, 4 decal, 5 fade.
vec3 OpRgb(int op, vec4 p, vec4 t) {
    if (op == 0) return p.rgb * t.rgb;
    if (op == 1) return p.rgb * t.rgb * 2.0;
    if (op == 2) return p.rgb + t.rgb;
    if (op == 3) return p.rgb;
    if (op == 4) return p.rgb * p.a + t.rgb * (1.0 - p.a);
    return t.rgb * p.a + p.rgb * (1.0 - p.a);
}

float OpAlpha(int op, vec4 p, vec4 t) {
    if (op == 0) return p.a * t.a;
    if (op == 1) return p.a * t.a * 2.0;
    if (op == 2) return p.a + t.a;
    if (op == 3) return p.a;
    if (op == 4) return p.a * p.a + t.a * (1.0 - p.a);
    return t.a * p.a + p.a * (1.0 - p.a);
}

void main() {
    vec4 c = v_color;
    vec4 t;
)";

static const char* s_fragmentShaderTail = R"(
    if (c.a < u_fog.w) {
        discard;
    }

    if (u_fog.z != 0.0) {
        float f = clamp((u_fog.y - v_fogDepth) / (u_fog.y - u_fog.x), 0.0, 1.0);
        c.rgb = mix(u_fogColor.rgb, c.rgb, f);
    }

    o_color = c;
}
)";

static GLuint CompileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "CGxDeviceWebGl: shader compile failed: %s\n", log);
    }

    return shader;
}

static GLuint CreateProgram() {
    std::string fragment = std::string(s_uniformBlock) + s_fragmentShaderHead;

    // Sampler arrays need constant indices in GLSL ES, so the stages are written out.
    for (uint32_t i = 0; i < CGxDeviceWebGl::NUM_TMUS; i++) {
        char stage[512];
        static const char* lanes = "xyzw";
        snprintf(stage, sizeof(stage),
            "    if (u_texEnable.%c != 0) {\n"
            "        if (u_texEnable.%c == 1) t = texture(u_tex2d%u, v_tex%u.xy / v_tex%u.w); else t = texture(u_texCube%u, v_tex%u.xyz);\n"
            "        c = clamp(vec4(OpRgb(u_colorOp.%c, c, t), OpAlpha(u_alphaOp.%c, c, t)), 0.0, 1.0);\n"
            "    }\n",
            lanes[i], lanes[i], i, i, i, i, i, lanes[i], lanes[i]);
        fragment += stage;
    }

    fragment += s_fragmentShaderTail;

    GLuint program = glCreateProgram();
    GLuint vs = CompileShader(GL_VERTEX_SHADER, std::string(s_uniformBlock) + s_vertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragment);
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        fprintf(stderr, "CGxDeviceWebGl: program link failed: %s\n", log);
    }

    glUseProgram(program);
    glUniformBlockBinding(program, glGetUniformBlockIndex(program, "Ff"), 0);

    for (uint32_t i = 0; i < CGxDeviceWebGl::NUM_TMUS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "u_tex2d%u", i);
        glUniform1i(glGetUniformLocation(program, name), i);
        snprintf(name, sizeof(name), "u_texCube%u", i);
        glUniform1i(glGetUniformLocation(program, name), CGxDeviceWebGl::NUM_TMUS + i);
    }

    return program;
}

static void ColorToFloats(float* out, uint32_t value) {
    const CImVector& color = reinterpret_cast<const CImVector&>(value);
    out[0] = color.r * (1.0f / 255.0f);
    out[1] = color.g * (1.0f / 255.0f);
    out[2] = color.b * (1.0f / 255.0f);
    out[3] = color.a * (1.0f / 255.0f);
}

static void SetVec4(float* out, float x, float y, float z, float w) {
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

// A row vector times a C44Matrix, the way glLightfv transforms a position by the current modelview.
static void TransformRow(const C44Matrix& m, const float* p, float* out) {
    const float* e = &m.a0;

    for (uint32_t i = 0; i < 4; i++) {
        out[i] = p[0] * e[i] + p[1] * e[4 + i] + p[2] * e[8 + i] + p[3] * e[12 + i];
    }
}

// The display's frame time in ms, measured by the page (index.html wowFrameMs); 60 Hz until it knows. A blocking call
// to the page's thread.
static double PageFrameMs() {
    return MAIN_THREAD_EM_ASM_DOUBLE({ return Module.wowFrameMs ? Module.wowFrameMs() : 1000 / 60; });
}

uint32_t WebDisplayRefreshRate() {
    return static_cast<uint32_t>(1000.0 / PageFrameMs() + 0.5);
}

// Always the usual rates, not only those up to the display's: the client checks a saved rate against this list at
// startup, before the page may have measured the display, and a rate above the display's just runs at the display's.
// Not 75: that is gxRefresh's default, which should mean "the display's rate" (a 75 Hz display still offers it).
uint32_t WebRefreshRates(uint32_t displayRate, uint32_t (&rates)[16]) {
    static const uint32_t s_rates[] = { 30, 60, 90, 100, 120, 144, 165, 240 };
    uint32_t count = 0;
    bool listed = false;

    for (uint32_t rate : s_rates) {
        rates[count++] = rate;
        listed = listed || rate == displayRate;
    }

    if (!listed) {
        rates[count++] = displayRate;
    }

    return count;
}

static void CanvasSize(int32_t& width, int32_t& height) {
    int w = 0;
    int h = 0;
    emscripten_get_canvas_element_size("#canvas", &w, &h);
    width = w > 0 ? w : 1024;
    height = h > 0 ? h : 768;
}

CGxDevice* CreateCGxDeviceWebGl() {
    void* mem = SMemAlloc(sizeof(CGxDeviceWebGl), __FILE__, __LINE__, 0);
    return mem ? new (mem) CGxDeviceWebGl() : nullptr;
}

CGxDeviceWebGl::CGxDeviceWebGl() {
    this->m_api = GxApi_OpenGl;
    this->m_caps.m_colorFormat = GxCF_rgba;
    this->m_caps.unk_0018 = 0;
    this->m_glContext = 0;
    this->m_program = 0;
    this->m_ubo = 0;
    this->m_vao = 0;
    this->m_fbo = 0;
    this->m_boundFbo = 0;
    this->m_arrayBuffer = 0;
    this->m_elementArrayBuffer = 0;
    this->m_indexPtr = 0;
    this->m_viewDirty = 0;
    this->m_attribEnabled = 0;
    this->m_colorMask = 0xF;
    this->m_depthMask = 1;
    this->m_cullFace = GL_BACK;
    this->m_lightingEnabled = 0;
    this->m_ambientOnlyMode = 0;
    memset(&this->m_uniforms, 0, sizeof(this->m_uniforms));
    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

CGxDeviceWebGl::~CGxDeviceWebGl() {
}

void CGxDeviceWebGl::BindBuffer(GLenum target, GLuint buffer) {
    GLuint& bound = target == GL_ARRAY_BUFFER ? this->m_arrayBuffer : this->m_elementArrayBuffer;

    if (bound != buffer) {
        glBindBuffer(target, buffer);
        bound = buffer;
    }
}

void CGxDeviceWebGl::SetAttribEnabled(GLuint location, bool enabled) {
    uint32_t bit = 1 << location;

    if (((this->m_attribEnabled & bit) != 0) == enabled) {
        return;
    }

    if (enabled) {
        glEnableVertexAttribArray(location);
        this->m_attribEnabled |= bit;
    } else {
        glDisableVertexAttribArray(location);
        this->m_attribEnabled &= ~bit;
    }
}

void CGxDeviceWebGl::ISetCaps() {
    this->m_caps.m_numTmus = NUM_TMUS;
    this->m_caps.m_pixelCenterOnEdge = 1;
    this->m_caps.m_texelCenterOnEdge = 1;

    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    this->m_caps.m_texMaxSize[GxTex_2d] = maxSize;
    this->m_caps.m_texTarget[GxTex_2d] = 1;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxSize);
    this->m_caps.m_texMaxSize[GxTex_CubeMap] = maxSize;
    this->m_caps.m_texTarget[GxTex_CubeMap] = 1;
    this->m_caps.m_texMaxSize[GxTex_Rectangle] = 256;
    this->m_caps.m_texTarget[GxTex_Rectangle] = 0;
    this->m_caps.m_texMaxSize[GxTex_NonPow2] = 256;
    this->m_caps.m_texTarget[GxTex_NonPow2] = 0;
    this->m_caps.m_texNonPow2 = 1;

    s_s3tc = emscripten_webgl_enable_extension(this->m_glContext, "WEBGL_compressed_texture_s3tc");
    this->m_caps.m_texFmt[GxTex_Dxt1] = s_s3tc;
    this->m_caps.m_texFmt[GxTex_Dxt3] = s_s3tc;
    this->m_caps.m_texFmt[GxTex_Dxt5] = s_s3tc;
    this->m_caps.m_texFmt[GxTex_Uv88] = 0;
    fprintf(stderr, "CGxDeviceWebGl: %s, S3TC %s\n", glGetString(GL_RENDERER), s_s3tc ? "yes" : "no");

    this->m_caps.m_numStreams = NUM_TMUS + 3;
    this->m_caps.m_streamOffset = 1;
    this->m_caps.m_generateMipMaps = 0;

    // No shader profiles: the client takes its fixed-function paths.
    for (uint32_t i = 0; i < GxShTargets_Last; i++) {
        this->m_caps.m_shaderTargets[i] = 0;
        this->m_caps.m_shaderConstants[i] = 0;
    }

    this->m_caps.m_texFilterTrilinear = 1;
    s_anisotropic = emscripten_webgl_enable_extension(this->m_glContext, "EXT_texture_filter_anisotropic");

    if (s_anisotropic) {
        GLint anisotropy = 1;
        glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
        this->m_caps.m_maxTexAnisotropy = anisotropy;
        this->m_caps.m_texFilterAnisotropic = anisotropy > 1;
    }

    this->m_caps.m_colorWrite = 1;
    this->m_caps.m_maxClipPlanes = 0; // ponytail: no user clip planes (WebGL has none); a shader discard could add them
    this->m_caps.m_occlusionQuery = 0; // WebGL2 queries only say "any samples", not how many
    this->m_caps.m_hwCursor = 0;
    this->m_caps.m_pointScaleMax = 1.0f;
    this->m_caps.m_pointParameters = 0;
    this->m_caps.m_pointSprite = 0;
    this->m_caps.m_shaderAlphaRef = 1;
    this->m_caps.unk_0134 = 1;
    memset(this->m_caps.unused_0114, 0, sizeof(this->m_caps.unused_0114));
    this->m_caps.m_stereoAvailable = 0;
    this->m_caps.unk_0138 = 1;
    this->Log(this->m_caps);
}

int32_t CGxDeviceWebGl::IDevAttachGlContext(const CGxFormat& format) {
    int32_t width;
    int32_t height;
    CanvasSize(width, height);

    this->m_glContext = wowGlCreate(width, height);

    if (!this->m_glContext) {
        CGxDevice::Log("CGxDeviceWebGl: this browser has no WebGL2");
        return 0;
    }

    this->m_context = 1;
    this->ISetCaps();

    this->m_program = CreateProgram();
    glGenBuffers(1, &this->m_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, this->m_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Uniforms), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, this->m_ubo);
    glGenVertexArrays(1, &this->m_vao);
    glBindVertexArray(this->m_vao);

    CRect rect;
    rect.minY = 0.0f;
    rect.minX = 0.0f;
    rect.maxY = static_cast<float>(height);
    rect.maxX = static_cast<float>(width);
    this->DeviceSetDefWindow(rect);

    this->ICursorCreate(format);
    this->IStateSetContextDefaults();
    this->IRsSync(1);
    this->ShaderConstantsClear();

    return 1;
}

void CGxDeviceWebGl::IStateSetContextDefaults() {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glEnable(GL_SCISSOR_TEST);

    // Values for arrays that are switched off, like OpenGL's current normal, color and texture coordinates.
    glVertexAttrib4f(1, 0.0f, 0.0f, 1.0f, 0.0f);
    glVertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);

    for (uint32_t tmu = 0; tmu < NUM_TMUS; tmu++) {
        glVertexAttrib4f(3 + tmu, 0.0f, 0.0f, 0.0f, 1.0f);
        C44Matrix identity;
        memcpy(this->m_uniforms.texMatrix[tmu], &identity.a0, sizeof(float) * 16);
    }

    this->InitLights();

    CGxMatrixStack& world = this->m_xforms[GxXform_World];
    CGxMatrixStack& view = this->m_xforms[GxXform_View];
    C44Matrix modelView;
    this->IXformGLModelView(world.m_mtx[world.m_level] * view.m_mtx[view.m_level], modelView);
    memcpy(this->m_uniforms.modelView, &modelView.a0, sizeof(float) * 16);
    this->m_viewDirty = 0;
    this->m_uniforms.point[0] = 1.0f;
}

int32_t CGxDeviceWebGl::DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) {
    return CGxDevice::DeviceCreate(windowProc, format);
}

int32_t CGxDeviceWebGl::DeviceCreate(void* window, const CGxFormat& format) {
    if (!this->m_context && !this->IDevAttachGlContext(format)) {
        return 0;
    }

    return CGxDevice::DeviceCreate(window, format);
}

int32_t CGxDeviceWebGl::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceWebGl::DeviceSetFormat():");
    this->Log(format);

    if (!this->m_context && !this->IDevAttachGlContext(format)) {
        return 0;
    }

    if (!CGxDevice::DeviceSetFormat(format)) {
        return 0;
    }

    this->m_windowVisible = 1;
    this->m_windowFocus = 1;
    this->m_viewportDirty = 1;
    return 1;
}

void CGxDeviceWebGl::DeviceDestroy() {
    CGxDevice::DeviceDestroy();
}

void* CGxDeviceWebGl::DeviceWindow() {
    return OsGuiGetWindow(0);
}

int32_t CGxDeviceWebGl::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    switch (wm) {
        case GxWM_Size:
            if (param2 == 1 || param2 == 2) {
                this->m_windowVisible = 0;
            } else {
                this->m_windowVisible = 1;
                this->DeviceSetDefWindow(*reinterpret_cast<const CRect*>(param1));
                this->m_viewportDirty = 1;
            }

            break;

        case GxWM_SetFocus:
            this->m_windowFocus = 1;
            break;

        case GxWM_KillFocus:
            this->m_windowFocus = 0;
            break;

        default:
            break;
    }

    return 0;
}

void CGxDeviceWebGl::CapsWindowSize(CRect& rect) {
    rect = this->DeviceCurWindow();
}

void CGxDeviceWebGl::CapsWindowSizeInScreenCoords(CRect& rect) {
    rect = this->DeviceCurWindow();
}

void CGxDeviceWebGl::DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) {
    this->ClampRectToWindow(rect);

    int32_t width = rect.maxX - rect.minX;
    int32_t height = rect.maxY - rect.minY;
    pixels.SetCount(width * height);

    const CRect& window = this->DeviceCurWindow();
    glReadPixels(rect.minX, static_cast<GLint>(window.maxY - rect.maxY), width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.Ptr());

    // RGBA rows, bottom up, into CImVector (BGRA) rows, top down.
    CImVector* data = pixels.Ptr();

    for (int32_t i = 0; i < width * height; i++) {
        uint8_t r = data[i].b;
        data[i].b = data[i].r;
        data[i].r = r;
    }

    for (int32_t y = 0; y < height / 2; y++) {
        CImVector* top = data + y * width;
        CImVector* bottom = data + (height - 1 - y) * width;

        for (int32_t x = 0; x < width; x++) {
            CImVector pixel = top[x];
            top[x] = bottom[x];
            bottom[x] = pixel;
        }
    }
}

void CGxDeviceWebGl::DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) {
    // ponytail: WebGL cannot read the depth buffer; everything reads as far away.
    this->ClampRectToWindow(rect);
    uint32_t count = (rect.maxX - rect.minX) * (rect.maxY - rect.minY);
    depths.SetCount(count);

    for (uint32_t i = 0; i < count; i++) {
        depths[i] = 1.0f;
    }
}

void CGxDeviceWebGl::DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane) {
    CGxTex* current = this->m_textureTarget[buffer].m_texture;

    if (current && current->m_flags.m_bit8 && this->m_boundFbo) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    }

    if (buffer == GxBuffers_Depth && !texture && this->m_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, this->m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, this->m_boundFbo);
    }

    CGxDevice::DeviceSetRenderTarget(buffer, texture, plane);

    if (texture) {
        this->m_textureTarget[buffer].m_apiSpecific = texture;

        if (!texture->m_apiSpecificData) {
            GLuint name;
            glGenTextures(1, &name);
            texture->m_apiSpecificData = reinterpret_cast<void*>(static_cast<uintptr_t>(name));
        }

        GLenum target = s_glTexTarget[texture->m_target];

        if (texture->m_needsCreation) {
            this->BindTexture(texture, 0xFFFFFFFF);
            const GlTexFormat& format = s_glTexFormat[texture->m_format];
            glTexImage2D(target, 0, format.m_internalFormat, texture->m_width, texture->m_height, 0, format.m_format, format.m_type, nullptr);
            texture->m_needsCreation = 0;
        }

        if (!this->m_fbo) {
            glGenFramebuffers(1, &this->m_fbo);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, this->m_fbo);
        this->m_boundFbo = this->m_fbo;

        GLuint name = static_cast<GLuint>(reinterpret_cast<uintptr_t>(texture->m_apiSpecificData));

        if (buffer) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, name, 0);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, name, 0);

            if (texture->m_flags.m_bit8) {
                if (!texture->m_apiSpecificData2) {
                    GLuint renderbuffer = 0;
                    glGenRenderbuffers(1, &renderbuffer);
                    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, texture->m_width, texture->m_height);
                    texture->m_apiSpecificData2 = reinterpret_cast<void*>(static_cast<uintptr_t>(renderbuffer));
                }

                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, static_cast<GLuint>(reinterpret_cast<uintptr_t>(texture->m_apiSpecificData2)));
            }
        }

        CRect rect;
        rect.minY = 0.0f;
        rect.minX = 0.0f;
        rect.maxY = static_cast<float>(texture->m_height);
        rect.maxX = static_cast<float>(texture->m_width);
        this->DeviceSetCurWindow(rect);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        this->m_boundFbo = 0;
        this->DeviceSetCurWindow(this->DeviceDefWindow());
        this->m_textureTarget[buffer].m_apiSpecific = nullptr;
    }

    this->m_scissorRectDirty = 1;
    GxXformSetViewport(this->m_viewport.m_x.m_l, this->m_viewport.m_x.m_h, this->m_viewport.m_y.m_l, this->m_viewport.m_y.m_h, this->m_viewport.m_z.m_l, this->m_viewport.m_z.m_h);
    g_theGxDevicePtr->XformSetProjection(this->m_projection);
}

void CGxDeviceWebGl::ScenePresent() {
    // Hitches: one console line per frame over 250 ms, with the game data downloaded meanwhile (all threads).
    static double s_lastFrame;
    double now = emscripten_get_now();
    int32_t requests;
    int64_t bytes;
    double waitMs;
    WebFile::TakeStats(requests, bytes, waitMs);

    if (s_lastFrame && now - s_lastFrame > 250.0) {
        fprintf(stderr, "CGxDeviceWebGl: frame %d took %.0f ms; game data: %d requests, %.1f MB, %.0f ms waited\n", this->m_frameCount, now - s_lastFrame, requests, bytes / 1048576.0, waitMs);
    }

    s_lastFrame = now;
    int32_t screenShot = this->m_scrShotClick;
    CGxDevice::ScenePresent();

    if (screenShot) {
        this->DeviceScreenShot();
    }

    this->ICursorDraw();

    // No glGetError here: in a browser it waits for the GPU process to finish all queued work, which stalled frames
    // for up to seconds whenever the GPU was busy. The browser console reports WebGL errors on its own.

    wowGlPresent();

    // The page sizes #canvas to its layout box; follow it. Asking is a blocking call to the page's thread, so only
    // every 30 frames (half a second).
    int32_t width = 0;
    int32_t height = 0;
    const CRect& window = this->DeviceDefWindow();

    // The time per frame: the display's refresh period, or longer when the video options chose a lower rate
    // (gxRefresh; WebRefreshRates). A rate not on that list, like the default 75, means the display's own. Read from
    // the cvar, its pending (latched) value first, not the device format: a page has no display mode to switch, so the
    // choice applies at once instead of after a gxRestart.
    static double s_frameMs = 1000.0 / 60.0;

    if (this->m_frameCount % 30 == 0) {
        CanvasSize(width, height);

        // Settings reach Config.wtf only on a clean exit, and a page is mostly closed or reloaded instead: write changed
        // ones every few seconds (SaveAll does nothing when none changed); index.html keeps WTF in IndexedDB.
        if (this->m_frameCount % 300 == 0) {
            CVar::SaveAll();
        }

        static CVar* gxRefresh = CVar::Lookup("gxRefresh");
        double displayMs = PageFrameMs();
        int32_t chosen = !gxRefresh ? 0 : gxRefresh->m_latchedValue.m_str ? SStrToInt(gxRefresh->m_latchedValue.m_str) : gxRefresh->m_intValue;
        uint32_t rates[16];
        uint32_t count = WebRefreshRates(static_cast<uint32_t>(1000.0 / displayMs + 0.5), rates);
        s_frameMs = displayMs;

        for (uint32_t i = 0; i < count; i++) {
            if (static_cast<int32_t>(rates[i]) == chosen && 1000.0 / rates[i] > displayMs) {
                s_frameMs = 1000.0 / rates[i];
            }
        }
    }

    if (width && (width != static_cast<int32_t>(window.maxX) || height != static_cast<int32_t>(window.maxY))) {
        wowGlResize(width, height);

        CRect rect;
        rect.minY = 0.0f;
        rect.minX = 0.0f;
        rect.maxY = static_cast<float>(height);
        rect.maxX = static_cast<float>(width);
        this->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), 0);

        // The size event goes out only now that the device reports the new size: its handlers ask the device
        // (fonts are rasterized for CapsWindowSize, GxuFontWindowSizeChanged), as on Windows, where both change together.
        OsQueuePut(OS_INPUT_SIZE, width, height, 0, 0);
    }

    this->WaitForFPSCap();

    // At most one frame per display refresh (60, 120, 144 Hz...; maxFPS still caps below that), and the wait is spent
    // back in the browser: this thread runs the client's endless main loop, so without yielding here the browser's own
    // work for it (releasing presented frames, among others) piles up until the browser stalls the thread for seconds
    // to catch up. emscripten_sleep returns to the browser's event loop (JSPI, -sJSPI) and resumes the frame
    // afterwards; it yields even when no time is left.
    // Frames are due on a running schedule of exact refresh periods (8.33 ms at 120 Hz), so whole-millisecond sleeps
    // still average out to the display rate; a frame that runs late restarts the schedule instead of rushing to catch up.
    static double s_nextPresent;
    now = emscripten_get_now();
    emscripten_sleep(s_nextPresent > now ? static_cast<uint32_t>(s_nextPresent - now) : 0);

    now = emscripten_get_now();
    s_nextPresent = s_nextPresent + s_frameMs > now ? s_nextPresent + s_frameMs : now + s_frameMs;
    this->ShaderConstantsClear();
}

void CGxDeviceWebGl::SceneClear(uint32_t mask, CImVector color) {
    glClearColor(color.r * (1.0f / 255.0f), color.g * (1.0f / 255.0f), color.b * (1.0f / 255.0f), color.a * (1.0f / 255.0f));
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    GLbitfield bits = 0;

    if (mask & 0x1) {
        bits = GL_COLOR_BUFFER_BIT;
    }

    if (mask & 0x2) {
        bits |= GL_DEPTH_BUFFER_BIT;
    }

    this->IStateSyncViewport();
    glClear(bits);

    glDepthMask(this->m_depthMask ? GL_TRUE : GL_FALSE);
    glColorMask(this->m_colorMask & 1, (this->m_colorMask & 4) != 0, (this->m_colorMask & 2) != 0, (this->m_colorMask & 8) != 0);
}

void CGxDeviceWebGl::IXformGLModelView(const C44Matrix& view, C44Matrix& world) {
    world = C44Matrix(view.a0, view.a1, -view.a2, view.a3, view.b0, view.b1, -view.b2, view.b3, view.c0, view.c1, -view.c2, view.c3, view.d0, view.d1, -view.d2, view.d3);
}

void CGxDeviceWebGl::IXformSetProjection(const C44Matrix& matrix) {
    C44Matrix projection(matrix.a0, matrix.a1, matrix.a2, matrix.a3, matrix.b0, matrix.b1, matrix.b2, matrix.b3, -matrix.c0, -matrix.c1, -matrix.c2, -matrix.c3, matrix.d0, matrix.d1, matrix.d2, matrix.d3);

    if (!(this->m_appMasterEnables & (1 << GxMasterEnable_NormalProjection)) && 1.0f != matrix.d3) {
        C44Matrix scale(0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        projection = projection * scale;
    }

    // Render targets are read upside down compared with the window, so they are drawn flipped (and wound the other way).
    GLenum cullFace = GL_BACK;

    if (this->m_textureTarget[GxBuffers_Color].m_apiSpecific || this->m_textureTarget[GxBuffers_Depth].m_apiSpecific) {
        projection.a1 = -projection.a1;
        projection.b1 = -projection.b1;
        projection.c1 = -projection.c1;
        projection.d1 = -projection.d1;
        cullFace = GL_FRONT;
    }

    if (this->m_cullFace != cullFace) {
        glCullFace(cullFace);
        this->m_cullFace = cullFace;
    }

    memcpy(this->m_uniforms.projection, &projection.a0, sizeof(float) * 16);
    this->m_projNative = projection;
}

void CGxDeviceWebGl::XformSetProjection(const C44Matrix& matrix) {
    this->m_projection = matrix;
    this->IXformSetProjection(matrix);
}

void CGxDeviceWebGl::XformSetView(const C44Matrix& matrix) {
    CGxDevice::XformSetView(matrix);
    this->m_viewDirty = 1;
}

void CGxDeviceWebGl::IStateSyncIndexPtr() {
    if (!this->m_primIndexDirty) {
        return;
    }

    CGxBuf* buf = this->m_primIndexBuf;
    this->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(reinterpret_cast<uintptr_t>(buf->m_pool->m_apiSpecific)));
    this->m_indexPtr = buf->m_index;
    this->m_primIndexDirty = 0;
}

void CGxDeviceWebGl::IStateSyncViewport() {
    const CRect& window = this->DeviceCurWindow();

    float width;
    float height;

    if (this->m_textureTarget[GxBuffers_Color].m_apiSpecific) {
        CGxTex* target = static_cast<CGxTex*>(this->m_textureTarget[GxBuffers_Color].m_apiSpecific);
        width = static_cast<float>(target->m_width);
        height = static_cast<float>(target->m_height);
    } else {
        width = window.maxX;
        height = window.maxY;
    }

    GLint x = static_cast<GLint>(this->m_viewport.m_x.m_l * width + 0.5f);
    GLint y = static_cast<GLint>(this->m_viewport.m_y.m_l * height + 0.5f);
    glViewport(x, y, static_cast<GLint>(width * this->m_viewport.m_x.m_h + 0.5f) - x, static_cast<GLint>(height * this->m_viewport.m_y.m_h + 0.5f) - y);
    glDepthRangef(this->m_viewport.m_z.m_l, this->m_viewport.m_z.m_h);
    this->m_viewportDirty = 0;
}

void CGxDeviceWebGl::IStateSyncScissorRect() {
    if (!this->m_scissorRectDirty) {
        return;
    }

    const CRect& window = this->DeviceCurWindow();

    float width;
    float height;

    if (this->m_textureTarget[GxBuffers_Color].m_apiSpecific) {
        CGxTex* target = static_cast<CGxTex*>(this->m_textureTarget[GxBuffers_Color].m_apiSpecific);
        width = static_cast<float>(target->m_width);
        height = static_cast<float>(target->m_height);
    } else {
        width = window.maxX;
        height = window.maxY;
    }

    GLint x = static_cast<GLint>(this->m_scissorRect.minX * width + 0.5f);
    GLint y = static_cast<GLint>(this->m_scissorRect.minY * height + 0.5f);
    glScissor(x, y, static_cast<GLint>(width * this->m_scissorRect.maxX + 0.5f) - x, static_cast<GLint>(height * this->m_scissorRect.maxY + 0.5f) - y);
    this->m_scissorRectDirty = 0;
}

void CGxDeviceWebGl::IStateSyncVertexPtrs() {
    if (!this->m_primVertexDirty) {
        return;
    }

    for (uint32_t attrib = 0; attrib < GxVAs_Last; attrib++) {
        uint32_t bit = 1 << attrib;

        if (!(bit & this->m_primVertexDirty)) {
            continue;
        }

        // The shader locations this stream feeds: position 0, normal 1, color 2, and 3 + tmu for every texture
        // stage whose GxRs_TextureCoord names this stream.
        GLuint locations[NUM_TMUS + 1];
        uint32_t count = 0;

        if (attrib == GxVA_Position) {
            locations[count++] = 0;
        } else if (attrib == GxVA_Normal) {
            locations[count++] = 1;
        } else if (attrib == GxVA_Color0) {
            locations[count++] = 2;
        } else if (attrib >= GxVA_TexCoord0) {
            for (uint32_t tmu = 0; tmu < NUM_TMUS; tmu++) {
                if (this->m_appRenderStates[GxRs_TextureCoord0 + tmu].m_value.m_data.u[0] == attrib - GxVA_TexCoord0) {
                    locations[count++] = 3 + tmu;
                }
            }
        }

        CGxBuf* buf = (bit & this->m_primVertexMask) ? this->m_primVertexFormatBuf[attrib] : nullptr;
        GLuint vbo = buf ? static_cast<GLuint>(reinterpret_cast<uintptr_t>(buf->m_pool->m_apiSpecific)) : 0;

        for (uint32_t i = 0; i < count; i++) {
            if (!vbo) {
                this->SetAttribEnabled(locations[i], false);
                continue;
            }

            const CGxVertexAttrib& vertexAttrib = this->m_primVertexFormatAttrib[attrib];
            this->BindBuffer(GL_ARRAY_BUFFER, vbo);
            glVertexAttribPointer(locations[i], s_glAttribSize[vertexAttrib.m_type], s_glAttribType[vertexAttrib.m_type], s_glAttribNormalized[vertexAttrib.m_type], vertexAttrib.m_bufSize, reinterpret_cast<const void*>(static_cast<uintptr_t>(buf->m_index + vertexAttrib.m_offset)));
            this->SetAttribEnabled(locations[i], true);
        }
    }

    this->m_primVertexDirty = 0;
}

void CGxDeviceWebGl::IStateSyncLights() {
    Uniforms& u = this->m_uniforms;
    bool colorArray = (this->m_primVertexMask & (1 << GxVA_Color0)) != 0;

    if (this->m_appRenderStates[GxRs_Lighting].m_value.m_data.i[0]) {
        this->m_ambientOnlyMode = 0;
        u.flags[0] = (this->m_appMasterEnables & 0x1) != 0;
        SetVec4(u.sceneAmbient, 0.0f, 0.0f, 0.0f, 0.0f);

        // Light positions go to eye space through the view, as glLightfv does with the view as modelview.
        CGxMatrixStack& view = this->m_xforms[GxXform_View];
        C44Matrix viewGl;
        this->IXformGLModelView(view.m_mtx[view.m_level], viewGl);

        for (uint32_t i = 0; i < 4; i++) {
            CGxApiLight& light = this->m_lights[i];
            u.lightAtten[i][3] = light.m_enable ? 1.0f : 0.0f;

            if (!light.m_enable) {
                continue;
            }

            float position[4];

            if (light.m_dir.w == 1.0f) {
                SetVec4(position, light.m_dir.x, light.m_dir.y, light.m_dir.z, light.m_dir.w);
            } else {
                SetVec4(position, -light.m_dir.x, -light.m_dir.y, -light.m_dir.z, -light.m_dir.w);
            }

            TransformRow(viewGl, position, u.lightPos[i]);
            SetVec4(u.lightAmbient[i], light.m_ambColor.x, light.m_ambColor.y, light.m_ambColor.z, 0.0f);
            SetVec4(u.lightDiffuse[i], light.m_dirColor.x, light.m_dirColor.y, light.m_dirColor.z, 1.0f);
            u.lightAtten[i][0] = light.m_constantAttenuation;
            u.lightAtten[i][1] = light.m_linearAttenuation;
            u.lightAtten[i][2] = light.m_quadraticAttenuation;
        }

        return;
    }

    if (colorArray || this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.u[0] == 0xFFFFFFFF) {
        this->m_ambientOnlyMode = 0;
        u.flags[0] = 0;
        SetVec4(u.sceneAmbient, 0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        // Unlit but colored by the material: lighting with no lights and a white ambient.
        this->m_ambientOnlyMode = 1;
        u.flags[0] = 1;
        SetVec4(u.sceneAmbient, 1.0f, 1.0f, 1.0f, 1.0f);

        for (uint32_t i = 0; i < 4; i++) {
            u.lightAtten[i][3] = 0.0f;
        }
    }
}

void CGxDeviceWebGl::IStateSyncMaterial() {
    Uniforms& u = this->m_uniforms;
    int32_t colorMaterial = this->m_appRenderStates[GxRs_ColorMaterial].m_value.m_data.i[0];

    ColorToFloats(u.matDiffuse, this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.u[0]);
    ColorToFloats(u.matEmissive, this->m_appRenderStates[GxRs_MatEmissive].m_value.m_data.u[0]);

    // Without a color array the tracked material color is OpenGL's current color, which IStateSyncMaterial set
    // from the material.
    static const EGxRenderState s_tracked[3] = { GxRs_MatDiffuse, GxRs_MatSpecular, GxRs_MatEmissive };
    ColorToFloats(u.currentColor, this->m_appRenderStates[s_tracked[colorMaterial > 2 ? 0 : colorMaterial]].m_value.m_data.u[0]);

    u.flags[1] = colorMaterial;
    u.flags[2] = (this->m_primVertexMask & (1 << GxVA_Color0)) != 0;
}

void CGxDeviceWebGl::IStateSyncXforms() {
    if (this->m_viewDirty) {
        CGxMatrixStack& world = this->m_xforms[GxXform_World];
        CGxMatrixStack& view = this->m_xforms[GxXform_View];
        C44Matrix modelView;
        this->IXformGLModelView(world.m_mtx[world.m_level] * view.m_mtx[view.m_level], modelView);
        memcpy(this->m_uniforms.modelView, &modelView.a0, sizeof(float) * 16);
        world.m_dirty = 0;
        view.m_dirty = 0;
    }

    for (uint32_t tmu = 0; tmu < NUM_TMUS; tmu++) {
        CGxTex* texture = static_cast<CGxTex*>(this->m_appRenderStates[GxRs_Texture0 + tmu].m_value.m_data.p);
        CGxMatrixStack& xform = this->m_xforms[tmu];
        CGxMatrixStack& texGen = this->m_texGen[tmu];

        if (!texture || (!xform.m_dirty && !texGen.m_dirty)) {
            continue;
        }

        C44Matrix matrix;

        if (static_cast<uint32_t>(this->m_appRenderStates[GxRs_TextureShader0 + tmu].m_value.m_data.i[0] - 1) <= 1) {
            matrix = texGen.m_mtx[texGen.m_level] * xform.m_mtx[xform.m_level];
        }

        xform.m_dirty = 0;
        texGen.m_dirty = 0;
        memcpy(this->m_uniforms.texMatrix[tmu], &matrix.a0, sizeof(float) * 16);
    }
}

void CGxDeviceWebGl::IStateSync() {
    this->m_viewDirty |= this->m_xforms[GxXform_World].m_dirty | this->m_xforms[GxXform_View].m_dirty;
    this->IRsSync(0);
    this->IStateSyncLights();
    this->IStateSyncMaterial();
    this->IStateSyncXforms();
    this->m_viewDirty = 0;
    this->IStateSyncScissorRect();
    this->IStateSyncViewport();
    this->IStateSyncVertexPtrs();
    this->IStateSyncIndexPtr();
}

void CGxDeviceWebGl::ISetTexGen(uint32_t tmu, int32_t texGen) {
    if (tmu >= NUM_TMUS) {
        return;
    }

    this->m_uniforms.texGen[tmu] = texGen;

    if (texGen == GxTexGen_EyeLinear) {
        CGxMatrixStack& stack = this->m_texGen[tmu];
        stack.m_dirty = 1;
        stack.m_flags[stack.m_level] &= ~CGxMatrixStack::F_Identity;

        C44Matrix& matrix = stack.m_mtx[stack.m_level];
        CGxMatrixStack& view = this->m_xforms[GxXform_View];
        this->IXformGLModelView(view.m_mtx[view.m_level], matrix);
        matrix = matrix.AffineInverse();
    } else {
        this->m_texGen[tmu].Identity();
    }
}

void CGxDeviceWebGl::ISetTexture(uint32_t tmu, CGxTex* texId) {
    if (tmu >= NUM_TMUS) {
        return;
    }

    if (texId) {
        this->m_uniforms.texEnable[tmu] = texId->m_target == GxTex_CubeMap ? 2 : 1;
        this->BindTexture(texId, tmu);
        this->ITexMarkAsUpdated(texId, tmu);
    } else {
        this->m_uniforms.texEnable[tmu] = 0;
    }
}

void CGxDeviceWebGl::IRsSendToHw(EGxRenderState which) {
    const CGxStateBom& value = this->m_appRenderStates[which].m_value;
    Uniforms& u = this->m_uniforms;

    switch (which) {
        case GxRs_PolygonOffset:
            if (value.m_data.f[0] == 0.0f) {
                glDisable(GL_POLYGON_OFFSET_FILL);
            } else {
                glPolygonOffset(-4.0f, value.m_data.f[0] * 65536.0f * -1.0f);
                glEnable(GL_POLYGON_OFFSET_FILL);
            }

            break;

        case GxRs_BlendingMode: {
            uint32_t mode = value.m_data.u[0];

            if (mode < 2 || mode >= GxBlends_Last) {
                glDisable(GL_BLEND);
            } else {
                glEnable(GL_BLEND);
                glBlendFunc(s_glBlendSrc[mode], s_glBlendDst[mode]);
            }

            break;
        }

        case GxRs_AlphaRef:
            u.fog[3] = value.m_data.i[0] > 0 ? value.m_data.i[0] * (1.0f / 255.0f) : 0.0f;
            break;

        case GxRs_FogStart:
            u.fog[0] = value.m_data.f[0];
            break;

        case GxRs_FogEnd:
            u.fog[1] = value.m_data.f[0];
            break;

        case GxRs_FogColor:
            ColorToFloats(u.fogColor, value.m_data.u[0]);
            break;

        case GxRs_Fog:
            u.fog[2] = (this->m_appMasterEnables & 0x2) && value.m_data.i[0] ? 1.0f : 0.0f;
            break;

        case GxRs_DepthTest:
            if ((this->m_appMasterEnables & 0x4) && value.m_data.i[0]) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }

            break;

        case GxRs_DepthFunc:
            glDepthFunc(s_glDepthFunc[value.m_data.i[0] & 3]);
            break;

        case GxRs_DepthWrite:
            this->m_depthMask = (this->m_appMasterEnables & 0x8) ? value.m_data.u[0] : 0;
            glDepthMask(this->m_depthMask ? GL_TRUE : GL_FALSE);
            break;

        case GxRs_ColorWrite:
            this->m_colorMask = (this->m_appMasterEnables & 0x10) ? value.m_data.u[0] : 0;
            glColorMask(this->m_colorMask & 1, (this->m_colorMask & 4) != 0, (this->m_colorMask & 2) != 0, (this->m_colorMask & 8) != 0);
            break;

        case GxRs_Culling: {
            uint32_t culling = value.m_data.u[0];

            if (!(this->m_appMasterEnables & 0x20) || !culling) {
                glDisable(GL_CULL_FACE);
                break;
            }

            glEnable(GL_CULL_FACE);
            glFrontFace(culling == 1 ? GL_CCW : GL_CW);
            break;
        }

        case GxRs_ScissorTest:
            if (value.m_data.i[0]) {
                glEnable(GL_SCISSOR_TEST);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }

            break;

        case GxRs_Texture0:
        case GxRs_Texture1:
        case GxRs_Texture2:
        case GxRs_Texture3:
            this->ISetTexture(which - GxRs_Texture0, static_cast<CGxTex*>(value.m_data.p));
            break;

        case GxRs_ColorOp0:
        case GxRs_ColorOp1:
        case GxRs_ColorOp2:
        case GxRs_ColorOp3:
            u.colorOp[which - GxRs_ColorOp0] = value.m_data.i[0];
            break;

        case GxRs_AlphaOp0:
        case GxRs_AlphaOp1:
        case GxRs_AlphaOp2:
        case GxRs_AlphaOp3:
            u.alphaOp[which - GxRs_AlphaOp0] = value.m_data.i[0];
            break;

        case GxRs_TexGen0:
        case GxRs_TexGen1:
        case GxRs_TexGen2:
        case GxRs_TexGen3:
            this->ISetTexGen(which - GxRs_TexGen0, value.m_data.i[0]);
            break;

        case GxRs_TextureShader0:
        case GxRs_TextureShader1:
        case GxRs_TextureShader2:
        case GxRs_TextureShader3:
            this->m_xforms[which - GxRs_TextureShader0].m_dirty = 1;
            break;

        case GxRs_TextureCoord0:
        case GxRs_TextureCoord1:
        case GxRs_TextureCoord2:
        case GxRs_TextureCoord3:
            this->m_primVertexDirty = 0xFFFFFFFF;
            break;

        case GxRs_PointScale:
            u.point[0] = value.m_data.f[0];
            break;

        case Unk84:
            glBlendColor(value.m_data.f[0], value.m_data.f[0], value.m_data.f[0], value.m_data.f[0]);
            break;

        default:
            // Material and lighting states are read at draw time (IStateSyncLights, IStateSyncMaterial); clip
            // planes, multisampling, shaders and point sprites have nothing to set.
            break;
    }
}

void CGxDeviceWebGl::Draw(CGxBatch* batch, int32_t count) {
    if (this->m_intF5C) {
        return;
    }

    this->IStateSync();

    glBindBuffer(GL_UNIFORM_BUFFER, this->m_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Uniforms), &this->m_uniforms);

    if (count) {
        glDrawRangeElements(s_glPrim[batch->m_primType], batch->m_minIndex, batch->m_maxIndex, batch->m_count, GL_UNSIGNED_SHORT, reinterpret_cast<const void*>(this->m_indexPtr + sizeof(uint16_t) * batch->m_start));
    } else {
        glDrawArrays(s_glPrim[batch->m_primType], batch->m_start, batch->m_count);
    }
}

void CGxDeviceWebGl::PrimLineWidth(float width) {
    glLineWidth(width);
}

void CGxDeviceWebGl::CreatePoolAPI(CGxPool* pool) {
    if (!this->m_context) {
        return;
    }

    GLuint name = 0;
    glGenBuffers(1, &name);
    pool->m_apiSpecific = reinterpret_cast<void*>(static_cast<uintptr_t>(name));
    this->BindBuffer(s_glBufferTarget[pool->m_target], name);
    glBufferData(s_glBufferTarget[pool->m_target], pool->m_size, nullptr, s_glBufferUsage[pool->m_usage]);
}

void CGxDeviceWebGl::DestroyPoolAPI(CGxPool* pool) {
    if (!pool) {
        return;
    }

    pool->Invalidate();

    if (!pool->m_apiSpecific) {
        return;
    }

    GLuint name = static_cast<GLuint>(reinterpret_cast<uintptr_t>(pool->m_apiSpecific));

    if (this->m_arrayBuffer == name) {
        this->m_arrayBuffer = 0;
    }

    if (this->m_elementArrayBuffer == name) {
        this->m_elementArrayBuffer = 0;
    }

    glDeleteBuffers(1, &name);
    pool->m_apiSpecific = nullptr;
}

void CGxDeviceWebGl::PoolDestroy(CGxPool* pool) {
    this->DestroyPoolAPI(pool);
    CGxDevice::PoolDestroy(pool);
}

void CGxDeviceWebGl::PoolSizeSet(CGxPool* pool, uint32_t size) {
    if (pool->m_apiSpecific) {
        this->BindBuffer(s_glBufferTarget[pool->m_target], static_cast<GLuint>(reinterpret_cast<uintptr_t>(pool->m_apiSpecific)));
        glBufferData(s_glBufferTarget[pool->m_target], size, nullptr, s_glBufferUsage[pool->m_usage]);
    }

    pool->Invalidate();
    pool->m_size = size;
}

char* CGxDeviceWebGl::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);

    CGxPool* pool = buf->m_pool;

    if (!pool->m_apiSpecific) {
        this->CreatePoolAPI(pool);
    }

    if (!pool->m_apiSpecific) {
        char* mem = static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
        pool->m_mem = mem;
        return mem;
    }

    // Writes go to scratch memory and are uploaded on unlock.
    this->BindBuffer(s_glBufferTarget[pool->m_target], static_cast<GLuint>(reinterpret_cast<uintptr_t>(pool->m_apiSpecific)));

    if (pool->m_usage == GxPoolUsage_Stream) {
        glBufferData(s_glBufferTarget[pool->m_target], buf->m_size, nullptr, GL_STREAM_DRAW);
        buf->m_index = 0;
    }

    char* mem = static_cast<char*>(s_emergencyMem[pool->m_target].Lock(buf->m_size));
    pool->m_mem = mem;
    return mem;
}

int32_t CGxDeviceWebGl::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);

    CGxPool* pool = buf->m_pool;
    EGxPoolTarget target = pool->m_target;
    int32_t result = 1;

    if (!pool->m_apiSpecific) {
        this->m_emergencyMem[target].Unlock();
        buf->m_valid = 0;
        this->m_intF5C = 1;
        pool->m_mem = nullptr;
        return 0;
    }

    if (s_emergencyMem[target].m_lock) {
        this->BindBuffer(s_glBufferTarget[target], static_cast<GLuint>(reinterpret_cast<uintptr_t>(pool->m_apiSpecific)));
        glBufferSubData(s_glBufferTarget[target], buf->m_index, buf->m_size, s_emergencyMem[target].m_data.m_data);
        s_emergencyMem[target].Unlock();
    }

    buf->m_valid = result;
    pool->m_mem = nullptr;
    return result;
}

void CGxDeviceWebGl::BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    CGxPool* pool = buf->m_pool;

    if (!pool->m_apiSpecific) {
        this->CreatePoolAPI(pool);
    }

    if (!pool->m_apiSpecific) {
        return;
    }

    this->BindBuffer(s_glBufferTarget[pool->m_target], static_cast<GLuint>(reinterpret_cast<uintptr_t>(pool->m_apiSpecific)));

    if (pool->m_usage == GxPoolUsage_Stream) {
        glBufferData(s_glBufferTarget[pool->m_target], size, data, GL_STREAM_DRAW);
        buf->m_index = 0;
    } else if (size == static_cast<uint32_t>(pool->m_size)) {
        glBufferData(s_glBufferTarget[pool->m_target], size, data, s_glBufferUsage[pool->m_usage]);
    } else {
        glBufferSubData(s_glBufferTarget[pool->m_target], offset + buf->m_index, size, data);
    }
}

void CGxDeviceWebGl::BindTexture(CGxTex* texId, uint32_t tmu) {
    // 2D textures sit on units 0-3, cube maps on 4-7: one sampler of each type per stage, never sharing a unit.
    uint32_t unit = (tmu == 0xFFFFFFFF ? 0 : tmu) + (texId->m_target == GxTex_CubeMap ? NUM_TMUS : 0);

    if (tmu == 0xFFFFFFFF) {
        this->IRsForceUpdate(GxRs_Texture0);
    }

    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(s_glTexTarget[texId->m_target], static_cast<GLuint>(reinterpret_cast<uintptr_t>(texId->m_apiSpecificData)));
}

void CGxDeviceWebGl::ITexSetFlags(CGxTex* texId) {
    GLenum target = s_glTexTarget[texId->m_target];
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, s_glMagFilter[texId->m_flags.m_filter]);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, s_glMinFilter[texId->m_flags.m_filter]);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, texId->m_flags.m_wrapU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, texId->m_flags.m_wrapV ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    if (s_anisotropic) {
        glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, texId->m_flags.m_maxAnisotropy ? texId->m_flags.m_maxAnisotropy : 1.0f);
    }

    texId->m_needsFlagUpdate = 0;
}

void CGxDeviceWebGl::TexDestroy(CGxTex* texId) {
    if (texId->m_apiSpecificData) {
        GLuint name = static_cast<GLuint>(reinterpret_cast<uintptr_t>(texId->m_apiSpecificData));
        glDeleteTextures(1, &name);
    }

    if (texId->m_apiSpecificData2) {
        GLuint renderbuffer = static_cast<GLuint>(reinterpret_cast<uintptr_t>(texId->m_apiSpecificData2));
        glDeleteRenderbuffers(1, &renderbuffer);
    }

    CGxDevice::TexDestroy(texId);
}

int32_t CGxDeviceWebGl::TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) {
    if (!this->m_context) {
        return 0;
    }

    GLenum target = dest->m_target == GxTex_CubeMap ? s_glCubeFaces[plane] : s_glTexTarget[dest->m_target];

    if (!dest->m_apiSpecificData) {
        GLuint name;
        glGenTextures(1, &name);
        dest->m_apiSpecificData = reinterpret_cast<void*>(static_cast<uintptr_t>(name));
    }

    this->BindTexture(dest, 0xFFFFFFFF);

    if (dest->m_needsFlagUpdate) {
        this->ITexSetFlags(dest);
    }

    if (dest->m_needsCreation) {
        glCopyTexImage2D(target, 0, s_glTexFormat[dest->m_format].m_internalFormat, 0, 0, dest->m_width, dest->m_height, 0);
        dest->m_needsCreation = 0;
    }

    glCopyTexSubImage2D(target, level, destPos.x, destPos.y, sourcePos.x, sourcePos.y, size.x, size.y);
    return 1;
}

int32_t CGxDeviceWebGl::TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) {
    return 0;
}

// Copies a rect of client texels into s_texBuffer as tight rows in the layout WebGL takes for the format.
static const uint8_t* ConvertTexels(EGxTexFormat format, const uint8_t* source, uint32_t stride, uint32_t width, uint32_t height) {
    uint32_t bytes = s_glTexFormat[format].m_bytesPerPixel;
    uint32_t rowSize = width * bytes;

    if (s_texBuffer.Count() < rowSize * height) {
        s_texBuffer.SetCount(rowSize * height);
    }

    uint8_t* dest = s_texBuffer.Ptr();

    for (uint32_t y = 0; y < height; y++, source += stride, dest += rowSize) {
        switch (format) {
            case GxTex_Argb8888:
                for (uint32_t x = 0; x < width; x++) {
                    dest[x * 4 + 0] = source[x * 4 + 2];
                    dest[x * 4 + 1] = source[x * 4 + 1];
                    dest[x * 4 + 2] = source[x * 4 + 0];
                    dest[x * 4 + 3] = source[x * 4 + 3];
                }

                break;

            case GxTex_Argb4444:
                for (uint32_t x = 0; x < width; x++) {
                    uint16_t v = reinterpret_cast<const uint16_t*>(source)[x];
                    reinterpret_cast<uint16_t*>(dest)[x] = static_cast<uint16_t>((v << 4) | (v >> 12));
                }

                break;

            case GxTex_Argb1555:
                for (uint32_t x = 0; x < width; x++) {
                    uint16_t v = reinterpret_cast<const uint16_t*>(source)[x];
                    reinterpret_cast<uint16_t*>(dest)[x] = static_cast<uint16_t>((v << 1) | (v >> 15));
                }

                break;

            default:
                memcpy(dest, source, rowSize);
                break;
        }
    }

    return s_texBuffer.Ptr();
}

void CGxDeviceWebGl::ITexDownload(CGxTex* texId, uint32_t width, uint32_t height, uint32_t face, uint32_t mip, uint32_t baseMip, uint32_t stride, const void* texels) {
    uint32_t minY = texId->m_updateRect.minY >> mip;
    uint32_t minX = texId->m_updateRect.minX >> mip;
    uint32_t maxY = minY + 1 > static_cast<uint32_t>(texId->m_updateRect.maxY >> mip) ? minY + 1 : texId->m_updateRect.maxY >> mip;
    uint32_t maxX = minX + 1 > static_cast<uint32_t>(texId->m_updateRect.maxX >> mip) ? minX + 1 : texId->m_updateRect.maxX >> mip;

    if (texId->m_flags.m_bit14) {
        minX = 0;
        minY = 0;
        maxX = width;
        maxY = height;
    }

    maxX = maxX < width ? maxX : width;
    maxY = maxY < height ? maxY : height;

    GLint level = mip - baseMip;
    GLenum target = texId->m_target == GxTex_CubeMap ? s_glCubeFaces[face] : s_glTexTarget[texId->m_target];
    const uint8_t* bits = static_cast<const uint8_t*>(texels);

    switch (texId->m_format) {
        case GxTex_Abgr8888:
        case GxTex_Argb8888:
        case GxTex_Argb4444:
        case GxTex_Argb1555:
        case GxTex_Rgb565: {
            // The client may hand 32-bit data for a 16-bit texture and leave the conversion to the driver; store such
            // textures in the data's own format instead (WebGL converts nothing).
            EGxTexFormat dataFormat = texId->m_dataFormat;

            if (dataFormat < GxTex_Abgr8888 || dataFormat > GxTex_Rgb565) {
                break; // ponytail: DXT data for an uncompressed texture is not decoded; the caps make the client avoid it
            }

            const GlTexFormat& format = s_glTexFormat[dataFormat];
            uint32_t updateWidth = maxX - minX;
            uint32_t updateHeight = maxY - minY;
            const uint8_t* source;
            uint32_t sourceStride;

            if (texId->m_flags.m_bit15) {
                source = bits;
                sourceStride = updateWidth * format.m_bytesPerPixel;
            } else {
                source = bits + minY * stride + minX * format.m_bytesPerPixel;
                sourceStride = stride;
            }

            const uint8_t* pixels = ConvertTexels(dataFormat, source, sourceStride, updateWidth, updateHeight);
            bool full = !minX && !minY && updateWidth == width && updateHeight == height;

            if (texId->m_needsCreation && full) {
                glTexImage2D(target, level, format.m_internalFormat, width, height, 0, format.m_format, format.m_type, pixels);
            } else {
                if (texId->m_needsCreation) {
                    glTexImage2D(target, level, format.m_internalFormat, width, height, 0, format.m_format, format.m_type, nullptr);
                }

                glTexSubImage2D(target, level, minX, minY, updateWidth, updateHeight, format.m_format, format.m_type, pixels);
            }

            break;
        }

        case GxTex_Dxt1:
        case GxTex_Dxt3:
        case GxTex_Dxt5: {
            const GlTexFormat& format = s_glTexFormat[texId->m_format];
            uint32_t blockX = minX & ~3;
            uint32_t blockY = minY & ~3;
            uint32_t blockWidth = ((maxX + 3) & ~3) - blockX;
            uint32_t blockHeight = ((maxY + 3) & ~3) - blockY;
            blockWidth = blockWidth > 4 ? blockWidth : 4;
            blockHeight = blockHeight > 4 ? blockHeight : 4;

            uint32_t bytesPerBlock = CGxDevice::s_texFormatBytesPerBlock[texId->m_format];
            uint32_t rowSize = bytesPerBlock * (blockWidth >> 2);
            uint32_t rows = blockHeight >> 2;
            uint32_t imageSize = rowSize * rows;
            const uint8_t* source = bits;

            if (!texId->m_flags.m_bit15) {
                source += stride * (blockY >> 2) + bytesPerBlock * (blockX >> 2);
            }

            if (s_texBuffer.Count() < imageSize) {
                s_texBuffer.SetCount(imageSize);
            }

            uint8_t* buffer = s_texBuffer.Ptr();

            for (uint32_t row = 0; row < rows; row++) {
                memcpy(buffer + row * rowSize, source + row * stride, rowSize);
            }

            if (width < 4 || height < 4 || (blockWidth == width && blockHeight == height)) {
                glCompressedTexImage2D(target, level, format.m_internalFormat, width, height, 0, imageSize, buffer);
                break;
            }

            if (texId->m_needsCreation) {
                // WebGL wants the whole level's data up front; create it empty, then fill the rect.
                static TSGrowableArray<uint8_t> s_zero;
                uint32_t fullSize = bytesPerBlock * ((width + 3) >> 2) * ((height + 3) >> 2);

                if (s_zero.Count() < fullSize) {
                    uint32_t old = s_zero.Count();
                    s_zero.SetCount(fullSize);
                    memset(s_zero.Ptr() + old, 0, fullSize - old);
                }

                glCompressedTexImage2D(target, level, format.m_internalFormat, width, height, 0, fullSize, s_zero.Ptr());
            }

            glCompressedTexSubImage2D(target, level, blockX, blockY, blockWidth, blockHeight, format.m_internalFormat, imageSize, buffer);
            break;
        }

        default:
            break;
    }
}

void CGxDeviceWebGl::ITexMarkAsUpdated(CGxTex* texId, uint32_t tmu) {
    if (!this->m_context) {
        return;
    }

    if (texId->m_needsUpdate && texId->m_userFunc) {
        uint32_t width = texId->m_width;
        uint32_t height = texId->m_height;

        if (!texId->m_apiSpecificData) {
            GLuint name;
            glGenTextures(1, &name);
            texId->m_apiSpecificData = reinterpret_cast<void*>(static_cast<uintptr_t>(name));
            this->BindTexture(texId, tmu);
            this->ITexSetFlags(texId);
        } else {
            this->BindTexture(texId, tmu);
        }

        uint32_t baseMip;
        uint32_t mipCount;

        if ((texId->m_flags.m_filter >= GxTex_NearestMipNearest && !texId->m_flags.m_generateMipMaps) || texId->m_flags.m_forceMipTracking) {
            uint32_t size = texId->m_height;

            if (texId->m_width > size) {
                size = texId->m_width;
            }

            mipCount = 1;

            while (size != 1) {
                size >>= 1;
                mipCount++;
            }

            baseMip = mipCount - 1;

            if (this->m_baseMipLevel < baseMip) {
                baseMip = this->m_baseMipLevel;
            }

            width >>= baseMip;
            height >>= baseMip;

            if (texId->m_flags.m_forceMipTracking) {
                mipCount = baseMip + 1;
            }
        } else {
            baseMip = 0;
            mipCount = 1;
        }

        glTexParameteri(s_glTexTarget[texId->m_target], GL_TEXTURE_MAX_LEVEL, mipCount - baseMip - 1);

        uint32_t texelStrideInBytes;
        const void* texels;
        texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

        uint32_t faceCount = texId->m_target == GxTex_CubeMap ? 6 : 1;

        for (uint32_t face = 0; face < faceCount; face++) {
            uint32_t mipWidth = width;
            uint32_t mipHeight = height;
            uint32_t levelCount = 0;

            for (uint32_t mip = baseMip; mip != mipCount; mip++, levelCount++) {
                texId->m_userFunc(GxTex_Latch, mipWidth, mipHeight, face, mip, texId->m_userArg, texelStrideInBytes, texels);

                if (!texId->m_flags.m_renderTarget) {
                    this->ITexDownload(texId, mipWidth, mipHeight, face, mip, baseMip, texelStrideInBytes, texels);
                }

                if (!levelCount && texId->m_flags.m_generateMipMaps) {
                    break;
                }

                mipWidth = mipWidth > 1 ? mipWidth >> 1 : 1;
                mipHeight = mipHeight > 1 ? mipHeight >> 1 : 1;
            }
        }

        texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

        if (!texId->m_flags.m_renderTarget) {
            texId->m_needsCreation = 0;
        }
    }

    if (texId->m_needsFlagUpdate) {
        this->ITexSetFlags(texId);
    }

    CGxDevice::ITexMarkAsUpdated(texId);
}

void CGxDeviceWebGl::ITexMarkAsUpdated(CGxTex* texId) {
    this->ITexMarkAsUpdated(texId, 0xFFFFFFFF);
}

bool CGxDeviceWebGl::QueryBegin(CGxQuery* query) {
    return false;
}

bool CGxDeviceWebGl::QueryEnd(CGxQuery* query) {
    return false;
}

bool CGxDeviceWebGl::QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data) {
    return false;
}

bool CGxDeviceWebGl::QueryGetData(CGxQuery* query, uint32_t& data) {
    return false;
}

void CGxDeviceWebGl::IShaderReload(CGxShader* shader, const char* path, const char* name) {
}

void CGxDeviceWebGl::IShaderCreate(CGxShader* shader) {
}

void CGxDeviceWebGl::StereoSetConvergence(float convergence) {
}

float CGxDeviceWebGl::StereoGetConvergence() {
    return 1.0f;
}

void CGxDeviceWebGl::StereoSetSeparation(float separation) {
}

float CGxDeviceWebGl::StereoGetSeparation() {
    return 0.0f;
}

bool CGxDeviceWebGl::StereoEnabled() {
    return false;
}

void CGxDeviceWebGl::DeviceUnk81(uint32_t a, uint32_t b) {
}

void CGxDeviceWebGl::DeviceUnk82() {
}

void CGxDeviceWebGl::DeviceUnk83(uint32_t a, uint32_t b) {
}

// wow335modding: a pass over the finished 3D world, before the UI is drawn on it (CGWorldFrame::OnWorldRender):
// contrast-adaptive sharpening (after AMD's CAS: flat areas get more, edges less, so no halos) and vibrance (muted
// colours gain saturation, saturated ones hardly). One copy of the screen and one full-screen triangle.
static const char* s_postVertexShader = R"(#version 300 es
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char* s_postFragmentShader = R"(#version 300 es
precision mediump float;
uniform sampler2D u_scene;
uniform vec2 u_params; // sharpness 0-1, vibrance
out vec4 o_color;
vec3 At(ivec2 p) {
    return texelFetch(u_scene, clamp(p, ivec2(0), textureSize(u_scene, 0) - 1), 0).rgb;
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 e = At(p);
    vec3 b = At(p + ivec2(0, 1));
    vec3 d = At(p + ivec2(-1, 0));
    vec3 f = At(p + ivec2(1, 0));
    vec3 h = At(p + ivec2(0, -1));
    vec3 lo = min(e, min(min(b, d), min(f, h)));
    vec3 hi = max(e, max(max(b, d), max(f, h)));
    vec3 amount = sqrt(clamp(min(lo, 1.0 - hi) / max(hi, 1e-4), 0.0, 1.0));
    vec3 w = amount * mix(-0.125, -0.2, u_params.x) * step(1e-3, u_params.x);
    vec3 c = clamp((e + (b + d + f + h) * w) / (1.0 + 4.0 * w), 0.0, 1.0);
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    float saturation = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    c = clamp(mix(vec3(luma), c, 1.0 + u_params.y * (1.0 - saturation)), 0.0, 1.0);
    o_color = vec4(c, 1.0);
}
)";

void CGxDeviceWebGl::PostProcessWorld() {
    static CVar* s_sharpen = CVar::Register("postSharpen", "Sharpen the 3D world, 0 (off) to 1", 1, "0.5", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    static CVar* s_vibrance = CVar::Register("postVibrance", "Liven up muted colours of the 3D world, 0 (off) to 1", 1, "0.25", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    float sharpen = s_sharpen->m_floatValue;
    float vibrance = s_vibrance->m_floatValue;

    GLint drawFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    // only the screen, never a texture the world was drawn into
    if ((sharpen <= 0.0f && vibrance <= 0.0f) || drawFbo != 0) {
        return;
    }

    static GLuint s_program;
    static GLint s_paramsLocation;
    static GLuint s_vao;
    static GLuint s_texture;
    static int32_t s_width;
    static int32_t s_height;

    if (!s_program) {
        s_program = glCreateProgram();
        GLuint vs = CompileShader(GL_VERTEX_SHADER, s_postVertexShader);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, s_postFragmentShader);
        glAttachShader(s_program, vs);
        glAttachShader(s_program, fs);
        glLinkProgram(s_program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        s_paramsLocation = glGetUniformLocation(s_program, "u_params");
        glGenVertexArrays(1, &s_vao);
    }

    // What the pass changes, to put back afterwards: the device keeps its own idea of this state.
    GLint program, vao, activeTexture, texture, readFbo, viewport[4];
    GLboolean colorMask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);

    int32_t width;
    int32_t height;
    CanvasSize(width, height);

    if (!s_texture || width != s_width || height != s_height) {
        if (!s_texture) {
            glGenTextures(1, &s_texture);
        }

        glBindTexture(GL_TEXTURE_2D, s_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        s_width = width;
        s_height = height;
    }

    glBindTexture(GL_TEXTURE_2D, s_texture);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    glUseProgram(s_program);
    glUniform2f(s_paramsLocation, sharpen < 1.0f ? sharpen : 1.0f, vibrance);
    glBindVertexArray(s_vao);
    glViewport(0, 0, width, height);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(activeTexture);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glUseProgram(program);
    glBindVertexArray(vao);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    (blend ? glEnable : glDisable)(GL_BLEND);
    (depthTest ? glEnable : glDisable)(GL_DEPTH_TEST);
    (cull ? glEnable : glDisable)(GL_CULL_FACE);
    (scissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
}

#endif
