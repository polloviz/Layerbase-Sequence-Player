// The current frame: copy to the clipboard, save as an image, show the file in Explorer.
#include "App.h"
#include "I18n.h"
#include "ImageIO.h"
#include "Platform.h"

#include <windows.h>

#include <cstdio>

AlphaMode AutoAlphaMode(const ImagePtr& img)
{
    if (!img || !img->hasAlpha) return AlphaMode::None;
    // Renderers write premultiplied color unless told otherwise (Cinema 4D "Straight Alpha" is
    // off by default, also for PNG); only TIFF says which kind it holds.
    return img->straightAlpha ? AlphaMode::Straight : AlphaMode::Premultiplied;
}

AlphaMode App::alphaModeFor(const ImagePtr& img) const
{
    if (!img || !img->hasAlpha) return AlphaMode::None;
    return m_alphaOverride >= 0 ? (AlphaMode)m_alphaOverride : AutoAlphaMode(img);
}

bool App::grabFrame(std::vector<uint8_t>& out, int& width, int& height, bool sixteenBit)
{
    if (!m_seq || !frameCount()) return false;
    FrameSetPtr set = m_cache.get(m_index);
    const FrameSetPtr& planned = set ? set : m_shownSet;
    if (!planned || !planned->plan) return false;
    // Not decoded yet, or decoded smaller for playback: decode it now at full resolution.
    if (!set || set->plan->proxy > 1) {
        auto plan = std::make_shared<LoadPlan>(*planned->plan);
        plan->proxy = 1;
        auto full = std::make_shared<FrameSet>();
        full->plan = plan;
        full->images.resize(plan->layers.size());
        DecodeLayers(*m_seq, m_index, *plan, full->images);
        set = full;
    }
    const ImagePtr img = baseImage(set);
    if (!img || !img->valid()) return false;
    width = img->width;
    height = img->height;
    // The viewer shows the playback frame again on the next redraw.
    if (stackActive()) {
        m_viewer.setComposite(compLayers(set), img->width, img->height, img->fullWidth(), img->fullHeight());
        return m_viewer.renderCompositeToMemory(m_channel, sixteenBit, false, out);
    }
    m_viewer.setMatteMode(m_matte);
    m_viewer.setAlphaMode(alphaModeFor(img));
    return m_viewer.renderToMemory(img, m_channel, sixteenBit, false, out);
}

void App::copyFrame()
{
    if (!m_seq || !m_shown || !m_shown->valid()) return;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!grabFrame(px, w, h, false) || !SetClipboardImage(m_hwnd, px.data(), w, h, EncodePng(px.data(), w, h, true))) {
        showToast(tr(S::FrameGrabFailed), true);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s  (%d\xC3\x97%d)", tr(S::FrameCopied), w, h);
    showToast(buf);
}

void App::saveFrameDialog()
{
    if (!m_seq || !m_shown || !m_shown->valid()) return;
    m_playDir = 0;
    std::wstring ext = FromUtf8(m_settings.frameSaveExt);
    if (ext != L".png" && ext != L".jpg" && ext != L".tif") ext = L".png";
    const std::wstring lastDir = FromUtf8(m_settings.frameSaveDir);
    const std::wstring dir = !lastDir.empty() && IsDirectory(lastDir) ? lastDir : m_seq->directory;
    // "shot_v001.1001.exr" -> "shot_v001_1001.png"; never the name of a frame of the sequence.
    std::wstring name = SequenceBaseName(*m_seq) + L"_" + std::to_wstring(m_seq->frames[m_index].number);
    const std::wstring candidate = ToLower(dir + L"\\" + name + ext);
    for (const SequenceFrame& f : m_seq->frames)
        if (ToLower(f.path) == candidate) { name += L"_view"; break; }
    const std::wstring path = ShowSaveFileDialog(m_hwnd, FromUtf8(tr(S::SaveFrame)).c_str(), dir + L"\\" + name + ext,
                                                 L"PNG, JPEG, TIFF 16-bit", L"*.png;*.jpg;*.jpeg;*.tif;*.tiff", ext.c_str());
    if (path.empty()) return;
    const std::wstring outExt = GetFileExtension(path);
    const bool tiff = outExt == L".tif" || outExt == L".tiff";

    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    std::string err;
    if (!grabFrame(px, w, h, tiff)) err = "GPU render";
    else SaveImageFile(path, px.data(), w, h, tiff, err);
    if (!err.empty()) {
        showToast(std::string(tr(S::FrameGrabFailed)) + ": " + err, true);
        return;
    }
    m_settings.frameSaveDir = ToUtf8(GetParentDir(path));
    m_settings.frameSaveExt = tiff ? ".tif" : outExt == L".jpeg" ? ".jpg" : ToUtf8(outExt);
    showToast(std::string(tr(S::FrameSaved)) + ": " + ToUtf8(GetFileName(path)));
}

void App::revealFrame()
{
    if (!m_seq || !frameCount()) return;
    RevealInExplorer(m_seq->frames[m_index].path);
}
