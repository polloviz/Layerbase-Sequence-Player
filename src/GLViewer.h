#pragma once
#include "ColorManager.h"
#include "Image.h"
#include "TextureUploader.h"

#include <string>
#include <vector>

enum class ChannelMode : int { RGB = 0, Red, Green, Blue, Alpha, Luma };
// Cryptomatte presentation (the mask travels in the image alpha).
enum class MatteMode : int { Off = 0, Ids, Overlay, Masked, Matte };
// What the color of an image with alpha holds (None: opaque, alpha ignored).
enum class AlphaMode : int { None = 0, Straight, Premultiplied };
// How a composite layer combines with the layers below it (scene-linear values).
enum class BlendMode : int { Normal = 0, Add, Subtract, Multiply, Screen, Count };
// Viewer-only analysis of the pixels (never applied to captures or exports).
enum class CheckMode : int { Off = 0, BadPixels, FalseColor, Zebra };
// How the compared sequence B is shown with A.
enum class CompareMode : int { Wipe = 0, SideBySide, Difference, Toggle, Count };

// One layer of a composite, listed bottom to top.
struct CompLayer {
    ImagePtr image;              // null or invalid = skipped
    int transform = -1;          // index into GLViewer::setInputTransforms(); -1 = skipped
    BlendMode blend = BlendMode::Normal;
    float opacity = 1.0f;
    float gain = 1.0f;           // linear multiplier (layer exposure)
    bool straight = false;       // color not premultiplied by alpha
};

// Draws the current frame through an OCIO-generated GLSL shader.
class GLViewer {
public:
    // dc / context: the main GL context, shared with the texture uploader.
    bool init(std::string& err, void* dc, void* context);
    void shutdown();

    // Textures are kept per image: showing an image again, or one uploaded ahead with
    // prefetch(), costs no upload.
    void setImage(const ImagePtr& img);          // single image
    // Composite instead of a single image: each layer goes through its input transform
    // (to the working space) and is blended over the ones below on a width x height
    // canvas, drawn at fullWidth x fullHeight (the file size; larger with a proxy). The
    // display transform then runs once on the result. Recomposited only when a layer or
    // a parameter changes.
    void setComposite(const std::vector<CompLayer>& layers, int width, int height, int fullWidth, int fullHeight);
    // Images of the next frame: uploaded ahead on the uploader thread while this one shows.
    void prefetch(const std::vector<ImagePtr>& images);

    // Display transform (image or working space -> display); null = passthrough.
    bool setProcessor(const OCIO::ConstGPUProcessorRcPtr& gpu, std::string& err);
    // Input transforms of the composite layers; null = passthrough.
    bool setInputTransforms(const std::vector<OCIO::ConstGPUProcessorRcPtr>& procs, std::string& err);
    void setExposure(float stops);
    void setGamma(float gamma);
    void setMatteMode(MatteMode m) { m_matte = m; }
    // The single image is shown over black and exported with alpha as straight color.
    void setAlphaMode(AlphaMode m) { m_alpha = m; }
    // Renders with alpha write premultiplied instead of straight color.
    void setPremultipliedOutput(bool on) { m_premultOut = on; }
    void setCheck(CheckMode m) { m_check = m; }

    // A/B compare with the single image: b (null = off) goes through its own display
    // transform. wipe: split position across the image (0..1); swap: B on the left / shown.
    void setCompare(const ImagePtr& b, CompareMode mode, float wipe, bool swap, float diffGain, AlphaMode alphaB);
    bool setProcessorB(const OCIO::ConstGPUProcessorRcPtr& gpu, std::string& err);   // null = passthrough
    void releaseCompare();                        // frees the B programs

    // The viewer source at most maxW pixels wide through the display transform, as RGBA8,
    // top row first (for scopes).
    bool renderPreview(int maxW, std::vector<uint8_t>& rgba, int& w, int& h);

    // viewport in framebuffer pixels (origin bottom-left). zoom = screen px per image px.
    void draw(int fbW, int fbH, int vx, int vy, int vw, int vh,
              float zoom, float panX, float panY, ChannelMode channel);

