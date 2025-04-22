#ifndef TFV_RECORDING_MANAGER_HPP
#define TFV_RECORDING_MANAGER_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rendering/Renderer.hpp"

namespace tfv
{
    // Forward declarations
    struct Surface;

    /**
     * Manages the recording and export of video/images from the traffic visualization
     */
    class RecordingManager
    {
      public:
        RecordingManager(IRenderer* renderer);
        ~RecordingManager();

        /**
         * Capture and save the current screen as a PNG image
         * @param path File path where to save the image
         * @return true if successful, false otherwise
         */
        bool captureScreenshot(const std::string& path);

        /**
         * Start recording frames to create a video
         * @param path Output file path for the video
         * @param fps Frames per second to record
         * @return true if recording started successfully
         */
        bool startRecording(const std::string& path, int fps = 30);

        /**
         * Stop recording and finalize the video file
         * @return true if video was successfully finalized
         */
        bool stopRecording();

        /**
         * Check if currently recording
         */
        bool isRecording() const { return m_recording; }

        /**
         * Capture the current frame for the video
         */
        void captureFrame();

        /**
         * Set a callback for recording status updates
         */
        using StatusCallback = std::function<void(const std::string& message)>;
        void setStatusCallback(StatusCallback callback) { m_statusCallback = callback; }

      private:
        // Save a surface to a PNG file
        bool saveSurface(Surface* surface, const std::string& path);

        // Process thread that saves frames to disk
        void processFrames();

        IRenderer* m_renderer;
        int m_width;
        int m_height;

        // Video recording state
        std::atomic<bool> m_recording{false};
        std::atomic<bool> m_threadRunning{false};
        std::string m_outputPath;
        int m_fps{30};

        // Frame queue for video recording
        std::vector<Surface*> m_frameQueue;
        std::mutex m_queueMutex;
        std::thread m_processThread;

        // Status updates
        StatusCallback m_statusCallback;
    };
} // namespace tfv

#endif