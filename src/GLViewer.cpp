#include <GL/glew.h>
#include "GLViewer.h"

#include <algorithm>
#include <cmath>

static const char* kVertexShader = R"(#version 400 core
layout(location = 0) in vec2 aPos;
uniform vec4 uRect;          // NDC x0, y0, x1, y1
out vec2 vUV;
void main() {
    gl_Position = vec4(mix(uRect.xy, uRect.zw, aPos), 0.0, 1.0);
    vUV = vec2(aPos.x, 1.0 - aPos.y);
}
)";

static const char* kPassthrough = R"(
vec4 OCIODisplay(vec4 c) { return c; }
)";

static const char* kFragmentMain = R"(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uImage;
uniform int uChannel;
uniform int uMatte;     // 0 off, 1 ID colors, 2 overlay, 3 masked, 4 matte (mask in alpha)
uniform int uOutAlpha;  // 0 opaque, 1 alpha with straight color, 2 alpha with premultiplied color (export)
uniform int uAlphaMode; // 0 opaque or mask in alpha, 1 straight color, 2 premultiplied color
void main() {
    vec4 src = texture(uImage, vUV);
    float a = src.a;
    // Shown over black; with alpha the view runs on straight color (premultiplied after it).
    if (uOutAlpha != 0) { if (uAlphaMode == 2 && a > 0.0) src.rgb /= a; }
    else if (uAlphaMode == 1) src.rgb *= a;
    if (uChannel == 4 || uMatte == 4) { fragColor = vec4(a, a, a, uOutAlpha != 0 ? a : 1.0); return; }
    if (uMatte == 1) {
        // False-color IDs are display values: no color transform; selection glows.
        vec3 c = mix(src.rgb * 0.55, vec3(1.0), a * 0.55);
        fragColor = vec4(c, 1.0);
        return;
    }
    vec3 rgb = uMatte == 3 && uOutAlpha == 0 ? src.rgb * a : src.rgb;   // mask in scene-linear, before the view
    vec4 c = OCIODisplay(vec4(rgb, 1.0));
    if (uMatte == 2) c.rgb = mix(c.rgb * 0.3, c.rgb, a) + vec3(0.06, 0.12, 0.3) * a;
    if (uChannel == 1) c.rgb = c.rrr;
    else if (uChannel == 2) c.rgb = c.ggg;
    else if (uChannel == 3) c.rgb = c.bbb;
    else if (uChannel == 5) c.rgb = vec3(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)));
    fragColor = vec4(uOutAlpha == 2 ? c.rgb * a : c.rgb, uOutAlpha != 0 ? a : 1.0);
}
)";

static const char* kInputPassthrough = R"(
vec4 OCIOInput(vec4 c) { return c; }
)";

// One composite layer, written premultiplied for the blend state of its mode.
static const char* kLayerMain = R"(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uImage;
uniform float uGain;
uniform float uOpacity;
uniform int uMode;       // BlendMode (3 = multiply)
uniform int uStraight;   // color not premultiplied by alpha (8/16-bit files)
void main() {
    vec4 s = texture(uImage, vUV);
    vec3 c = OCIOInput(vec4(s.rgb, 1.0)).rgb * uGain;
    if (uStraight != 0) c *= s.a;
    if (uMode == 3) { fragColor = vec4(mix(vec3(1.0), c, uOpacity), 0.0); return; }
    fragColor = vec4(c * uOpacity, s.a * uOpacity);
}
)";

