/*
 * LD_PRELOAD for xdg-desktop-portal-kde: keep the "Remote control" (krfb)
 * StatusNotifierItem Passive unless $XDG_RUNTIME_DIR/crd-viewer exists
 * (created by crd-viewer-flag while a CRD client is connected).
 *
 * Build:
 *   g++ -shared -fPIC -O2 -o crd-tray-icon.so crd-tray-icon.cpp -ldl
 * Install:
 *   sudo install -m 755 crd-tray-icon.so /usr/local/lib/
 *   # then enable remote-desktop/crd-icon.conf for plasma-xdg-desktop-portal-kde
 */
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class QString;
class QObject;
class QMetaObject;

namespace {
enum ItemStatus { Passive = 1, Active = 2, NeedsAttention = 3 };

using SetStatusFn = void (*)(void *, ItemStatus);
using SetIconFn = void (*)(void *, const QString &);

SetStatusFn real_set_status = nullptr;
SetIconFn real_set_icon = nullptr;

bool viewer_wanted() {
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  char path[512];
  if (runtime && runtime[0]) {
    std::snprintf(path, sizeof(path), "%s/crd-viewer", runtime);
  } else {
    std::snprintf(path, sizeof(path), "/run/user/%d/crd-viewer", static_cast<int>(getuid()));
  }
  return access(path, F_OK) == 0;
}

void resolve() {
  if (!real_set_status) {
    real_set_status = reinterpret_cast<SetStatusFn>(
        dlsym(RTLD_NEXT, "_ZN19KStatusNotifierItem9setStatusENS_10ItemStatusE"));
  }
  if (!real_set_icon) {
    real_set_icon = reinterpret_cast<SetIconFn>(
        dlsym(RTLD_NEXT, "_ZN19KStatusNotifierItem13setIconByNameERK7QString"));
  }
}
}  // namespace

extern "C" void _ZN19KStatusNotifierItem9setStatusENS_10ItemStatusE(void *self,
                                                                    ItemStatus st) {
  resolve();
  if (!real_set_status)
    return;
  if (st != Passive && !viewer_wanted())
    st = Passive;
  real_set_status(self, st);
}

extern "C" void _ZN19KStatusNotifierItem13setIconByNameERK7QString(void *self,
                                                                   const QString &name) {
  resolve();
  if (real_set_icon)
    real_set_icon(self, name);
  // After icon changes, re-assert Passive when no client is connected.
  if (real_set_status && !viewer_wanted())
    real_set_status(self, Passive);
}
