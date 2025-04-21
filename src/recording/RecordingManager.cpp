#include "recording/RecordingManager.hpp"
#include <SDL2/SDL_image.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace tfv
{
    RecordingManager::RecordingManager(SDL_Renderer* renderer, int width, int height)
        : m_renderer(renderer), m_width(width), m_height(height)
    {
        // Initialize SDL_image for saving PNG files
        int imgFlags = IMG_INIT_PNG;
        if(!(IMG_Init(imgFlags) & imgFlags))
        {
            std::cerr << "SDL_image could not initialize! SDL_image Error: " << IMG_GetError()
                      << std::endl;
        }
    }

    RecordingManager::~RecordingManager()
    {
        // Make sure recording is stopped
        stopRecording();

        // Clean up any remaining frames
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for(auto* surface : m_frameQueue)
        {
            SDL_FreeSurface(surface);
        }
        m_frameQueue.clear();
    }

    bool RecordingManager::captureScreenshot(const std::string& path)
    {
        if(!m_renderer)
        {
            return false;
        }

        // Create an RGB surface to copy the renderer to
        SDL_Surface* screenshot = SDL_CreateRGBSurface(0, m_width, m_height, 32, 0x00FF0000,
                                                       0x0000FF00, 0x000000FF, 0xFF000000);
        if(!screenshot)
        {
            std::cerr << "Failed to create screenshot surface: " << SDL_GetError() << std::endl;
            return false;
        }

        // Read pixels from renderer to surface
        if(SDL_RenderReadPixels(m_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, screenshot->pixels,
                                screenshot->pitch) != 0)
        {
            std::cerr << "Failed to read pixels from renderer: " << SDL_GetError() << std::endl;
            SDL_FreeSurface(screenshot);
            return false;
        }

        // Save the surface to a file
        bool success = saveSurface(screenshot, path);

        // Clean up
        SDL_FreeSurface(screenshot);

        if(success && m_statusCallback)
        {
            m_statusCallback("Screenshot saved to " + path);
        }

        return success;
    }

    bool RecordingManager::startRecording(const std::string& path, int fps)
    {
        if(m_recording)
        {
            return false; // Already recording
        }

        if(!std::filesystem::exists(std::filesystem::path(path).parent_path()))
        {
            if(m_statusCallback)
            {
                m_statusCallback("Output directory does not exist");
            }
            return false;
        }

        // Create output directory for frames
        m_outputPath = path;
        m_fps = fps;

        // Create a frames subdirectory to store individual frames
        std::string framesDir = m_outputPath + "_frames";
        std::filesystem::create_directory(framesDir);

        // Clear any existing frames
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for(auto* surface : m_frameQueue)
            {
                SDL_FreeSurface(surface);
            }
            m_frameQueue.clear();
        }

        // Start the processing thread
        m_threadRunning = true;
        m_recording = true;
        m_processThread = std::thread(&RecordingManager::processFrames, this);

        if(m_statusCallback)
        {
            m_statusCallback("Started recording at " + std::to_string(fps) + " FPS");
        }

        return true;
    }

    bool RecordingManager::stopRecording()
    {
        if(!m_recording)
        {
            return false; // Not recording
        }

        // Stop recording
        m_recording = false;

        // Wait for processing thread to finish
        if(m_threadRunning && m_processThread.joinable())
        {
            m_threadRunning = false;
            m_processThread.join();
        }

        if(m_statusCallback)
        {
            m_statusCallback("Recording stopped. Frames saved to " + m_outputPath + "_frames");
            m_statusCallback("To create video, use: ffmpeg -framerate " + std::to_string(m_fps) +
                             " -i " + m_outputPath +
                             "_frames/frame_%08d.png -c:v libx264 -pix_fmt yuv420p " +
                             m_outputPath);
        }

        return true;
    }

    void RecordingManager::captureFrame()
    {
        if(!m_recording || !m_renderer)
        {
            return;
        }

        // Create an RGB surface to copy the renderer to
        SDL_Surface* frame = SDL_CreateRGBSurface(0, m_width, m_height, 32, 0x00FF0000, 0x0000FF00,
                                                  0x000000FF, 0xFF000000);
        if(!frame)
        {
            std::cerr << "Failed to create frame surface: " << SDL_GetError() << std::endl;
            return;
        }

        // Read pixels from renderer to surface
        if(SDL_RenderReadPixels(m_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, frame->pixels,
                                frame->pitch) != 0)
        {
            std::cerr << "Failed to read pixels from renderer: " << SDL_GetError() << std::endl;
            SDL_FreeSurface(frame);
            return;
        }

        // Add the frame to the queue
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_frameQueue.push_back(frame);
        }
    }

    bool RecordingManager::saveSurface(SDL_Surface* surface, const std::string& path)
    {
        if(!surface)
        {
            return false;
        }

        // Save the surface to a PNG file
        if(IMG_SavePNG(surface, path.c_str()) != 0)
        {
            std::cerr << "Failed to save PNG: " << IMG_GetError() << std::endl;
            return false;
        }

        return true;
    }

    void RecordingManager::processFrames()
    {
        std::string framesDir = m_outputPath + "_frames";
        int frameCount = 0;

        while(m_threadRunning)
        {
            std::vector<SDL_Surface*> framesToProcess;

            // Get frames from the queue
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                if(!m_frameQueue.empty())
                {
                    framesToProcess.swap(m_frameQueue);
                }
            }

            // Process frames
            for(auto* frame : framesToProcess)
            {
                // Generate frame filename
                std::ostringstream framePath;
                framePath << framesDir << "/frame_" << std::setw(8) << std::setfill('0')
                          << frameCount++ << ".png";

                // Save the frame
                saveSurface(frame, framePath.str());

                // Clean up
                SDL_FreeSurface(frame);
            }

            // Sleep to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
} // namespace tfv