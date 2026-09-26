#pragma once
#include "ColorManager.h"
#include "Image.h"

#include <string>
#include <vector>

enum class ChannelMode : int { RGB = 0, Red, Green, Blue, Alpha, Luma };
// Cryptomatte presentation (the mask travels in the image alpha).
enum class MatteMode : int { Off = 0, Ids, Overlay, Masked, Matte };

// Draws the current frame through an OCIO-generated GLSL shader.
class GLViewer {
public:
    bool init(std::string& err);
    void shutdown();

    void setImage(const ImagePtr& img);          // uploads only when the pointer changes
    const ImagePtr& image() const { return m_image; }

    // null processor = passthrough (no color management).
    bool setProcessor(const OCIO::ConstGPUProcessorRcPtr& gpu, std::string& err);
    void setExposure(float stops);
    void setGamma(float gamma);
    void setMatteMode(MatteMode m) { m_matte = m; }

    // viewport in framebuffer pixels (origin bottom-left). zoom = screen px per image px.
    void draw(int fbW, int fbH, int vx, int vy, int vw, int vh,
              float zoom, float panX, float panY, ChannelMode channel);

    // Renders the image 1:1 through the current color pipeline into an offscreen
    // buffer and reads it back as packed RGB (8 or 16 bit per channel), top row first.
    // withAlpha: RGBA readback (alpha = image alpha / Cryptomatte mask).
    bool renderToMemory(const ImagePtr& img, ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out);

private:
    void drawQuad(float x0, float y0, float x1, float y1, bool nearest, ChannelMode channel, bool outAlpha = false);
    struct LutTex { unsigned id = 0; unsigned target = 0; std::string sampler; };
    struct Uniform { int loc = -1; OCIO::GpuShaderDesc::UniformData data; };

    bool buildProgram(const std::string& ocioText, std::string& err);
    void releaseLuts();

    unsigned m_vao = 0, m_vbo = 0;
    unsigned m_program = 0;
    unsigned m_tex = 0;
    unsigned m_fbo = 0, m_fboTex = 0;
    int m_fboW = 0, m_fboH = 0;
    bool m_fbo16 = false;
    int m_texW = 0, m_texH = 0;
    PixelType m_texType = PixelType::U8;
    ImagePtr m_image;

    int m_locRect = -1, m_locImage = -1, m_locChannel = -1, m_locMatte = -1, m_locOutAlpha = -1;
    MatteMode m_matte = MatteMode::Off;
    std::vector<LutTex> m_luts;
    std::vector<Uniform> m_uniforms;
    OCIO::GpuShaderDescRcPtr m_shaderDesc;
    OCIO::DynamicPropertyDoubleRcPtr m_dynExposure, m_dynGamma;
};
