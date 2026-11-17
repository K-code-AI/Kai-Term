#include <cstdlib>
#include <filesystem>
#include <string>

namespace kai {

// Choose a Qt platform plugin from the ambient environment. This is
// platform selection, not compositor-specific application logic.
void prepareQtPlatform()
{
    const char* qpa = std::getenv("QT_QPA_PLATFORM");
    if (qpa && *qpa)
        return;

    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* display = std::getenv("DISPLAY");

    if (!wayland || !*wayland) {
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        if (runtime && *runtime) {
            std::string found;
            try {
                for (const auto& entry : std::filesystem::directory_iterator(runtime)) {
                    const std::string name = entry.path().filename().string();
                    if (name.rfind("wayland-", 0) == 0) {
                        found = name;
                        break;
                    }
                }
            } catch (...) {
                found.clear();
            }
            if (!found.empty())
                setenv("WAYLAND_DISPLAY", found.c_str(), 1);
        }
    }

    wayland = std::getenv("WAYLAND_DISPLAY");
    if (wayland && *wayland && (!display || !*display)) {
        setenv("QT_QPA_PLATFORM", "wayland", 0);
    }
}

} // namespace kai
