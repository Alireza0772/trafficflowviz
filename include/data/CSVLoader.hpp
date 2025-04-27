#ifndef TFV_CSV_LOADER_HPP
#define TFV_CSV_LOADER_HPP

#include <filesystem>
#include <vector>

#include "core/TrafficEntity.hpp"

namespace tfv
{

    /** Simple blocking CSV reader: id,segment,position,speed (comma‑separated). */
    std::vector<Vehicle> loadVehiclesCSV(const std::filesystem::path& path);

} // namespace tfv
#endif