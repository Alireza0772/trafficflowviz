#include "agents/DllBrain.hpp"

#include "agents/VtableBrain.hpp"
#include "agents/tfv_brain.h"
#include "utils/LoggingManager.hpp"

#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>

namespace tfv
{
    namespace
    {
        // Owns the dl handle AND the adapter, tearing down in the correct order: the
        // brain instance is destroyed (via the vtable, whose code lives in the .so)
        // BEFORE dlclose unloads that code.
        class DllBrain final : public IBrain
        {
          public:
            DllBrain(void* handle, std::unique_ptr<VtableBrain> inner)
                : m_handle(handle), m_inner(std::move(inner))
            {
            }
            ~DllBrain() override
            {
                m_inner.reset(); // runs the vtable's destroy() while the .so is still loaded
                if(m_handle)
                    dlclose(m_handle);
            }
            DllBrain(const DllBrain&) = delete;
            DllBrain& operator=(const DllBrain&) = delete;

            void decideBatch(const Observation* obs, int n, Action* out) override
            {
                m_inner->decideBatch(obs, n, out);
            }
            void reset(uint64_t seed) override { m_inner->reset(seed); }
            std::string weightsHash() const override { return m_inner->weightsHash(); }
            const char* kindName() const override { return m_inner->kindName(); }
            bool drivesLaneChange() const override { return m_inner->drivesLaneChange(); }

          private:
            void* m_handle{nullptr};
            std::unique_ptr<VtableBrain> m_inner;
        };
    } // namespace

    std::unique_ptr<IBrain> loadDllBrain(const std::string& path, const std::string& config)
    {
        if(path.empty()) // dlopen("") would fail anyway; reject explicitly (never dlopen(NULL))
        {
            LOG_ERROR("[brain dll] empty library path");
            return nullptr;
        }
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if(!handle)
        {
            const char* err = dlerror();
            LOG_ERROR("[brain dll] cannot open {path}: {err}", PARAM(path, path),
                      PARAM(err, std::string(err ? err : "unknown")));
            return nullptr;
        }

        // Resolve the factory symbol. Use memcpy (not a direct cast) so a function/object
        // pointer round-trip is well-defined and warning-clean.
        dlerror();
        void* sym = dlsym(handle, "tfv_brain_create");
        if(!sym)
        {
            LOG_ERROR("[brain dll] missing symbol tfv_brain_create in {path}", PARAM(path, path));
            dlclose(handle);
            return nullptr;
        }
        tfv_brain_create_fn create = nullptr;
        std::memcpy(&create, &sym, sizeof(create));

        tfv_brain_desc desc{};
        const tfv_brain_vtable* vt = nullptr;
        void* self = nullptr;
        const int rc = create(config.c_str(), &desc, &vt, &self);
        if(rc != 0 || !tfv_brain_abi_ok(&desc) || !tfv_brain_vtable_ok(vt))
        {
            LOG_ERROR("[brain dll] rejected {path} (rc={rc}, abiOk={abi})", PARAM(path, path),
                      PARAM(rc, rc), PARAM(abi, tfv_brain_abi_ok(&desc) ? 1 : 0));
            if(rc == 0 && vt && tfv_brain_vtable_ok(vt) && vt->destroy)
                vt->destroy(self); // tear down a created-but-rejected instance
            dlclose(handle);
            return nullptr;
        }

        LOG_INFO("[brain dll] loaded '{kind}' from {path}",
                 PARAM(kind, std::string(desc.kindName ? desc.kindName : "?")), PARAM(path, path));
        auto inner = std::make_unique<VtableBrain>(desc, vt, self, /*owning=*/true);
        return std::make_unique<DllBrain>(handle, std::move(inner));
    }

} // namespace tfv

#else // non-POSIX

namespace tfv
{
    std::unique_ptr<IBrain> loadDllBrain(const std::string&, const std::string&)
    {
        LOG_WARN("[brain dll] DLL brains are not supported on this platform");
        return nullptr;
    }
} // namespace tfv

#endif
