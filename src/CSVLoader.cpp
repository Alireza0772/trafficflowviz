#include "CSVLoader.hpp"

#include <fstream>
#include <iostream> // <— add
#include <sstream>

namespace tfv
{

    std::vector<Vehicle> loadVehiclesCSV(const std::filesystem::path& path)
    {
        std::vector<Vehicle> out;
        std::ifstream file(path);
        if(!file.is_open())
        {
            std::cerr << "[Road] could not open " << path << '\n';
        }

        std::string line;
        std::getline(file, line);

        while(std::getline(file, line))
        {
            std::stringstream ss(line);
            Vehicle v{};
            char comma;
            ss >> v.id >> comma >> v.segment >> comma >> v.position >> comma >> v.speed;

            if(ss)
            {
                out.push_back(v);
                std::cout << "[CSV] id=" << v.id << " seg=" << v.segment << " pos=" << v.position
                          << " speed=" << v.speed << '\n'; // ← log record
            }
            else
            {
                std::cerr << "[CSV] skipping malformed line: " << line << '\n';
            }
        }
        return out;
    }

} // namespace tfv