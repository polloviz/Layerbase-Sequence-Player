// AOV stack: EXR layers of the opened sequence or of other sequences (one pass each),
// converted to the working space, blended on the GPU and viewed once.
#include "App.h"
#include "I18n.h"
#include "ImageIO.h"
#include "Platform.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {

const char* BlendName(BlendMode m)
{
    switch (m) {
    case BlendMode::Add: return tr(S::BlendAdd);
    case BlendMode::Subtract: return tr(S::BlendSubtract);
    case BlendMode::Multiply: return tr(S::BlendMultiply);
    case BlendMode::Screen: return tr(S::BlendScreen);
    default: return tr(S::BlendNormal);
    }
}

const ExrLayer* SelectedExrLayer(const StackLayer& l)
{
    return l.exrLayers && l.exrLayer >= 0 && l.exrLayer < (int)l.exrLayers->size() ? &(*l.exrLayers)[l.exrLayer] : nullptr;
}

// What gets decoded: the file set and the EXR channels. The single view of the opened
// sequence uses the same key, so its decoded frames carry over into the stack and back.
std::string LayerKey(const std::shared_ptr<const Sequence>& seq, const ExrLayer* layer)
{
    std::string k = seq ? "seq|" + ToUtf8(ToLower(seq->directory + L"\\" + seq->displayName())) : "own";
    if (layer) {
        k += "|" + std::to_string(layer->part);
        for (const auto& c : layer->channels) k += "|" + c;
    }
    return k;
}

std::string LayerName(const StackLayer& l, const std::wstring& ownName)
{
    const ExrLayer* e = SelectedExrLayer(l);
    if (!l.seq) return e ? e->label : ToUtf8(ownName);     // AOVs of the opened file: the layer name says it all
    const std::string base = ToUtf8(SequenceBaseName(*l.seq));
    return !e || e->isRootColor ? base : base + " \xC2\xB7 " + e->label;
}

LoadOptionsPtr LayerOptions(const ExrLayer* layer)
{
    auto o = std::make_shared<LoadOptions>();
    if (layer && !layer->isRootColor) {   // root RGBA: default fast path
        o->part = layer->part;
        o->channels = layer->channels;
    }
    return o;
}

}  // namespace

void App::applyLoadPlan()
{
    auto plan = std::make_shared<LoadPlan>();
    // Exports always read full resolution.
    plan->proxy = m_planProxy = m_exporter || m_batchRunning ? 1 : m_proxy;
    if (!stackActive()) {
        // With Cryptomatte the pixels depend on the selection: a fresh key decodes them again.
        const bool crypto = m_loadOpts && m_loadOpts->cryptoActive();
        const ExrLayer* layer = m_layer >= 0 && m_layer < (int)m_exrInfo.layers.size() ? &m_exrInfo.layers[m_layer] : nullptr;
        plan->layers.push_back({ crypto ? "view#" + std::to_string(++m_viewSerial) : LayerKey(nullptr, layer), nullptr, m_loadOpts });
    } else {
        for (const StackLayer& s : m_stack)
            if (std::none_of(plan->layers.begin(), plan->layers.end(), [&](const LayerLoad& l) { return l.key == s.key; }))
                plan->layers.push_back({ s.key, s.files, LayerOptions(SelectedExrLayer(s)) });
    }
    m_cache.setPlan(plan);
}

void App::showFrame(const FrameSetPtr& set)
{
    m_shownSet = set;
    m_shown = baseImage(set);
    m_viewer.setMatteMode(stackActive() ? MatteMode::Off : m_matte);
}

ImagePtr App::baseImage(const FrameSetPtr& set) const
{
    if (!set || set->images.empty()) return nullptr;
    if (!stackActive()) return set->images[0];
    ImagePtr lowest;
    for (const StackLayer& s : m_stack) {
        ImagePtr img = set->find(s.key);
        if (img && img->valid()) return img;
        if (!lowest) lowest = img;
    }
    return lowest ? lowest : set->images[0];
}

