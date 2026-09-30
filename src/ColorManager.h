#pragma once
#include <OpenColorIO/OpenColorIO.h>
#include <string>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

struct BuiltinConfigInfo {
    std::string uri;       // "ocio://studio-config-v4.0.0_aces-v2.0_ocio-v2.4"
    std::string label;     // "ACES 2.0 · Studio v4.0.0"
    std::string uiName;    // long official name
    bool recommended = false;
};

struct ExternalConfigInfo {
    std::string path;      // UTF-8 path to config.ocio
    std::string label;     // "Blender 5.2"
};

// Built-in AgX display rendering (Blender/Filament formulation), usable with any config.
enum class AgxLook : int { None = 0, Base, Punchy, Golden };

// Where a loaded LUT file runs in the pipeline.
enum class LutPosition : int {
    Display = 0,   // on the display output, after the view (creative LUTs for Rec.709 / sRGB)
    Grading,       // in the config's color_timing space (e.g. ACEScct), before the view
};

struct ColorSpaceInfo {
    std::string name;
    std::string family;
    std::string description;
};

// Owns the active OCIO config and the user's input/display/view/look choice.
class ColorManager {
public:
    static const std::vector<BuiltinConfigInfo>& Builtins();
    static std::string DefaultBuiltinUri();          // latest recommended ACES 2.0 studio config
    static bool HasEnvConfig();
    static const std::vector<ExternalConfigInfo>& BlenderConfigs();   // installed Blender versions

    // Virtual views appended to every display: "AgX · built-in" etc.
    static const std::vector<std::string>& AgxViewNames();
    AgxLook agxLook() const;             // from current view name
    bool isBuiltinView(const std::string& v) const;

    // source: "ocio://<builtin>", "$OCIO", or an absolute path to a .ocio/.ocioz file.
    bool load(const std::string& source);

    const std::string& source() const { return m_source; }
    const std::string& error() const { return m_error; }
    std::string sourceLabel() const;                 // short label for UI
    bool isCustomFile() const;
    bool valid() const { return (bool)m_config; }

    const std::vector<ColorSpaceInfo>& colorSpaces() const { return m_colorSpaces; }
    const std::vector<std::string>& displays() const { return m_displays; }
    const std::vector<std::string>& looks() const { return m_looks; }
    std::vector<std::string> views(const std::string& display) const;
    bool hasColorSpace(const std::string& name) const;
    const ColorSpaceInfo* findColorSpace(const std::string& name) const;

    // Suggested input space for a format; honours the config's file rules.
    std::string defaultInput(bool floatFormat, const std::string& utf8Path) const;
    std::string defaultDisplay() const;
    std::string defaultView(const std::string& display) const;

    // Current selection (validated by setters).
    std::string input, display, view, look;   // look empty = config default for the view
    // LUT file applied by the GPU processors (any format OCIO reads: .cube, .3dl, .csp,
    // .spi1d/.spi3d, .clf, .cc…); empty = none.
    std::string lutPath;
    LutPosition lutPosition = LutPosition::Display;
    bool hasGradingSpace() const;                    // color_timing role, for LutPosition::Grading
    // Where the LUT runs: the display output, also when the config has no grading space.
    bool lutOnDisplay() const { return lutPosition == LutPosition::Display || !hasGradingSpace(); }
    // Parses a LUT file; false with error() when OCIO cannot read it.
    bool checkLut(const std::string& path);
    // The LUT alone on the file values (color management off). Null without a LUT.
    OCIO::ConstGPUProcessorRcPtr buildLutProcessor();

    // Builds the GPU processor for current selection. Returns null on error (see error()).
    OCIO::ConstGPUProcessorRcPtr buildGpuProcessor() { return buildGpuProcessor(input); }
    // Same display/view/look from another source space (the working space of a composite).
    OCIO::ConstGPUProcessorRcPtr buildGpuProcessor(const std::string& src);

    // Scene-linear working space (role scene_linear); empty when the config has none.
    std::string workingSpace() const;
    // Plain color space conversion, for the layers of a composite. Null on error.
    OCIO::ConstGPUProcessorRcPtr buildConversion(const std::string& src, const std::string& dst);

private:
    void rebuildLists();
    OCIO::GroupTransformRcPtr buildAgxTransform(AgxLook look, const std::string& src);
    OCIO::FileTransformRcPtr lutTransform() const;   // null without a LUT
    // The LUT framed by conversions to and from the grading space.
    OCIO::GroupTransformRcPtr gradingLutTransform(const std::string& src) const;

    OCIO::ConstConfigRcPtr m_config;
    std::string m_source;
    std::string m_error;
    std::vector<ColorSpaceInfo> m_colorSpaces;
    std::vector<std::string> m_displays;
    std::vector<std::string> m_looks;
};
