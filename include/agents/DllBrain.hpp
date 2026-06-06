#ifndef TFV_DLL_BRAIN_HPP
#define TFV_DLL_BRAIN_HPP

#include "agents/Brain.hpp"

#include <memory>
#include <string>

namespace tfv
{
    /**
     * Load a brain from a shared library that implements tfv_brain.h (exports the C
     * symbol "tfv_brain_create"). The library is opened, the factory is called, the
     * HARD ABI gate is applied, and the result is wrapped in a VtableBrain. The dl
     * handle is kept alive for the brain's lifetime and closed after the brain is
     * destroyed.
     *
     * Returns nullptr on ANY failure (cannot open / missing symbol / creation failed /
     * ABI mismatch) so the caller can fall back to the rule brain. Unsupported on
     * non-POSIX platforms (returns nullptr).
     */
    std::unique_ptr<IBrain> loadDllBrain(const std::string& path, const std::string& config);

} // namespace tfv

#endif // TFV_DLL_BRAIN_HPP
