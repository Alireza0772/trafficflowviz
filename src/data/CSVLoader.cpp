#include "data/CSVLoader.hpp"
#include "utils/LoggingManager.hpp"

#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <sstream>
#include <string>

namespace tfv
{
    std::vector<Vehicle> loadVehiclesCSV(const std::filesystem::path& path)
    {
        std::vector<Vehicle> vehicles;
        std::ifstream file(path);

        if(!file.is_open())
        {
            LOG_ERROR("Failed to open file: {file}", PARAM(file, path.string()));
            return vehicles;
        }

        std::string line;

        // Skip header line
        std::getline(file, line);

        // Parse data lines
        while(std::getline(file, line))
        {
            std::stringstream ss(line);
            std::string token;

            Vehicle v;
            // Format: id,segmentId,position,velX,velY

            // ID
            if(std::getline(ss, token, ','))
            {
                v.id = std::stoull(token);
            }
            else
                continue;

            // Segment ID
            if(std::getline(ss, token, ','))
            {
                v.segmentId = std::stoul(token);
            }
            else
                continue;

            // Position
            if(std::getline(ss, token, ','))
            {
                v.position = std::stof(token);
            }
            else
                continue;

            // Velocity X
            float velX = 0.0f;
            if(std::getline(ss, token, ','))
            {
                velX = std::stof(token);
            }
            else
                continue;

            // Velocity Y
            float velY = 0.0f;
            if(std::getline(ss, token, ','))
            {
                velY = std::stof(token);
            }
            else
                continue;

            v.vel = glm::vec2(velX, velY);

            vehicles.push_back(v);
        }

        // Log the number of vehicles loaded
        LOG_INFO("Loaded {count} vehicles from {file}", PARAM(count, vehicles.size()),
                 PARAM(file, path.string()));
        return vehicles;
    }
} // namespace tfv