std::vector<CompLayer> App::compLayers(const FrameSetPtr& set) const
{
    std::vector<CompLayer> out;
    if (!set) return out;
    for (const StackLayer& s : m_stack) {
        if (!s.visible) continue;
        CompLayer c;
        c.image = set->find(s.key);
        const auto it = std::find(m_stackInputs.begin(), m_stackInputs.end(), s.input);
        c.transform = it == m_stackInputs.end() ? -1 : int(it - m_stackInputs.begin());
        c.blend = s.blend;
        c.opacity = s.opacity;
        c.gain = std::exp2(s.exposure);
        out.push_back(c);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Editing

void App::stackBegin()
{
    if (stackActive() || !m_seq) return;
    // The current view becomes the bottom layer. Cryptomatte does not apply to the stack.
    if (m_matte != MatteMode::Off) setMatteMode(MatteMode::Off);
    m_cryptoPanel = false;
    m_stackPanel = true;
    StackLayer base;
    if (!m_exrInfo.layers.empty()) {
        base.exrLayers = std::make_shared<const std::vector<ExrLayer>>(m_exrInfo.layers);
        base.exrLayer = std::clamp(m_layer, 0, (int)m_exrInfo.layers.size() - 1);
    }
    base.isFloat = IsFloatFormat(m_seqExt);
    base.input = m_color.input;
    base.inputAuto = !m_inputUserChosen;
    base.blend = BlendMode::Normal;
    stackPush(std::move(base));
}

void App::stackPush(StackLayer layer)
{
    layer.id = m_stackNextId++;
    layer.key = LayerKey(layer.seq, SelectedExrLayer(layer));
    layer.name = LayerName(layer, SequenceBaseName(*m_seq));
    m_stack.push_back(std::move(layer));
    m_stackSel = (int)m_stack.size() - 1;
    applyLoadPlan();
    m_colorDirty = true;
}

void App::stackAddOwn(int exrLayer)
{
    if (!m_seq || exrLayer < 0 || exrLayer >= (int)m_exrInfo.layers.size()) return;
    stackBegin();
    const auto own = std::find_if(m_stack.begin(), m_stack.end(), [](const StackLayer& s) { return !s.seq; });
    StackLayer l;
    l.exrLayers = own != m_stack.end() && own->exrLayers ? own->exrLayers
                                                         : std::make_shared<const std::vector<ExrLayer>>(m_exrInfo.layers);
    l.exrLayer = exrLayer;
    l.isFloat = IsFloatFormat(m_seqExt);
    if (own != m_stack.end()) {   // AOVs of one file share its color space
        l.input = own->input;
        l.inputAuto = own->inputAuto;
    } else {
        l.input = autoInput(l.isFloat, m_seq->frames[0].path);
    }
    l.blend = BlendMode::Add;     // AOVs rebuild the beauty by adding up
    stackPush(std::move(l));
}

void App::stackAddOther(const StackLayer& source, int exrLayer)
{
    StackLayer l;
    l.seq = source.seq;
    l.files = source.files;
    l.missing = source.missing;
    l.exrLayers = source.exrLayers;
    l.exrLayer = exrLayer;
    l.isFloat = source.isFloat;
    l.input = source.input;
    l.inputAuto = source.inputAuto;
    l.blend = BlendMode::Add;
    stackPush(std::move(l));
}

void App::stackAddSequences(const std::vector<std::wstring>& paths)
{
    if (!m_seq) return;
    int missing = 0;
    for (const std::wstring& path : paths) {
        int start = 0;
        Sequence seq = DetectSequence(path, &start);
        if (seq.empty() || !IsSupportedExtension(GetFileExtension(seq.frames[0].path))) {
            showToast(std::string(tr(S::NotASequence)) + " " + ToUtf8(GetFileName(path)), true);
            continue;
        }
        stackBegin();
        StackLayer l;
        l.seq = std::make_shared<const Sequence>(std::move(seq));
        auto files = std::make_shared<const std::vector<std::wstring>>(MatchFrames(*m_seq, *l.seq));
        l.missing = (int)std::count(files->begin(), files->end(), std::wstring());
        l.files = files;
        const std::wstring ext = GetFileExtension(l.seq->frames[0].path);
        l.isFloat = IsFloatFormat(ext);
        if (ext == L".exr") {
            ExrInfo info = ReadExrInfo(l.seq->frames[start].path);
            if (!info.layers.empty()) {
                l.exrLayer = info.defaultLayer;
                l.exrLayers = std::make_shared<const std::vector<ExrLayer>>(std::move(info.layers));
            }
        }
        l.input = autoInput(l.isFloat, l.seq->frames[0].path);
        l.blend = BlendMode::Add;
        missing += l.missing;
        stackPush(std::move(l));
    }
    if (missing > 0) showToast(std::to_string(missing) + " " + tr(S::FramesMissing), true);
}

void App::stackAddDialog()
{
    const std::vector<std::wstring> paths = ShowOpenImagesDialog(m_hwnd, FromUtf8(tr(S::AddPassSequences)).c_str());
    if (!paths.empty()) stackAddSequences(paths);
}

void App::stackRemove(int index)
{
    if (index < 0 || index >= (int)m_stack.size()) return;
    m_stack.erase(m_stack.begin() + index);
    m_stackSel = std::clamp(m_stackSel >= index ? m_stackSel - 1 : m_stackSel, 0, std::max(0, (int)m_stack.size() - 1));
    applyLoadPlan();
    m_colorDirty = true;
}

void App::stackMove(int from, int to)
{
    const int n = (int)m_stack.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    StackLayer l = std::move(m_stack[from]);
    m_stack.erase(m_stack.begin() + from);
    m_stack.insert(m_stack.begin() + to, std::move(l));
    m_stackSel = to;
    // Without a scene_linear role the bottom layer's space is the working space.
    if (m_color.workingSpace().empty()) m_colorDirty = true;
}

void App::stackClear()
{
    m_stack.clear();
    m_stackSel = 0;
    applyLoadPlan();
    m_colorDirty = true;
}

void App::stackSetExrLayer(int index, int exrLayer)
{
    StackLayer& l = m_stack[index];
    l.exrLayer = exrLayer;
    l.key = LayerKey(l.seq, SelectedExrLayer(l));
    l.name = LayerName(l, SequenceBaseName(*m_seq));
    applyLoadPlan();
}

void App::stackSetInput(int index, const std::string& name)
{
    StackLayer& l = m_stack[index];
    l.inputAuto = name.empty();
    l.input = name.empty() ? autoInput(l.isFloat, (l.seq ? l.seq : m_seq)->frames[0].path) : name;
    m_colorDirty = true;
}

void App::stackRefreshInputs()
{
    for (StackLayer& l : m_stack)
        if (l.inputAuto || !m_color.hasColorSpace(l.input)) {
            l.inputAuto = true;
            l.input = autoInput(l.isFloat, (l.seq ? l.seq : m_seq)->frames[0].path);
        }
}

// Debug: SP_TEST_STACK=diffuse,specular:add,C:\passes\spec.0001.exr,hidebase builds a stack at
// startup from layer names of the opened file and other sequences (optional :blend suffix).
void App::stackTestSetup()
{
    const wchar_t* env = _wgetenv(L"SP_TEST_STACK");
    if (!env || !m_seq) return;
    const std::wstring all = env;
    static const wchar_t* blends[] = { L"normal", L"add", L"subtract", L"multiply", L"screen" };
    size_t start = 0;
    while (start < all.size()) {
        const size_t comma = all.find(L',', start);
        std::wstring item = all.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        start = comma == std::wstring::npos ? all.size() : comma + 1;
        if (item == L"hidebase") {
            if (stackActive()) m_stack.front().visible = false;
            continue;
        }
        BlendMode blend = BlendMode::Add;
        const size_t colon = item.rfind(L':');
        if (colon != std::wstring::npos && colon > 1)   // not a drive letter
            for (int b = 0; b < (int)BlendMode::Count; ++b)
                if (item.compare(colon + 1, std::wstring::npos, blends[b]) == 0) {
                    blend = (BlendMode)b;
                    item.resize(colon);
                }
        const size_t before = m_stack.size();
        if (item.find_first_of(L"\\/") != std::wstring::npos) {
            stackAddSequences({ item });
        } else {
            for (size_t i = 0; i < m_exrInfo.layers.size(); ++i)
                if (FromUtf8(m_exrInfo.layers[i].label) == item) { stackAddOwn((int)i); break; }
        }
        if (m_stack.size() > before) m_stack.back().blend = blend;
    }
}

// ---------------------------------------------------------------------------
// Panel

void App::drawStackAddMenu()
{
    if (!ImGui::BeginPopup("##stackadd")) return;
    int own = -1, otherLayer = -1;
    const StackLayer* other = nullptr;
    // Headers use the base name: ImGui would cut "shot.####.exr" at the "##".
    if (!m_exrInfo.layers.empty()) {
        ImGui::SeparatorText(ToUtf8(SequenceBaseName(*m_seq)).c_str());
        for (int i = 0; i < (int)m_exrInfo.layers.size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::MenuItem(m_exrInfo.layers[i].label.c_str())) own = i;
            ImGui::PopID();
        }
    }
    // More layers of the multi-layer sequences already in the stack.
    std::vector<const StackLayer*> sources;
    for (const StackLayer& l : m_stack)
        if (l.seq && l.exrLayers && l.exrLayers->size() > 1 &&
            std::none_of(sources.begin(), sources.end(), [&](const StackLayer* s) { return s->seq == l.seq; }))
            sources.push_back(&l);
    for (const StackLayer* src : sources) {
        ImGui::PushID(src->id);
        ImGui::SeparatorText(ToUtf8(SequenceBaseName(*src->seq)).c_str());
        for (int i = 0; i < (int)src->exrLayers->size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::MenuItem((*src->exrLayers)[i].label.c_str())) { other = src; otherLayer = i; }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr(S::AddPassSequences))) defer([this] { stackAddDialog(); });
    ImGui::EndPopup();

    if (own >= 0) stackAddOwn(own);
    if (other) {
        const StackLayer source = *other;   // the stack grows below
        stackAddOther(source, otherLayer);
    }
}