    // Renders the image 1:1 through the current color pipeline into an offscreen
    // buffer and reads it back as packed RGB (8 or 16 bit per channel), top row first.
    // withAlpha: RGBA readback (alpha = image alpha / Cryptomatte mask).
    bool renderToMemory(const ImagePtr& img, ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out);
    // Same for the composite set with setComposite().
    bool renderCompositeToMemory(ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out);

private:
    struct LutTex { unsigned id = 0; unsigned target = 0; std::string sampler; };
    struct Uniform { int loc = -1; OCIO::GpuShaderDesc::UniformData data; };
    // A shader program and the resources of its OCIO color function.
    struct Program {
        unsigned id = 0;
        std::vector<LutTex> luts;
        std::vector<Uniform> uniforms;
        OCIO::GpuShaderDescRcPtr desc;   // owns the dynamic properties the uniforms read
        int locRect = -1, locImage = -1, locImageB = -1;
        int locChannel = -1, locMatte = -1, locOutAlpha = -1, locAlphaMode = -1, locCheck = -1;   // display
        int locGain = -1, locOpacity = -1, locMode = -1, locStraight = -1; // composite layer
    };
    struct Tex {
        ImagePtr image;
        unsigned id = 0;
        int w = 0, h = 0;                        // storage, as of the last upload issued
        PixelType type = PixelType::U8;
        TextureUploader::JobPtr job;             // upload not waited on yet
    };

    bool buildOcioProgram(Program& p, const OCIO::ConstGPUProcessorRcPtr& gpu, const char* function,
                          const char* passthrough, const char* main, std::string& err);
    static void releaseProgram(Program& p);
    bool buildDisplay(Program& p, const OCIO::ConstGPUProcessorRcPtr& gpu, std::string& err,
                      OCIO::DynamicPropertyDoubleRcPtr& exposure, OCIO::DynamicPropertyDoubleRcPtr& gamma);
    void bindOcio(const Program& p);     // LUT textures (units 1..) and uniforms
    // matte / alphaMode: the uMatte / uAlphaMode of kFragmentMain; check: CheckMode.
    void drawDisplay(const Program& p, float x0, float y0, float x1, float y1, bool nearest, ChannelMode channel, bool outAlpha,
                     unsigned tex, int matte, int alphaMode, int check);
    int sourceMatte() const;             // the single image's or the composite's
    int sourceAlpha() const;
    void drawCompare(int fbW, int fbH, int vx, int vy, int vw, int vh, float zoom, float panX, float panY, ChannelMode channel,
                     unsigned texA, int texW, int fullW, int fullH);
    void compose();                      // renders the composite when it is out of date
    bool renderSource(bool composite, ChannelMode channel, bool sixteenBit, bool withAlpha, std::vector<uint8_t>& out);
    static unsigned createTexture();
    unsigned texture(const ImagePtr& img);   // ready to sample, uploading it now if needed
    Tex& slotFor(const ImagePtr& img);       // a texture no current or next image uses
    bool needed(const Tex& t) const;
    bool finishJob(Tex& t);                  // false: the upload never happened
    void trimPool();

    unsigned m_vao = 0, m_vbo = 0;
    Program m_display;
    unsigned m_fbo = 0, m_fboTex = 0;
    int m_fboW = 0, m_fboH = 0;
    bool m_fbo16 = false;
    ImagePtr m_image;
    std::vector<Tex> m_pool;
    std::vector<ImagePtr> m_prefetch;
    TextureUploader m_uploader;
    void* m_dc = nullptr;
    void* m_context = nullptr;

    MatteMode m_matte = MatteMode::Off;
    AlphaMode m_alpha = AlphaMode::None;
    bool m_premultOut = false;
    CheckMode m_check = CheckMode::Off;
    OCIO::DynamicPropertyDoubleRcPtr m_dynExposure, m_dynGamma;
    float m_exposure = 0.0f, m_gamma = 1.0f;

    // A/B compare (built on first use)
    ImagePtr m_imageB;
    CompareMode m_cmpMode = CompareMode::Wipe;
    float m_wipe = 0.5f, m_diffGain = 1.0f;
    bool m_swap = false;
    AlphaMode m_alphaB = AlphaMode::None;
    Program m_displayB, m_diff;
    OCIO::DynamicPropertyDoubleRcPtr m_dynExposureB, m_dynGammaB;

    // Scope preview target (built on first use)
    unsigned m_prevFbo = 0, m_prevTex = 0;
    int m_prevW = 0, m_prevH = 0;

    // Composite
    bool m_useComp = false, m_compDirty = false;
    std::vector<Program> m_inputs;
    std::vector<CompLayer> m_comp;
    unsigned m_compFbo = 0, m_compTex = 0;
    int m_compW = 0, m_compH = 0;          // canvas
    int m_compFullW = 0, m_compFullH = 0;  // size on screen
    int m_compTexW = 0, m_compTexH = 0;    // allocated target
};
