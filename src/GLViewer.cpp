#include <GL/glew.h>
#include "GLViewer.h"

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
uniform int uOutAlpha;  // write alpha (export with alpha)
void main() {
    vec4 src = texture(uImage, vUV);
    float a = src.a;
    if (uChannel == 4 || uMatte == 4) { fragColor = vec4(a, a, a, uOutAlpha != 0 ? a : 1.0); return; }
    if (uMatte == 1) {
        // False-color IDs are display values: no color transform; selection glows.
        vec3 c = mix(src.rgb * 0.55, vec3(1.0), a * 0.55);
        fragColor = vec4(c, 1.0);
        return;
    }
    vec3 rgb = uMatte == 3 ? src.rgb * a : src.rgb;   // mask in scene-linear, before the view
    vec4 c = OCIODisplay(vec4(rgb, 1.0));
    if (uMatte == 2) c.rgb = mix(c.rgb * 0.3, c.rgb, a) + vec3(0.06, 0.12, 0.3) * a;
    if (uChannel == 1) c.rgb = c.rrr;
    else if (uChannel == 2) c.rgb = c.ggg;
    else if (uChannel == 3) c.rgb = c.bbb;
    else if (uChannel == 5) c.rgb = vec3(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)));
    fragColor = vec4(c.rgb, uOutAlpha != 0 ? a : 1.0);
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

bool GLViewer::init(std::string& err)
{
    const float quad[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return buildProgram(kPassthrough, err);
}

void GLViewer::shutdown()
{
    releaseLuts();
    if (m_fboTex) glDeleteTextures(1, &m_fboTex);
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    m_fbo = m_fboTex = 0;
    if (m_program) glDeleteProgram(m_program);
    if (m_tex) glDeleteTextures(1, &m_tex);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_program = m_tex = m_vbo = m_vao = 0;
}

void GLViewer::releaseLuts()
{
    for (auto& l : m_luts) glDeleteTextures(1, &l.id);
    m_luts.clear();
    m_uniforms.clear();
}

bool GLViewer::buildProgram(const std::string& ocioText, std::string& err)
{
    std::string fs = "#version 400 core\n" + ocioText + kFragmentMain;
    unsigned vs = Compile(GL_VERTEX_SHADER, kVertexShader, err);
    if (!vs) return false;
    unsigned f = Compile(GL_FRAGMENT_SHADER, fs, err);
    if (!f) { glDeleteShader(vs); return false; }
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
        return false;
    }
    if (m_program) glDeleteProgram(m_program);
    m_program = prog;
    m_locRect = glGetUniformLocation(prog, "uRect");
    m_locImage = glGetUniformLocation(prog, "uImage");
    m_locChannel = glGetUniformLocation(prog, "uChannel");
    m_locMatte = glGetUniformLocation(prog, "uMatte");
    m_locOutAlpha = glGetUniformLocation(prog, "uOutAlpha");
    return true;
}