static unsigned Compile(GLenum type, const std::string& src, std::string& err)
{
    unsigned s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        err = log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static unsigned Link(const std::string& fragment, std::string& err)
{
    unsigned vs = Compile(GL_VERTEX_SHADER, kVertexShader, err);
    if (!vs) return 0;
    unsigned f = Compile(GL_FRAGMENT_SHADER, fragment, err);
    if (!f) { glDeleteShader(vs); return 0; }
    unsigned prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(f);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        err = log;
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

unsigned GLViewer::createTexture()
{
    unsigned t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

bool GLViewer::init(std::string& err, void* dc, void* context)
{
    m_dc = dc;
    m_context = context;
    const float quad[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    return buildOcioProgram(m_display, nullptr, "OCIODisplay", kPassthrough, kFragmentMain, err);
}

void GLViewer::shutdown()
{
    m_uploader.stop();
    for (auto& t : m_pool) glDeleteTextures(1, &t.id);
    m_pool.clear();
    releaseProgram(m_display);
    for (auto& p : m_inputs) releaseProgram(p);
    m_inputs.clear();
    for (unsigned* t : { &m_fboTex, &m_compTex })
        if (*t) glDeleteTextures(1, t);
    for (unsigned* f : { &m_fbo, &m_compFbo })
        if (*f) glDeleteFramebuffers(1, f);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_fbo = m_fboTex = m_compFbo = m_compTex = m_vbo = m_vao = 0;
}

void GLViewer::releaseProgram(Program& p)
{
    for (auto& l : p.luts) glDeleteTextures(1, &l.id);
    if (p.id) glDeleteProgram(p.id);
    p = Program();
}

bool GLViewer::buildOcioProgram(Program& p, const OCIO::ConstGPUProcessorRcPtr& gpu, const char* function,
                                const char* passthrough, const char* main, std::string& err)
{
    Program np;
    std::string colorText = passthrough;
    try {
        if (gpu) {
            np.desc = OCIO::GpuShaderDesc::CreateShaderDesc();
            np.desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_4_0);
            np.desc->setFunctionName(function);
            np.desc->setResourcePrefix("ocio_");
            gpu->extractGpuShaderInfo(np.desc);
            colorText = np.desc->getShaderText();
        }
        np.id = Link("#version 400 core\n" + colorText + main, err);
        if (!np.id) return false;

        if (np.desc) {
            // LUT textures. Unit 0 is the image.
            const auto& desc = np.desc;
            unsigned unit = 1;
            for (unsigned i = 0; i < desc->getNum3DTextures(); ++i, ++unit) {
                const char *texName = nullptr, *sampler = nullptr;
                unsigned edge = 0;
                OCIO::Interpolation interp = OCIO::INTERP_LINEAR;
                desc->get3DTexture(i, texName, sampler, edge, interp);
                const float* values = nullptr;
                desc->get3DTextureValues(i, values);
                LutTex t{ 0, GL_TEXTURE_3D, sampler };
                glGenTextures(1, &t.id);
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_3D, t.id);
                const GLint filter = interp == OCIO::INTERP_NEAREST ? GL_NEAREST : GL_LINEAR;
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, edge, edge, edge, 0, GL_RGB, GL_FLOAT, values);
                np.luts.push_back(t);
            }
            for (unsigned i = 0; i < desc->getNumTextures(); ++i, ++unit) {
                const char *texName = nullptr, *sampler = nullptr;
                unsigned w = 0, h = 0;
                OCIO::GpuShaderDesc::TextureType channel = OCIO::GpuShaderDesc::TEXTURE_RGB_CHANNEL;
                OCIO::GpuShaderDesc::TextureDimensions dims = OCIO::GpuShaderDesc::TEXTURE_2D;
                OCIO::Interpolation interp = OCIO::INTERP_LINEAR;
                desc->getTexture(i, texName, sampler, w, h, channel, dims, interp);
                const float* values = nullptr;
                desc->getTextureValues(i, values);
                const bool red = channel == OCIO::GpuShaderDesc::TEXTURE_RED_CHANNEL;
                const GLint internal = red ? GL_R32F : GL_RGB32F;
                const GLenum format = red ? GL_RED : GL_RGB;
                const GLint filter = interp == OCIO::INTERP_NEAREST ? GL_NEAREST : GL_LINEAR;
                LutTex t{ 0, dims == OCIO::GpuShaderDesc::TEXTURE_1D ? (unsigned)GL_TEXTURE_1D : (unsigned)GL_TEXTURE_2D, sampler };
                glGenTextures(1, &t.id);
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(t.target, t.id);
                glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, filter);
                glTexParameteri(t.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(t.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (t.target == GL_TEXTURE_1D)
                    glTexImage1D(GL_TEXTURE_1D, 0, internal, w, 0, format, GL_FLOAT, values);
                else
                    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_FLOAT, values);
                np.luts.push_back(t);
            }
            glActiveTexture(GL_TEXTURE0);

            for (unsigned i = 0; i < desc->getNumUniforms(); ++i) {
                Uniform u;
                const char* name = desc->getUniform(i, u.data);
                u.loc = glGetUniformLocation(np.id, name);
                np.uniforms.push_back(u);
            }
        }
    } catch (const std::exception& e) {
        err = e.what();
        releaseProgram(np);
        return false;
    }
    releaseProgram(p);
    p = std::move(np);
    p.locRect = glGetUniformLocation(p.id, "uRect");
    p.locImage = glGetUniformLocation(p.id, "uImage");
    p.locChannel = glGetUniformLocation(p.id, "uChannel");
    p.locMatte = glGetUniformLocation(p.id, "uMatte");
    p.locOutAlpha = glGetUniformLocation(p.id, "uOutAlpha");
    p.locAlphaMode = glGetUniformLocation(p.id, "uAlphaMode");
    p.locGain = glGetUniformLocation(p.id, "uGain");
    p.locOpacity = glGetUniformLocation(p.id, "uOpacity");
    p.locMode = glGetUniformLocation(p.id, "uMode");
    p.locStraight = glGetUniformLocation(p.id, "uStraight");
    return true;
}

bool GLViewer::setProcessor(const OCIO::ConstGPUProcessorRcPtr& gpu, std::string& err)
{
    m_dynExposure.reset();
    m_dynGamma.reset();
    if (!buildOcioProgram(m_display, gpu, "OCIODisplay", kPassthrough, kFragmentMain, err)) {
        std::string dummy;
        buildOcioProgram(m_display, nullptr, "OCIODisplay", kPassthrough, kFragmentMain, dummy);
        return false;
    }
    if (const auto& desc = m_display.desc) {
        if (desc->hasDynamicProperty(OCIO::DYNAMIC_PROPERTY_EXPOSURE)) {
            auto p = desc->getDynamicProperty(OCIO::DYNAMIC_PROPERTY_EXPOSURE);
            m_dynExposure = OCIO::DynamicPropertyValue::AsDouble(p);
        }
        if (desc->hasDynamicProperty(OCIO::DYNAMIC_PROPERTY_GAMMA)) {
            auto p = desc->getDynamicProperty(OCIO::DYNAMIC_PROPERTY_GAMMA);
            m_dynGamma = OCIO::DynamicPropertyValue::AsDouble(p);
        }
    }
    return true;
}

bool GLViewer::setInputTransforms(const std::vector<OCIO::ConstGPUProcessorRcPtr>& procs, std::string& err)
{
    for (auto& p : m_inputs) releaseProgram(p);
    m_inputs.assign(procs.size(), Program());
    bool ok = true;
    for (size_t i = 0; i < procs.size(); ++i)
        if (!buildOcioProgram(m_inputs[i], procs[i], "OCIOInput", kInputPassthrough, kLayerMain, err)) {
            ok = false;
            std::string dummy;
            buildOcioProgram(m_inputs[i], nullptr, "OCIOInput", kInputPassthrough, kLayerMain, dummy);
        }
    m_compDirty = true;
    return ok;
}

void GLViewer::setExposure(float stops)
{
    if (m_dynExposure) m_dynExposure->setValue(stops);
}

void GLViewer::setGamma(float gamma)
{
    // ExposureContrast raises to the power of `gamma`; UI gamma > 1 should brighten.
    if (m_dynGamma) m_dynGamma->setValue(1.0 / std::max(0.01f, gamma));
}

void GLViewer::setImage(const ImagePtr& img)
{
    m_useComp = false;
    m_image = img;
}

void GLViewer::setComposite(const std::vector<CompLayer>& layers, int width, int height, int fullWidth, int fullHeight)
{
    m_useComp = true;
    m_compFullW = fullWidth;
    m_compFullH = fullHeight;
    auto same = [](const CompLayer& a, const CompLayer& b) {
        return a.image == b.image && a.transform == b.transform && a.blend == b.blend && a.opacity == b.opacity && a.gain == b.gain &&
               a.straight == b.straight;
    };
    if (width == m_compW && height == m_compH && std::equal(layers.begin(), layers.end(), m_comp.begin(), m_comp.end(), same))
        return;
    m_comp = layers;
    m_compW = width;
    m_compH = height;
    m_compDirty = true;
}

// ---------------------------------------------------------------------------
// Texture pool: one texture per image of the current frame and of the next one.

bool GLViewer::needed(const Tex& t) const
{
    if (!t.image) return false;
    if (m_useComp) {
        for (const CompLayer& l : m_comp)
            if (l.image == t.image) return true;
    } else if (t.image == m_image) {
        return true;
    }
    return std::find(m_prefetch.begin(), m_prefetch.end(), t.image) != m_prefetch.end();
}

bool GLViewer::finishJob(Tex& t)
{
    if (!t.job) return true;
    m_uploader.wait(t.job);   // usually long done: the worker uploads ahead
    const bool uploaded = t.job->uploaded;
    if (!uploaded) t.w = t.h = 0;   // the worker stopped first: storage unknown
    t.job.reset();
    return uploaded;
}

GLViewer::Tex& GLViewer::slotFor(const ImagePtr& img)
{
    // Prefer a free texture with the same storage: no reallocation.
    Tex* best = nullptr;
    for (Tex& t : m_pool) {
        if (needed(t)) continue;
        if (!best || (t.w == img->width && t.h == img->height && t.type == img->type)) best = &t;
    }
    if (!best) {
        m_pool.push_back(Tex());
        best = &m_pool.back();
        best->id = createTexture();
    }
    finishJob(*best);   // a stale prefetch may still write into it
    best->image = img;
    return *best;
}

unsigned GLViewer::texture(const ImagePtr& img)
{
    auto it = std::find_if(m_pool.begin(), m_pool.end(), [&](const Tex& t) { return t.image == img; });
    Tex* t;
    if (it != m_pool.end()) {
        if (finishJob(*it)) return it->id;
        t = &*it;
    } else {
        t = &slotFor(img);
    }
    const bool allocate = t->w != img->width || t->h != img->height || t->type != img->type;
    glActiveTexture(GL_TEXTURE0);
    UploadTexture(t->id, *img, allocate);
    t->w = img->width;
    t->h = img->height;
    t->type = img->type;
    return t->id;
}

void GLViewer::prefetch(const std::vector<ImagePtr>& images)
{
    m_prefetch.clear();
    for (const ImagePtr& img : images)
        if (img && img->valid() && std::find(m_prefetch.begin(), m_prefetch.end(), img) == m_prefetch.end()) m_prefetch.push_back(img);
    if (!m_prefetch.empty() && !m_uploader.ready()) {
        m_uploader.start(m_dc, m_context);   // first playback: the worker context comes up meanwhile
        return;
    }
    for (const ImagePtr& img : m_prefetch) {
        if (std::any_of(m_pool.begin(), m_pool.end(), [&](const Tex& t) { return t.image == img; })) continue;
        Tex& t = slotFor(img);
        auto job = std::make_shared<TextureUploader::Job>();
        job->texture = t.id;
        job->image = img;
        job->allocate = t.w != img->width || t.h != img->height || t.type != img->type;
        t.w = img->width;
        t.h = img->height;
        t.type = img->type;
        t.job = job;
        m_uploader.submit(job);
    }
    trimPool();
}

void GLViewer::trimPool()
{
    // Keep one spare texture for the frame after next; free the rest (and their images).
    int spare = 0;
    for (auto it = m_pool.begin(); it != m_pool.end();) {
        if (needed(*it) || it->job || spare++ < 1) { ++it; continue; }
        glDeleteTextures(1, &it->id);
        it = m_pool.erase(it);
    }
}

void GLViewer::bindOcio(const Program& p)
{
    for (size_t i = 0; i < p.luts.size(); ++i) {
        glActiveTexture(GL_TEXTURE1 + (GLenum)i);
        glBindTexture(p.luts[i].target, p.luts[i].id);
        glUniform1i(glGetUniformLocation(p.id, p.luts[i].sampler.c_str()), 1 + (int)i);
    }
    for (auto& u : p.uniforms) {
        if (u.loc < 0) continue;
        const auto& d = u.data;
        switch (d.m_type) {
        case OCIO::UNIFORM_DOUBLE: glUniform1f(u.loc, (float)d.m_getDouble()); break;
        case OCIO::UNIFORM_BOOL: glUniform1i(u.loc, d.m_getBool() ? 1 : 0); break;
        case OCIO::UNIFORM_FLOAT3: { auto& v = d.m_getFloat3(); glUniform3f(u.loc, v[0], v[1], v[2]); break; }
        case OCIO::UNIFORM_VECTOR_FLOAT: glUniform1fv(u.loc, d.m_vectorFloat.m_getSize(), d.m_vectorFloat.m_getVector()); break;
        case OCIO::UNIFORM_VECTOR_INT: glUniform1iv(u.loc, d.m_vectorInt.m_getSize(), d.m_vectorInt.m_getVector()); break;
        default: break;
        }
    }
    glActiveTexture(GL_TEXTURE0);
}

void GLViewer::compose()
{
    if (!m_compDirty) return;
    m_compDirty = false;
    if (m_compW <= 0 || m_compH <= 0) return;

    if (!m_compFbo || m_compTexW != m_compW || m_compTexH != m_compH) {
        if (!m_compFbo) glGenFramebuffers(1, &m_compFbo);
        if (m_compTex) glDeleteTextures(1, &m_compTex);
        m_compTex = createTexture();
        // Float target: blending keeps scene-linear values above 1 and below 0.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_compW, m_compH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_compFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_compTex, 0);
        m_compTexW = m_compW;
        m_compTexH = m_compH;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_compFbo);
    glViewport(0, 0, m_compW, m_compH);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBindVertexArray(m_vao);
    for (const CompLayer& l : m_comp) {
        if (!l.image || !l.image->valid() || l.transform < 0 || l.transform >= (int)m_inputs.size()) continue;
        const Program& p = m_inputs[l.transform];
        if (!p.id) continue;
        const unsigned tex = texture(l.image);

        // Premultiplied layer color (see kLayerMain) combined with the canvas.
        // Only Normal changes coverage: the other modes keep the alpha below.
        switch (l.blend) {
        case BlendMode::Normal:
            glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Add:
            glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            break;
        case BlendMode::Subtract:
            glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            break;
        case BlendMode::Multiply:
            glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
            break;
        default:   // Screen: a + b - a * b
            glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
            break;
        }
        glUseProgram(p.id);
        glUniform4f(p.locRect, -1.0f, 1.0f, 1.0f, -1.0f);   // image top row -> row 0, like the uploads
        glUniform1i(p.locImage, 0);
        glUniform1f(p.locGain, l.gain);
        glUniform1f(p.locOpacity, std::clamp(l.opacity, 0.0f, 1.0f));
        glUniform1i(p.locMode, (int)l.blend);
        glUniform1i(p.locStraight, l.straight && l.image->hasAlpha ? 1 : 0);
        bindOcio(p);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        // Layers with another resolution are stretched over the canvas.
        const GLint filter = l.image->width == m_compW && l.image->height == m_compH ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLViewer::draw(int fbW, int fbH, int vx, int vy, int vw, int vh,
                    float zoom, float panX, float panY, ChannelMode channel)
{
    // Source texture and its size on screen (the file size: a proxy is drawn scaled up).
    unsigned tex = 0;
    int texW = 0, fullW = 0, fullH = 0;
    if (m_useComp) {
        compose();
        tex = m_compTex;
        texW = m_compW;
        fullW = m_compFullW;
        fullH = m_compFullH;
    } else if (m_image && m_image->valid()) {
        tex = texture(m_image);
        texW = m_image->width;
        fullW = m_image->fullWidth();
        fullH = m_image->fullHeight();
    }

    glViewport(0, 0, fbW, fbH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glClearColor(0.075f, 0.075f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (tex && texW > 0 && fullW > 0 && m_display.id) {
        const float w = fullW * zoom, h = fullH * zoom;
        const float cx = vx + vw * 0.5f + panX, cy = vy + vh * 0.5f + panY;
        // Snap to whole pixels for crisp 1:1 display; a proxy is always filtered.
        const float x0 = std::floor(cx - w * 0.5f), y0 = std::floor(cy - h * 0.5f);
        const float x1 = x0 + w, y1 = y0 + h;
        const bool nearest = zoom >= 1.0f && texW == fullW;
        drawDisplay(x0 / fbW * 2 - 1, y0 / fbH * 2 - 1, x1 / fbW * 2 - 1, y1 / fbH * 2 - 1, nearest, channel, false, tex);
    }
    glDisable(GL_SCISSOR_TEST);
}

void GLViewer::drawDisplay(float x0, float y0, float x1, float y1, bool nearest, ChannelMode channel, bool outAlpha, unsigned tex)
{
    const Program& p = m_display;
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(p.id);
    glUniform4f(p.locRect, x0, y0, x1, y1);
    glUniform1i(p.locImage, 0);
    glUniform1i(p.locChannel, (int)channel);
    glUniform1i(p.locMatte, m_useComp ? 0 : (int)m_matte);
    glUniform1i(p.locOutAlpha, !outAlpha ? 0 : m_premultOut ? 2 : 1);
    // The composite is premultiplied; a Cryptomatte mask replaces the image alpha.
    glUniform1i(p.locAlphaMode, m_useComp ? (int)AlphaMode::Premultiplied : m_matte != MatteMode::Off ? 0 : (int)m_alpha);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    bindOcio(p);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

bool GLViewer::renderToMemory(const ImagePtr& img, ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out)
{
    if (!img || !img->valid()) return false;
    setImage(img);
    return renderSource(false, channel, sixteenBit, withAlpha, out);
}

bool GLViewer::renderCompositeToMemory(ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out)
{
    return m_useComp && renderSource(true, channel, sixteenBit, withAlpha, out);
}

bool GLViewer::renderSource(bool composite, ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out)
{
    if (composite) compose();
    const unsigned tex = composite ? m_compTex : m_image && m_image->valid() ? texture(m_image) : 0;
    const int w = composite ? m_compW : m_image ? m_image->width : 0, h = composite ? m_compH : m_image ? m_image->height : 0;
    if (!tex || w <= 0 || h <= 0 || !m_display.id) return false;

    if (!m_fbo || m_fboW != w || m_fboH != h || m_fbo16 != sixteenBit) {
        if (!m_fbo) glGenFramebuffers(1, &m_fbo);
        if (m_fboTex) glDeleteTextures(1, &m_fboTex);
        glGenTextures(1, &m_fboTex);
        glBindTexture(GL_TEXTURE_2D, m_fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, sixteenBit ? GL_RGBA16 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fboTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
        m_fboW = w;
        m_fboH = h;
        m_fbo16 = sixteenBit;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    // Image top row lands in framebuffer row 0, so glReadPixels returns top-down rows.
    drawDisplay(-1.0f, 1.0f, 1.0f, -1.0f, true, channel, withAlpha, tex);

    const int comps = withAlpha ? 4 : 3;
    out.resize(size_t(w) * h * comps * (sixteenBit ? 2 : 1));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, withAlpha ? GL_RGBA : GL_RGB, sixteenBit ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, out.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}
