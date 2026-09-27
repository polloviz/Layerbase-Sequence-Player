#include <GL/glew.h>
#include "TextureUploader.h"
#include "Platform.h"

#include <windows.h>
#include <cstring>

void UploadTexture(unsigned tex, const Image& img, bool allocate, bool fromBuffer)
{
    GLint internal; GLenum type;
    switch (img.type) {
    case PixelType::U8:  internal = GL_RGBA8;   type = GL_UNSIGNED_BYTE;  break;
    case PixelType::U16: internal = GL_RGBA16;  type = GL_UNSIGNED_SHORT; break;
    default:             internal = GL_RGBA16F; type = GL_HALF_FLOAT;     break;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    const void* pixels = fromBuffer ? nullptr : img.data.data();   // offset 0 of the bound unpack buffer
    if (allocate) glTexImage2D(GL_TEXTURE_2D, 0, internal, img.width, img.height, 0, GL_RGBA, type, pixels);
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_RGBA, type, pixels);
}

void TextureUploader::start(void* mainDC, void* mainContext)
{
    if (m_thread.joinable() || m_failed) return;
    // The worker context needs a DC with the main pixel format. The hidden window belongs
    // to the main thread, which pumps its messages.
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = L"SequencePlayerUploader";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    HDC dc = hwnd ? GetDC(hwnd) : nullptr;
    const int pf = GetPixelFormat((HDC)mainDC);
    PIXELFORMATDESCRIPTOR pfd{ sizeof(pfd), 1 };
    // Created here: sharing with the main context fails while it is current on another thread.
    using CreateCtxFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
    const auto create = (CreateCtxFn)wglGetProcAddress("wglCreateContextAttribsARB");
    const int attribs[] = {
        0x2091 /*WGL_CONTEXT_MAJOR_VERSION_ARB*/, 4,
        0x2092 /*WGL_CONTEXT_MINOR_VERSION_ARB*/, 1,
        0x9126 /*WGL_CONTEXT_PROFILE_MASK_ARB*/, 0x1 /*CORE*/,
        0,
    };
    HGLRC ctx = nullptr;
    if (dc && pf && create && DescribePixelFormat((HDC)mainDC, pf, sizeof(pfd), &pfd) && SetPixelFormat(dc, pf, &pfd))
        ctx = create(dc, (HGLRC)mainContext, attribs);
    if (!ctx) {
        if (hwnd) DestroyWindow(hwnd);
        m_failed = true;
        Log("texture uploader unavailable");
        return;
    }
    m_window = hwnd;
    m_dc = dc;
    m_thread = std::thread([this, ctx] {
        SetThreadDescription(GetCurrentThread(), L"texture uploader");
        const bool ok = wglMakeCurrent((HDC)m_dc, ctx) != FALSE;
        {
            std::lock_guard lock(m_mutex);
            m_ready = ok;
            m_failed = !ok;
        }
        Log("texture uploader %s", ok ? "ready" : "failed");
        if (ok) run();
        {
            // Nothing will upload the rest: release whoever waits.
            std::lock_guard lock(m_mutex);
            for (auto& job : m_queue) job->done = true;
            m_queue.clear();
        }
        m_doneCv.notify_all();
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(ctx);
    });
}

void TextureUploader::run()
{
    GLuint pbo = 0;
    size_t pboSize = 0;
    for (;;) {
        JobPtr job;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return m_quit || !m_queue.empty(); });
            if (m_quit) break;
            job = m_queue.front();
            m_queue.pop_front();
        }
        // Copy into a mapped pixel buffer ourselves: the driver holds a process-wide lock
        // while it copies from client memory, which would stall the main thread's drawing.
        // From the buffer the texture is filled by DMA.
        const Image& img = *job->image;
        const size_t bytes = img.data.size();
        if (!pbo) glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        if (bytes > pboSize) {
            glBufferData(GL_PIXEL_UNPACK_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
            pboSize = bytes;
        }
        void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, bytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
        bool mapped = false;
        if (dst) {
            std::memcpy(dst, img.data.data(), bytes);
            mapped = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) == GL_TRUE;
        }
        if (mapped) UploadTexture(job->texture, img, job->allocate, true);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        if (!mapped) UploadTexture(job->texture, img, job->allocate, false);
        glBindTexture(GL_TEXTURE_2D, 0);
        // Wait here until the GPU has the data: the main context then only has to bind the
        // texture again (GL shared-object rules). Any sync call on the main thread would
        // stall it while the driver catches up.
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        const GLenum r = fence ? glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 2000000000ull) : GL_WAIT_FAILED;
        const bool uploaded = r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED;
        if (fence) glDeleteSync(fence);
        {
            std::lock_guard lock(m_mutex);
            job->uploaded = uploaded;
            job->done = true;
        }
        m_doneCv.notify_all();
    }
    if (pbo) glDeleteBuffers(1, &pbo);
}

void TextureUploader::stop()
{
    if (m_thread.joinable()) {
        {
            std::lock_guard lock(m_mutex);
            m_quit = true;
        }
        m_cv.notify_all();
        m_thread.join();
    }
    if (m_window) DestroyWindow((HWND)m_window);
    m_window = m_dc = nullptr;
}

bool TextureUploader::ready() const
{
    std::lock_guard lock(m_mutex);
    return m_ready && !m_quit;
}

bool TextureUploader::failed() const
{
    std::lock_guard lock(m_mutex);
    return m_failed;
}

void TextureUploader::submit(const JobPtr& job)
{
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(job);
    }
    m_cv.notify_one();
}

void TextureUploader::wait(const JobPtr& job)
{
    std::unique_lock lock(m_mutex);
    m_doneCv.wait(lock, [&] { return job->done; });
}
