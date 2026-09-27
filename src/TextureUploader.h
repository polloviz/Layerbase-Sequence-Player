#pragma once
#include "Image.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

// Uploads an image into a texture of the current context: glTexImage2D when allocating
// storage, glTexSubImage2D otherwise. fromBuffer: the pixels are in the bound unpack buffer.
void UploadTexture(unsigned tex, const Image& img, bool allocate, bool fromBuffer = false);

// Uploads textures from a worker thread that owns a GL context sharing objects with the
// main one, so large frames reach the GPU without stalling the main thread. A job is done
// once its data is on the GPU; the main context then binds the texture without any sync call.
class TextureUploader {
public:
    struct Job {
        unsigned texture = 0;
        ImagePtr image;
        bool allocate = false;       // (re)define the storage: size or type changed
        bool done = false;           // guarded by the uploader mutex
        bool uploaded = false;       // valid once done: the data is on the GPU
    };
    using JobPtr = std::shared_ptr<Job>;

    ~TextureUploader() { stop(); }

    // Starts the worker; its context is created there, so this returns at once.
    // Call on the main thread with the main context current.
    void start(void* mainDC, void* mainContext);
    void stop();
    bool ready() const;              // the worker context is up
    bool failed() const;             // it could not be created: upload inline

    void submit(const JobPtr& job);
    void wait(const JobPtr& job);    // until the worker has issued the upload

private:
    void run();

    mutable std::mutex m_mutex;
    std::condition_variable m_cv, m_doneCv;
    std::deque<JobPtr> m_queue;
    std::thread m_thread;
    void* m_window = nullptr;        // hidden window whose DC the worker context uses
    void* m_dc = nullptr;
    bool m_quit = false, m_ready = false, m_failed = false;
};