void App::drawStackPanel(float x, float y, float w, float h)
{
    const float s = m_dpiScale;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::Begin("##stack", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    ImGui::TextUnformatted(tr(S::StackTitle));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x - ImGui::GetFrameHeight() * 0.6f);
    if (ImGui::SmallButton("x")) m_stackPanel = false;
    ImGui::Spacing();

    if (!stackActive()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", tr(S::StackIntro));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (ImGui::Button(tr(S::AddLayer), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("##stackadd");
    drawStackAddMenu();
    if (!stackActive()) {
        ImGui::End();
        return;
    }

    // Layers, top of the stack first: a click selects, a drag moves.
    ImGui::Spacing();
    const int n = (int)m_stack.size();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float rowH = ImGui::GetFrameHeight();
    const float listH = std::min((rowH + st.ItemSpacing.y) * n - st.ItemSpacing.y + st.WindowPadding.y * 2, h * 0.42f);
    ImGui::BeginChild("##layers", ImVec2(0, listH), ImGuiChildFlags_Borders);
    int moveFrom = -1, moveTo = -1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = n - 1; i >= 0; --i) {
        StackLayer& l = m_stack[i];
        ImGui::PushID(l.id);
        // The row is one selectable; the visibility checkbox and the texts sit on top of it.
        const ImVec2 rowPos = ImGui::GetCursorPos();
        if (ImGui::Selectable("##row", i == m_stackSel, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH))) m_stackSel = i;
        const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
        const ImagePtr img = m_shownSet ? m_shownSet->find(l.key) : nullptr;
        const bool bad = img && !img->valid();
        if (bad) ImGui::SetItemTooltip("%s", img->error.c_str());
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("STACK_LAYER", &i, sizeof(int));
            ImGui::TextUnformatted(l.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STACK_LAYER")) {
                moveFrom = *static_cast<const int*>(p->Data);
                moveTo = i;
            }
            ImGui::EndDragDropTarget();
        }

        const char* blend = BlendName(l.blend);
        const ImVec2 bs = ImGui::CalcTextSize(blend);
        const float textY = (r0.y + r1.y - bs.y) * 0.5f;
        const float nameX = r0.x + rowH + st.ItemInnerSpacing.x;
        const float blendX = r1.x - bs.x - st.FramePadding.x;
        const ImU32 nameCol = ImGui::GetColorU32(bad ? ImVec4(1.0f, 0.45f, 0.45f, 1) : st.Colors[l.visible ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
        const ImVec4 clip(nameX, r0.y, blendX - st.ItemSpacing.x, r1.y);
        dl->AddText(nullptr, 0.0f, ImVec2(nameX, textY), nameCol, l.name.c_str(), nullptr, 0.0f, &clip);
        dl->AddText(ImVec2(blendX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), blend);

        ImGui::SetCursorPos(rowPos);
        ImGui::Checkbox("##visible", &l.visible);
        ImGui::SetItemTooltip("%s", tr(S::Visible));
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (moveFrom >= 0) stackMove(moveFrom, moveTo);

    // Selected layer
    m_stackSel = std::clamp(m_stackSel, 0, (int)m_stack.size() - 1);
    const int sel = m_stackSel;
    StackLayer& l = m_stack[sel];
    ImGui::Spacing();
    ImGui::SeparatorText(l.name.c_str());
    const float labelW = 82 * s;
    auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    if (l.seq) {
        row(tr(S::Source));
        ImGui::TextUnformatted(ToUtf8(l.seq->displayName()).c_str());
        ImGui::SetItemTooltip("%s", ToUtf8(l.seq->directory).c_str());
        if (l.missing > 0) {
            ImGui::SetCursorPosX(labelW);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1), "%d %s", l.missing, tr(S::FramesMissing));
        }
    }
    if (const ExrLayer* e = SelectedExrLayer(l); e && l.exrLayers->size() > 1) {
        row(tr(S::Layer));
        int pick = -1;
        if (ImGui::BeginCombo("##exrlayer", e->label.c_str(), ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < (int)l.exrLayers->size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable((*l.exrLayers)[i].label.c_str(), i == l.exrLayer) && i != l.exrLayer) pick = i;
                if (i == l.exrLayer && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (pick >= 0) stackSetExrLayer(sel, pick);
    }
    row(tr(S::Input));
    std::string picked;
    if (drawColorSpaceCombo("##layerinput", -FLT_MIN, l.input, l.inputAuto, picked)) stackSetInput(sel, picked);

    row(tr(S::Blend));
    if (ImGui::BeginCombo("##blend", BlendName(l.blend))) {
        for (int b = 0; b < (int)BlendMode::Count; ++b) {
            if (ImGui::Selectable(BlendName((BlendMode)b), (int)l.blend == b)) l.blend = (BlendMode)b;
            if ((BlendMode)b == BlendMode::Screen) ImGui::SetItemTooltip("%s", tr(S::ScreenHint));
        }
        ImGui::EndCombo();
    }
    if (l.blend == BlendMode::Screen) ImGui::SetItemTooltip("%s", tr(S::ScreenHint));

    row(tr(S::Opacity));
    float pct = l.opacity * 100.0f;
    if (ImGui::SliderFloat("##opacity", &pct, 0.0f, 100.0f, "%.0f%%")) l.opacity = pct / 100.0f;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) l.opacity = 1.0f;

    row(tr(S::Exposure));
    ImGui::DragFloat("##layerexposure", &l.exposure, 0.02f, -16.0f, 16.0f, "EV %+.2f");
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) l.exposure = 0.0f;

    ImGui::Spacing();
    const float bw = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x * 2) / 3.0f;
    ImGui::BeginDisabled(sel >= n - 1);
    const bool up = ImGui::Button(tr(S::MoveUp), ImVec2(bw, 0));
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sel <= 0);
    const bool down = ImGui::Button(tr(S::MoveDown), ImVec2(bw, 0));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool remove = ImGui::Button(tr(S::RemoveLayer), ImVec2(bw, 0));
    if (up) stackMove(sel, sel + 1);
    else if (down) stackMove(sel, sel - 1);
    else if (remove) stackRemove(sel);

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(S::StackHint));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (stackActive() && ImGui::Button(tr(S::CloseStack), ImVec2(-FLT_MIN, 0))) stackClear();
    ImGui::End();
}
