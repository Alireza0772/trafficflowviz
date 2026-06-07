#ifndef TFV_BRAIN_REGISTRY_HPP
#define TFV_BRAIN_REGISTRY_HPP

#include "agents/Brain.hpp"
#include "agents/IdmParams.hpp"

#include <memory>
#include <string>

namespace tfv
{
    /**
     * Resolve a brain-kind string to an IBrain instance. Phase 2 knows only the
     * built-in "rule" brain; unknown kinds log a warning and fall back to it.
     * Phase 6 extends this to "python:...", "onnx:...", "dll:..." backends.
     */
    std::unique_ptr<IBrain> makeBrain(const std::string& kind, const IdmParams& idm);

} // namespace tfv

#endif // TFV_BRAIN_REGISTRY_HPP
