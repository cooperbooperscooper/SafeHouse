// (0.1.11, VR first person) The physical binocucom's drawing: its model and its lens's mask (vrbinoc.h).
#include "vrbinoc.h"
#include "gl.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace vrbinoc
{
namespace
{
struct VERT
{
    glm::vec4 pos;     // (clip space)
    glm::vec4 color;   // (premultiplied as drawn: opaque here but the mask's outside)
};

GLuint s_prog = 0, s_vao = 0, s_vbo = 0;
size_t s_cvertBuf = 0;

bool FProgram()
{
    if (s_prog) return true;
    const char* szVs = "#version 430 core\nlayout(location = 0) in vec4 aPos;\nlayout(location = 1) in vec4 aColor;\n"
                       "out vec4 vColor;\nvoid main() { vColor = aColor; gl_Position = aPos; }\n";
    const char* szFs = "#version 430 core\nin vec4 vColor;\nout vec4 oColor;\nvoid main() { oColor = vColor; }\n";
    auto compile = [](GLenum type, const char* sz) {
        const GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &sz, nullptr);
        glCompileShader(sh);
        GLint f = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &f);
        if (!f) { glDeleteShader(sh); return GLuint(0); }
        return sh;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, szVs), fs = compile(GL_FRAGMENT_SHADER, szFs);
    if (vs && fs)
    {
        s_prog = glCreateProgram();
        glAttachShader(s_prog, vs);
        glAttachShader(s_prog, fs);
        glLinkProgram(s_prog);
        GLint f = 0;
        glGetProgramiv(s_prog, GL_LINK_STATUS, &f);
        if (!f) glDeleteProgram(s_prog), s_prog = 0;
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!s_prog)
    {
        std::fprintf(stderr, "vr: the binocucom's program did not build (no binocucom drawn)\n");
        return false;
    }
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), reinterpret_cast<const void*>(sizeof(glm::vec4)));
    glBindVertexArray(0);
    return true;
}

// The renderer keeps a cache of the GL state: everything changed here is put back as it was (as vr.cpp's pointer)
struct SAVEDGL
{
    GLint prog = 0, vao = 0, vbo = 0, fbo = 0, aviewport[4]{}, depthFunc = 0;
    GLboolean fDepth = GL_FALSE, fBlend = GL_FALSE, fCull = GL_FALSE, fScissor = GL_FALSE, fStencil = GL_FALSE;
    GLboolean fDepthMask = GL_TRUE, amask[4]{};
    SAVEDGL()
    {
        fDepth = glIsEnabled(GL_DEPTH_TEST);
        fBlend = glIsEnabled(GL_BLEND);
        fCull = glIsEnabled(GL_CULL_FACE);
        fScissor = glIsEnabled(GL_SCISSOR_TEST);
        fStencil = glIsEnabled(GL_STENCIL_TEST);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, aviewport);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &fDepthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, amask);
    }
    ~SAVEDGL()
    {
        glUseProgram(GLuint(prog));
        glBindVertexArray(GLuint(vao));
        glBindBuffer(GL_ARRAY_BUFFER, GLuint(vbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(fbo));
        glViewport(aviewport[0], aviewport[1], aviewport[2], aviewport[3]);
        glDepthFunc(GLenum(depthFunc));
        glDepthMask(fDepthMask);
        glColorMask(amask[0], amask[1], amask[2], amask[3]);
        auto set = [](GLenum cap, GLboolean f) { if (f) glEnable(cap); else glDisable(cap); };
        set(GL_DEPTH_TEST, fDepth);
        set(GL_BLEND, fBlend);
        set(GL_CULL_FACE, fCull);
        set(GL_SCISSOR_TEST, fScissor);
        set(GL_STENCIL_TEST, fStencil);
    }
};

void Upload(const std::vector<VERT>& av)
{
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    const size_t cb = av.size() * sizeof(VERT);
    if (av.size() > s_cvertBuf)
    {
        s_cvertBuf = av.size() * 2;
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(s_cvertBuf * sizeof(VERT)), nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(cb), av.data());
}

// The model, in its own space (metres): two barrels along Z (the left one through the fist, at the origin; the right
// one toward the palm's side, +X), -Z their objectives (forward), +Z their eyepieces; a bridge between them; the front
// tubes drawn out with the zoom. Each triangle with its face's normal and colour.
struct TRI
{
    glm::vec3 ap[3];
    glm::vec3 normal;
    glm::vec3 color;
};
constexpr float kxBarrelR = 0.068f;               // (the right barrel's axis; the left's at x 0)
constexpr float kzEyepiece = 0.075f, kzBody = 0.055f, kzFront = -0.06f;
constexpr int kcSide = 10;
const glm::vec3 kcolBody(0.17f, 0.23f, 0.21f), kcolRubber(0.05f, 0.05f, 0.055f), kcolTube(0.42f, 0.44f, 0.47f);
const glm::vec3 kcolGlass(0.16f, 0.36f, 0.55f), kcolBridge(0.12f, 0.15f, 0.14f);

