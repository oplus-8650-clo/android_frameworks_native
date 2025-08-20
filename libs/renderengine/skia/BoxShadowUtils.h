
#include <android/gui/BoxShadowSettings.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkShader.h>
#include <array>
#include "compat/SkiaGpuContext.h"

namespace android::renderengine::skia {

struct BoxShadowData;

class BoxShadowUtils {
public:
    void init(SkiaGpuContext* context);
    void cleanup();
    void drawBoxShadows(SkCanvas* canvas, const SkRect& rect, float cornerRadius,
                        const android::gui::BoxShadowSettings& settings);

private:
    static constexpr std::array<int, 8> kSupportedBlurRadius = {8, 12, 14, 16, 24, 28, 48, 64};

    static constexpr float kDefaultCornerRadius = 32.0f;
    sk_sp<SkShader> mBlurImages[kSupportedBlurRadius.size()];
};

} // namespace android::renderengine::skia