bool GLViewer::setProcessor(const OCIO::ConstGPUProcessorRcPtr& gpu, std::string& err)
{
    releaseLuts();
    m_shaderDesc.reset();
    m_dynExposure.reset();
    m_dynGamma.reset();

    if (!gpu) return buildProgram(kPassthrough, err);

    try {
        auto desc = OCIO::GpuShaderDesc::CreateShaderDesc();
        desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_4_0);
        desc->setFunctionName("OCIODisplay");
        desc->setResourcePrefix("ocio_");
        gpu->extractGpuShaderInfo(desc);

        if (!buildProgram(desc->getShaderText(), err)) {
            buildProgram(kPassthrough, err);
            return false;
        }

        // Upload LUT textures. Unit 0 is the image.
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
            m_luts.push_back(t);
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
            m_luts.push_back(t);
        }
        glActiveTexture(GL_TEXTURE0);

        for (unsigned i = 0; i < desc->getNumUniforms(); ++i) {
            Uniform u;
            const char* name = desc->getUniform(i, u.data);
            u.loc = glGetUniformLocation(m_program, name);
            m_uniforms.push_back(u);
        }

        if (desc->hasDynamicProperty(OCIO::DYNAMIC_PROPERTY_EXPOSURE)) {
            auto p = desc->getDynamicProperty(OCIO::DYNAMIC_PROPERTY_EXPOSURE);
            m_dynExposure = OCIO::DynamicPropertyValue::AsDouble(p);
        }
        if (desc->hasDynamicProperty(OCIO::DYNAMIC_PROPERTY_GAMMA)) {
            auto p = desc->getDynamicProperty(OCIO::DYNAMIC_PROPERTY_GAMMA);
            m_dynGamma = OCIO::DynamicPropertyValue::AsDouble(p);
        }
        m_shaderDesc = desc;
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        releaseLuts();
        std::string dummy;
        buildProgram(kPassthrough, dummy);
        return false;
    }
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
    if (img == m_image) return;
    m_image = img;
    if (!img || !img->valid()) return;

    GLint internal; GLenum type;
    switch (img->type) {
    case PixelType::U8:  internal = GL_RGBA8;   type = GL_UNSIGNED_BYTE;  break;
    case PixelType::U16: internal = GL_RGBA16;  type = GL_UNSIGNED_SHORT; break;
    default:             internal = GL_RGBA16F; type = GL_HALF_FLOAT;     break;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (img->width == m_texW && img->height == m_texH && img->type == m_texType) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img->width, img->height, GL_RGBA, type, img->data.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internal, img->width, img->height, 0, GL_RGBA, type, img->data.data());
        m_texW = img->width;
        m_texH = img->height;
        m_texType = img->type;
    }
}

void GLViewer::draw(int fbW, int fbH, int vx, int vy, int vw, int vh,
                    float zoom, float panX, float panY, ChannelMode channel)
{
    glViewport(0, 0, fbW, fbH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glClearColor(0.075f, 0.075f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_image && m_image->valid() && m_program && m_texW > 0) {
        const float w = m_texW * zoom, h = m_texH * zoom;
        const float cx = vx + vw * 0.5f + panX, cy = vy + vh * 0.5f + panY;
        // Snap to whole pixels for crisp 1:1 display.
        const float x0 = std::floor(cx - w * 0.5f), y0 = std::floor(cy - h * 0.5f);
        const float x1 = x0 + w, y1 = y0 + h;
        drawQuad(x0 / fbW * 2 - 1, y0 / fbH * 2 - 1, x1 / fbW * 2 - 1, y1 / fbH * 2 - 1, zoom >= 1.0f, channel);
    }
    glDisable(GL_SCISSOR_TEST);
}

void GLViewer::drawQuad(float x0, float y0, float x1, float y1, bool nearest, ChannelMode channel, bool outAlpha)
{
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(m_program);
    glUniform4f(m_locRect, x0, y0, x1, y1);
    glUniform1i(m_locImage, 0);
    glUniform1i(m_locChannel, (int)channel);
    glUniform1i(m_locMatte, (int)m_matte);
    glUniform1i(m_locOutAlpha, outAlpha ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);

    for (size_t i = 0; i < m_luts.size(); ++i) {
        glActiveTexture(GL_TEXTURE1 + (GLenum)i);
        glBindTexture(m_luts[i].target, m_luts[i].id);
        glUniform1i(glGetUniformLocation(m_program, m_luts[i].sampler.c_str()), 1 + (int)i);
    }
    for (auto& u : m_uniforms) {
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

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

bool GLViewer::renderToMemory(const ImagePtr& img, ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out)
{
    if (!img || !img->valid() || !m_program) return false;
    setImage(img);
    const int w = img->width, h = img->height;

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
    drawQuad(-1.0f, 1.0f, 1.0f, -1.0f, true, channel, withAlpha);

    const int comps = withAlpha ? 4 : 3;
    out.resize(size_t(w) * h * comps * (sixteenBit ? 2 : 1));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, withAlpha ? GL_RGBA : GL_RGB, sixteenBit ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, out.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}