// a tube (a kcSide-sided prism) about the axis at (x, y) from z0 to z1, radius r, its open ends left to the caps
void Tube(std::vector<TRI>* patri, float x, float r, float z0, float z1, const glm::vec3& col)
{
    for (int i = 0; i < kcSide; ++i)
    {
        const float a0 = glm::two_pi<float>() * float(i) / kcSide, a1 = glm::two_pi<float>() * float(i + 1) / kcSide;
        const glm::vec3 p00(x + r * std::cos(a0), r * std::sin(a0), z0), p01(x + r * std::cos(a0), r * std::sin(a0), z1);
        const glm::vec3 p10(x + r * std::cos(a1), r * std::sin(a1), z0), p11(x + r * std::cos(a1), r * std::sin(a1), z1);
        const float am = 0.5f * (a0 + a1);
        const glm::vec3 n(std::cos(am), std::sin(am), 0.0f);
        patri->push_back({{p00, p10, p11}, n, col});
        patri->push_back({{p00, p11, p01}, n, col});
    }
}
// a disc facing +Z (fFront false) or -Z at z, and a ring from r0 out to r1 around it (r0 0: the disc alone)
void Disc(std::vector<TRI>* patri, float x, float r0, float r1, float z, bool fFront, const glm::vec3& col)
{
    const glm::vec3 n(0.0f, 0.0f, fFront ? -1.0f : 1.0f);
    for (int i = 0; i < kcSide; ++i)
    {
        const float a0 = glm::two_pi<float>() * float(i) / kcSide, a1 = glm::two_pi<float>() * float(i + 1) / kcSide;
        const glm::vec3 o0(x + r1 * std::cos(a0), r1 * std::sin(a0), z), o1(x + r1 * std::cos(a1), r1 * std::sin(a1), z);
        if (r0 <= 0.0f) patri->push_back({{glm::vec3(x, 0.0f, z), o0, o1}, n, col});
        else
        {
            const glm::vec3 i0(x + r0 * std::cos(a0), r0 * std::sin(a0), z), i1(x + r0 * std::cos(a1), r0 * std::sin(a1), z);
            patri->push_back({{i0, o0, o1}, n, col});
            patri->push_back({{i0, o1, i1}, n, col});
        }
    }
}
void Box(std::vector<TRI>* patri, const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& col)
{
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
        patri->push_back({{a, b, c}, n, col});
        patri->push_back({{a, c, d}, n, col});
    };
    const glm::vec3 a = lo, b = hi;
    quad({a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}, {0, 0, -1});
    quad({a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}, {0, 0, 1});
    quad({a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z}, {0, -1, 0});
    quad({a.x, b.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}, {0, 1, 0});
    quad({a.x, a.y, a.z}, {a.x, b.y, a.z}, {a.x, b.y, b.z}, {a.x, a.y, b.z}, {-1, 0, 0});
    quad({b.x, a.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, {b.x, a.y, b.z}, {1, 0, 0});
}

std::vector<TRI> Model(float uExtend)
{
    std::vector<TRI> atri;
    const float zTube = kzFront - (0.012f + 0.05f * std::clamp(uExtend, 0.0f, 1.0f));   // (the front tube's end)
    for (const float x : {0.0f, kxBarrelR})
    {
        Tube(&atri, x, 0.016f, kzBody, kzEyepiece, kcolRubber);            // the eyepiece's cup
        Disc(&atri, x, 0.0f, 0.011f, kzEyepiece, false, kcolGlass * 0.5f);   // its glass
        Disc(&atri, x, 0.011f, 0.016f, kzEyepiece, false, kcolRubber);
        Disc(&atri, x, 0.016f, 0.024f, kzBody, false, kcolBody);            // the body's back
        Tube(&atri, x, 0.024f, kzFront, kzBody, kcolBody);                  // the body
        Disc(&atri, x, 0.021f, 0.024f, kzFront, true, kcolBody);            // its front, round the tube
        Tube(&atri, x, 0.021f, zTube, kzFront, kcolTube);                   // the front tube (telescopic)
        Disc(&atri, x, 0.0f, 0.018f, zTube, true, kcolGlass);               // the objective's glass
        Disc(&atri, x, 0.018f, 0.021f, zTube, true, kcolTube * 0.8f);
    }
    Box(&atri, glm::vec3(0.012f, -0.009f, -0.03f), glm::vec3(kxBarrelR - 0.012f, 0.009f, 0.04f), kcolBridge);   // the bridge
    return atri;
}
}  // namespace

