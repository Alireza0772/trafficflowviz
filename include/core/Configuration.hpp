#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <optional>

namespace tfv
{
    /**
     * @brief Configuration management system for TrafficFlowViz
     * 
     * Provides a centralized way to manage application settings,
     * file paths, and runtime parameters. Supports loading from
     * configuration files, environment variables, and command-line
     * arguments with proper precedence handling.
     */
    class Configuration
    {
    public:
        /**
         * @brief Get the singleton instance of Configuration
         */
        static Configuration& instance();

        /**
         * @brief Initialize configuration from various sources
         * @param configFile Optional path to configuration file
         * @param argc Command line argument count
         * @param argv Command line arguments
         * @return true if initialization succeeded
         */
        bool initialize(const std::optional<std::filesystem::path>& configFile = std::nullopt,
                       int argc = 0, char* argv[] = nullptr);

        // Data file paths
        std::filesystem::path getVehicleDataPath() const;
        std::filesystem::path getCityDataPath() const;
        std::filesystem::path getDataDirectory() const;
        
        // Asset paths
        std::filesystem::path getAssetsDirectory() const;
        std::filesystem::path getIconPath() const;
        std::filesystem::path getShaderDirectory() const;
        
        // Output paths
        std::filesystem::path getOutputDirectory() const;
        std::filesystem::path getLogsDirectory() const;
        std::filesystem::path getRecordingsDirectory() const;
        
        // Application settings
        std::string getApplicationTitle() const;
        int getWindowWidth() const;
        int getWindowHeight() const;
        std::string getRendererType() const;
        bool isFullscreen() const;
        bool isVSync() const;
        
        // Performance settings
        int getMaxFPS() const;
        int getMaxVehicles() const;
        float getSimulationTimeStep() const;
        int getWorkerThreads() const;

        // Simulation / reproducibility
        uint64_t getMasterSeed() const;
        
        // Networking settings
        std::string getWebSocketURL() const;
        int getWebSocketPort() const;
        std::string getKafkaBrokers() const;
        int getMaxMessageRate() const;
        
        // Feature flags
        bool isHeatmapEnabled() const;
        bool isAlertsEnabled() const;
        bool isRecordingEnabled() const;
        bool isMetricsEnabled() const;
        bool isDebugMode() const;
        
        // Generic get/set for extensibility
        template<typename T>
        std::optional<T> get(const std::string& key) const;
        
        template<typename T>
        void set(const std::string& key, const T& value);
        
        // Save current configuration to file
        bool saveToFile(const std::filesystem::path& path) const;
        
        // Load configuration from file
        bool loadFromFile(const std::filesystem::path& path);
        
        // Environment variable support
        void loadFromEnvironment();
        
        // Command line argument parsing
        void parseCommandLine(int argc, char* argv[]);

    private:
        Configuration() = default;
        ~Configuration() = default;
        Configuration(const Configuration&) = delete;
        Configuration& operator=(const Configuration&) = delete;
        
        // Internal storage
        std::unordered_map<std::string, std::string> m_values;
        std::filesystem::path m_executablePath;
        std::filesystem::path m_workingDirectory;
        
        // Helper methods
        std::filesystem::path resolveRelativePath(const std::string& relativePath) const;
        void setDefaults();
        std::string getValue(const std::string& key, const std::string& defaultValue = "") const;
        int getIntValue(const std::string& key, int defaultValue = 0) const;
        bool getBoolValue(const std::string& key, bool defaultValue = false) const;
        float getFloatValue(const std::string& key, float defaultValue = 0.0f) const;
    };
    
    // Convenience macros for common configuration access
    #define TFV_CONFIG() tfv::Configuration::instance()
    #define TFV_GET_PATH(key) TFV_CONFIG().get##key##Path()
    #define TFV_GET_SETTING(key) TFV_CONFIG().get##key()

} // namespace tfv