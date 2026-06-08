#include "core/Configuration.hpp"
#include "utils/LoggingManager.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace tfv
{
    Configuration& Configuration::instance()
    {
        static Configuration instance;
        return instance;
    }

    bool Configuration::initialize(const std::optional<std::filesystem::path>& configFile,
                                  int argc, char* argv[])
    {
        try {
            // Get executable path
#ifdef _WIN32
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            m_executablePath = std::filesystem::path(path).parent_path();
#elif __APPLE__
            char path[1024];
            uint32_t size = sizeof(path);
            if (_NSGetExecutablePath(path, &size) == 0) {
                m_executablePath = std::filesystem::path(path).parent_path();
            }
#elif __linux__
            char path[PATH_MAX];
            ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
            if (count != -1) {
                path[count] = '\0';
                m_executablePath = std::filesystem::path(path).parent_path();
            }
#endif
            
            m_workingDirectory = std::filesystem::current_path();
            
            // Set default values first
            setDefaults();
            
            // Load from environment variables
            loadFromEnvironment();
            
            // Load from config file if provided
            if (configFile.has_value()) {
                loadFromFile(configFile.value());
            } else {
                // Try to load default config files
                std::vector<std::filesystem::path> defaultConfigs = {
                    m_workingDirectory / "trafficflowviz.conf",
                    m_executablePath / "trafficflowviz.conf",
                    std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".trafficflowviz.conf"
                };
                
                for (const auto& configPath : defaultConfigs) {
                    if (std::filesystem::exists(configPath)) {
                        loadFromFile(configPath);
                        break;
                    }
                }
            }
            
            // Parse command line arguments (highest priority)
            if (argc > 0 && argv != nullptr) {
                parseCommandLine(argc, argv);
            }
            
            LOG_INFO("Configuration initialized successfully");
            LOG_INFO("Executable path: {path}", PARAM(path, m_executablePath.string()));
            LOG_INFO("Working directory: {path}", PARAM(path, m_workingDirectory.string()));
            
            return true;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to initialize configuration: {error}", PARAM(error, e.what()));
            return false;
        }
    }

    void Configuration::setDefaults()
    {
        // Application settings
        m_values["app.title"] = "Traffic Flow Visualization";
        m_values["app.window.width"] = "1280";
        m_values["app.window.height"] = "720";
        m_values["app.renderer"] = "SDL";
        m_values["app.fullscreen"] = "false";
        m_values["app.vsync"] = "true";
        
        // Performance settings
        m_values["perf.max_fps"] = "60";
        m_values["perf.max_vehicles"] = "100000";
        m_values["perf.simulation_timestep"] = "0.016";
        m_values["perf.worker_threads"] = std::to_string(std::thread::hardware_concurrency());

        // Simulation / reproducibility
        m_values["sim.master_seed"] = "12345";

        // Procedural world generation (default ON). Set sim.procedural=0 to load the CSVs
        // (data.city_file + data.vehicles_file) instead. Fully deterministic for the seed above.
        m_values["sim.procedural"] = "1";
        m_values["sim.proc.rows"] = "6";        // grid rows (intersections)
        m_values["sim.proc.cols"] = "8";        // grid cols
        m_values["sim.proc.spacing_m"] = "160"; // block size (px == m here)
        m_values["sim.proc.jitter_m"] = "40";   // per-node position jitter
        m_values["sim.proc.keep_prob"] = "0.8"; // chance a redundant grid edge survives
        m_values["sim.proc.two_way"] = "1";     // expand each street into a two-way road
        m_values["sim.proc.lanes"] = "1";       // lanes per direction
        m_values["sim.proc.vehicles"] = "60";   // number of vehicles to spawn

        // Agent decision layer (Phase 2)
        m_values["sim.default_brain"] = "rule";
        m_values["perf.decision_hz"] = "10.0";
        m_values["sim.idm.v0_cap"] = "13.9";
        m_values["sim.idm.a_max"] = "1.5";
        m_values["sim.idm.b_comfort"] = "2.0";
        m_values["sim.idm.b_max"] = "6.0";
        m_values["sim.idm.s0"] = "2.0";
        m_values["sim.idm.T"] = "1.5";
        m_values["sim.idm.delta"] = "4.0";

        // File paths (relative to executable or working directory)
        m_values["paths.data_dir"] = "data";
        m_values["paths.assets_dir"] = "assets";
        m_values["paths.output_dir"] = "output";
        m_values["paths.logs_dir"] = "logs";
        m_values["paths.recordings_dir"] = "recordings";
        m_values["paths.shaders_dir"] = "shaders";
        
        // Data files
        m_values["data.vehicles_file"] = "vehicles/vehicles.csv";
        m_values["data.city_file"] = "roads/roads_complex.csv";
        m_values["data.signs_file"] = "roads/signs.csv";
        m_values["data.icon_file"] = "icon.png";
        
        // Networking
        m_values["network.websocket_url"] = "ws://localhost:8080";
        m_values["network.websocket_port"] = "8080";
        m_values["network.kafka_brokers"] = "localhost:9092";
        m_values["network.max_message_rate"] = "10000";
        
        // Feature flags
        m_values["features.heatmap_enabled"] = "false";
        m_values["features.alerts_enabled"] = "false";
        m_values["features.recording_enabled"] = "false";
        m_values["features.metrics_enabled"] = "true";
        m_values["features.debug_mode"] = "false";
    }

    void Configuration::loadFromEnvironment()
    {
        // Load TFV_* environment variables
        const char* envVars[] = {
            "TFV_DATA_DIR",
            "TFV_ASSETS_DIR",
            "TFV_OUTPUT_DIR",
            "TFV_RENDERER",
            "TFV_WINDOW_WIDTH",
            "TFV_WINDOW_HEIGHT",
            "TFV_DEBUG_MODE",
            "TFV_MAX_VEHICLES",
            "TFV_WEBSOCKET_URL"
        };
        
        for (const char* envVar : envVars) {
            const char* value = std::getenv(envVar);
            if (value) {
                std::string key = envVar;
                // Convert TFV_DATA_DIR to paths.data_dir
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                key = key.substr(4); // Remove "tfv_"
                std::replace(key.begin(), key.end(), '_', '.');
                
                if (key == "data.dir") key = "paths.data_dir";
                else if (key == "assets.dir") key = "paths.assets_dir";
                else if (key == "output.dir") key = "paths.output_dir";
                else if (key == "window.width") key = "app.window.width";
                else if (key == "window.height") key = "app.window.height";
                else if (key == "debug.mode") key = "features.debug_mode";
                else if (key == "max.vehicles") key = "perf.max_vehicles";
                else if (key == "websocket.url") key = "network.websocket_url";
                else if (key == "renderer") key = "app.renderer";
                
                m_values[key] = value;
                LOG_DEBUG("Loaded environment variable {key}={value}", PARAM(key, key), PARAM(value, value));
            }
        }
    }

    void Configuration::parseCommandLine(int argc, char* argv[])
    {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            
            if (arg.starts_with("--")) {
                // Handle --key=value format
                size_t eq = arg.find('=');
                if (eq != std::string::npos) {
                    std::string key = arg.substr(2, eq - 2);
                    std::string value = arg.substr(eq + 1);
                    
                    // Convert command line format to internal format
                    std::replace(key.begin(), key.end(), '-', '.');
                    m_values[key] = value;
                    LOG_DEBUG("Command line argument: {key}={value}", PARAM(key, key), PARAM(value, value));
                }
                // Handle --key value format
                else if (i + 1 < argc) {
                    std::string key = arg.substr(2);
                    std::string value = argv[++i];
                    std::replace(key.begin(), key.end(), '-', '.');
                    m_values[key] = value;
                    LOG_DEBUG("Command line argument: {key}={value}", PARAM(key, key), PARAM(value, value));
                }
            }
        }
    }

    bool Configuration::loadFromFile(const std::filesystem::path& path)
    {
        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                LOG_ERROR("Could not open config file: {path}", PARAM(path, path.string()));
                return false;
            }
            
            std::string line;
            while (std::getline(file, line)) {
                // Skip comments and empty lines
                if (line.empty() || line[0] == '#' || line[0] == ';') {
                    continue;
                }
                
                size_t eq = line.find('=');
                if (eq != std::string::npos) {
                    std::string key = line.substr(0, eq);
                    std::string value = line.substr(eq + 1);
                    
                    // Trim whitespace
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    
                    m_values[key] = value;
                }
            }
            
            LOG_INFO("Loaded configuration from: {path}", PARAM(path, path.string()));
            return true;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to load config file {path}: {error}", 
                     PARAM(path, path.string()), PARAM(error, e.what()));
            return false;
        }
    }

    bool Configuration::saveToFile(const std::filesystem::path& path) const
    {
        try {
            std::ofstream file(path);
            if (!file.is_open()) {
                LOG_ERROR("Could not create config file: {path}", PARAM(path, path.string()));
                return false;
            }
            
            file << "# TrafficFlowViz Configuration File\n";
            file << "# Generated automatically\n\n";
            
            for (const auto& [key, value] : m_values) {
                file << key << "=" << value << "\n";
            }
            
            LOG_INFO("Saved configuration to: {path}", PARAM(path, path.string()));
            return true;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to save config file {path}: {error}", 
                     PARAM(path, path.string()), PARAM(error, e.what()));
            return false;
        }
    }

    std::filesystem::path Configuration::resolveRelativePath(const std::string& relativePath) const
    {
        std::filesystem::path path(relativePath);
        if (path.is_absolute()) {
            return path;
        }
        
        // Try multiple search paths in order
        std::vector<std::filesystem::path> searchPaths = {
            m_workingDirectory,                    // Current working directory
            m_executablePath,                      // Executable directory
            m_executablePath.parent_path(),        // Parent of executable (build -> project root)
            m_executablePath.parent_path().parent_path(),  // build/bin -> project root
        };
        
        for (const auto& basePath : searchPaths) {
            auto fullPath = basePath / path;
            if (std::filesystem::exists(fullPath)) {
                LOG_DEBUG("Found data file at: {path}", PARAM(path, fullPath.string()));
                return fullPath;
            }
        }
        
        // If not found anywhere, return the first candidate for error reporting
        auto fallbackPath = m_workingDirectory / path;
        LOG_DEBUG("Data file not found, using fallback: {path}", PARAM(path, fallbackPath.string()));
        return fallbackPath;
    }

    // Path accessors
    std::filesystem::path Configuration::getDataDirectory() const
    {
        return resolveRelativePath(getValue("paths.data_dir", "data"));
    }

    std::filesystem::path Configuration::getVehicleDataPath() const
    {
        return getDataDirectory() / getValue("data.vehicles_file", "vehicles/vehicles.csv");
    }

    std::filesystem::path Configuration::getCityDataPath() const
    {
        return getDataDirectory() / getValue("data.city_file", "roads/roads_complex.csv");
    }

    std::filesystem::path Configuration::getAssetsDirectory() const
    {
        return resolveRelativePath(getValue("paths.assets_dir", "assets"));
    }

    std::filesystem::path Configuration::getIconPath() const
    {
        return getAssetsDirectory() / getValue("data.icon_file", "icon.png");
    }

    std::filesystem::path Configuration::getShaderDirectory() const
    {
        return resolveRelativePath(getValue("paths.shaders_dir", "shaders"));
    }

    std::filesystem::path Configuration::getOutputDirectory() const
    {
        return resolveRelativePath(getValue("paths.output_dir", "output"));
    }

    std::filesystem::path Configuration::getLogsDirectory() const
    {
        return resolveRelativePath(getValue("paths.logs_dir", "logs"));
    }

    std::filesystem::path Configuration::getRecordingsDirectory() const
    {
        return resolveRelativePath(getValue("paths.recordings_dir", "recordings"));
    }

    // Application settings
    std::string Configuration::getApplicationTitle() const
    {
        return getValue("app.title", "Traffic Flow Visualization");
    }

    int Configuration::getWindowWidth() const
    {
        return getIntValue("app.window.width", 1280);
    }

    int Configuration::getWindowHeight() const
    {
        return getIntValue("app.window.height", 720);
    }

    std::string Configuration::getRendererType() const
    {
        return getValue("app.renderer", "SDL");
    }

    bool Configuration::isFullscreen() const
    {
        return getBoolValue("app.fullscreen", false);
    }

    bool Configuration::isVSync() const
    {
        return getBoolValue("app.vsync", true);
    }

    // Performance settings
    int Configuration::getMaxFPS() const
    {
        return getIntValue("perf.max_fps", 60);
    }

    int Configuration::getMaxVehicles() const
    {
        return getIntValue("perf.max_vehicles", 100000);
    }

    float Configuration::getSimulationTimeStep() const
    {
        return getFloatValue("perf.simulation_timestep", 0.016f);
    }

    int Configuration::getWorkerThreads() const
    {
        return getIntValue("perf.worker_threads", std::thread::hardware_concurrency());
    }

    float Configuration::getFloat(const std::string& key, float defaultValue) const
    {
        return getFloatValue(key, defaultValue);
    }

    int Configuration::getInt(const std::string& key, int defaultValue) const
    {
        return getIntValue(key, defaultValue);
    }

    std::string Configuration::getString(const std::string& key,
                                         const std::string& defaultValue) const
    {
        return getValue(key, defaultValue);
    }

    uint64_t Configuration::getMasterSeed() const
    {
        try
        {
            return static_cast<uint64_t>(std::stoull(getValue("sim.master_seed", "12345")));
        }
        catch(const std::exception&)
        {
            return 12345ull;
        }
    }

    // Networking settings
    std::string Configuration::getWebSocketURL() const
    {
        return getValue("network.websocket_url", "ws://localhost:8080");
    }

    int Configuration::getWebSocketPort() const
    {
        return getIntValue("network.websocket_port", 8080);
    }

    std::string Configuration::getKafkaBrokers() const
    {
        return getValue("network.kafka_brokers", "localhost:9092");
    }

    int Configuration::getMaxMessageRate() const
    {
        return getIntValue("network.max_message_rate", 10000);
    }

    // Feature flags
    bool Configuration::isHeatmapEnabled() const
    {
        return getBoolValue("features.heatmap_enabled", false);
    }

    bool Configuration::isAlertsEnabled() const
    {
        return getBoolValue("features.alerts_enabled", false);
    }

    bool Configuration::isRecordingEnabled() const
    {
        return getBoolValue("features.recording_enabled", false);
    }

    bool Configuration::isMetricsEnabled() const
    {
        return getBoolValue("features.metrics_enabled", true);
    }

    bool Configuration::isDebugMode() const
    {
        return getBoolValue("features.debug_mode", false);
    }

    std::map<std::string, std::string> Configuration::snapshot(const std::string& prefix) const
    {
        std::map<std::string, std::string> out; // std::map -> sorted -> deterministic manifest
        for(const auto& [key, value] : m_values)
            if(key.rfind(prefix, 0) == 0)
                out[key] = value;
        return out;
    }

    // Helper methods
    std::string Configuration::getValue(const std::string& key, const std::string& defaultValue) const
    {
        auto it = m_values.find(key);
        return (it != m_values.end()) ? it->second : defaultValue;
    }

    int Configuration::getIntValue(const std::string& key, int defaultValue) const
    {
        try {
            return std::stoi(getValue(key, std::to_string(defaultValue)));
        }
        catch (const std::exception&) {
            return defaultValue;
        }
    }

    bool Configuration::getBoolValue(const std::string& key, bool defaultValue) const
    {
        std::string value = getValue(key, defaultValue ? "true" : "false");
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes" || value == "on";
    }

    float Configuration::getFloatValue(const std::string& key, float defaultValue) const
    {
        try {
            return std::stof(getValue(key, std::to_string(defaultValue)));
        }
        catch (const std::exception&) {
            return defaultValue;
        }
    }

} // namespace tfv