glm::vec3 PosEyepieces() { return glm::vec3(0.5f * kxBarrelR, 0.0f, kzEyepiece); }

void DrawMesh(const glm::mat4& matClip, const glm::mat3& matTurn, float uExtend)
{
    if (!FProgram()) return;
    const std::vector<TRI> atri = Model(uExtend);
    std::vector<VERT> av;
    av.reserve(atri.size() * 3);
    const glm::vec3 dirLight = glm::normalize(glm::vec3(0.3f, 0.2f, 1.0f));   // (the world's up: Z)
    for (const TRI& tri : atri)
    {
        const glm::vec3 n = glm::normalize(matTurn * tri.normal);
        // (two-sided: a face turned away is lit as its back; a soft ambient so the shade side is not black)
        const float l = 0.45f + 0.55f * std::fabs(glm::dot(n, dirLight));
        const glm::vec4 col(tri.color * l, 1.0f);
        for (const glm::vec3& p : tri.ap) av.push_back({matClip * glm::vec4(p, 1.0f), col});
    }
    // (wholly behind the eye: nothing to draw; partly, the clipper cuts it)
    bool fAny = false;
    for (const VERT& v : av) fAny = fAny || v.pos.w > 1e-3f;
    if (!fAny) return;
    SAVEDGL saved;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.FboScene());
    glViewport(0, 0, g_gl.renderWidth, g_gl.renderHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL);   // (reversed Z: greater is nearer)
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(s_prog);
    Upload(av);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(av.size()));
}

void DrawLensMask()
{
    if (!FProgram()) return;
    const float dx = float(std::max(g_gl.renderWidth, 1)), dy = float(std::max(g_gl.renderHeight, 1));
    const float sx = std::min(dx, dy) / dx, sy = std::min(dx, dy) / dy;   // (the centred square's half extent, NDC)
    std::vector<VERT> av;
    // a ring from r0 to r1 (the square's own units: 1 its edge's middle) in colour col
    auto ring = [&](float r0, float r1, const glm::vec4& col) {
        constexpr int kc = 64;
        for (int i = 0; i < kc; ++i)
        {
            const float a0 = glm::two_pi<float>() * float(i) / kc, a1 = glm::two_pi<float>() * float(i + 1) / kc;
            auto at = [&](float r, float a) { return glm::vec4(r * std::cos(a) * sx, r * std::sin(a) * sy, 0.0f, 1.0f); };
            av.push_back({at(r0, a0), col});
            av.push_back({at(r1, a0), col});
            av.push_back({at(r1, a1), col});
            av.push_back({at(r0, a0), col});
            av.push_back({at(r1, a1), col});
            av.push_back({at(r0, a1), col});
        }
    };
    ring(1.0f, 1.6f, glm::vec4(0.0f));                           // outside: transparent (past the square's corners)
    ring(0.955f, 1.0f, glm::vec4(0.02f, 0.02f, 0.025f, 1.0f));   // the rim
    ring(0.9f, 0.955f, glm::vec4(0.0f, 0.0f, 0.0f, 0.35f));      // a soft edge inside it (premultiplied: darker)
    SAVEDGL saved;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.FboScene());
    glViewport(0, 0, g_gl.renderWidth, g_gl.renderHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    // (the scene's alpha is no coverage: all of it opaque first, then the rings over it)
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    GLfloat acolClear[4]{};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, acolClear);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(acolClear[0], acolClear[1], acolClear[2], acolClear[3]);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(s_prog);
    Upload(av);
    const GLsizei cvRing = GLsizei(av.size() / 3);   // (each ring's vertices: three rings, the same size)
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, 2 * cvRing);   // (the outside and the rim replace what is there)
    GLint srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0, eqRgb = 0, eqA = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqA);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);   // (the soft edge darkens; alpha stays 1)
    glDrawArrays(GL_TRIANGLES, 2 * cvRing, cvRing);
    glBlendEquationSeparate(GLenum(eqRgb), GLenum(eqA));
    glBlendFuncSeparate(GLenum(srcRgb), GLenum(dstRgb), GLenum(srcA), GLenum(dstA));
}

void FreeGL()
{
    if (s_vbo) glDeleteBuffers(1, &s_vbo);
    if (s_vao) glDeleteVertexArrays(1, &s_vao);
    if (s_prog) glDeleteProgram(s_prog);
    s_vbo = s_vao = s_prog = 0;
    s_cvertBuf = 0;
}
}  // namespace vrbinoc
