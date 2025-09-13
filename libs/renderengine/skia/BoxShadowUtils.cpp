#include "BoxShadowUtils.h"

#include <common/trace.h>

namespace android::renderengine::skia {

const SkString kEffectSource_BoxShadowEffect(R"(
uniform vec4  u_rectBounds;     // l, t, r, b
uniform float u_cornerRadius;

uniform vec4  u_keyShadowColor;
uniform vec2  u_keyOffset;
uniform float u_keyBlurRadius;
uniform float u_keySpreadRadius;

uniform vec4  u_ambientShadowColor;
uniform vec2  u_ambientOffset;
uniform float u_ambientBlurRadius;
uniform float u_ambientSpreadRadius;

float sdRoundRect(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Accurate approximation of erf
// This can be further approximated and probably
// nooone would notice the reduced visual quality.
float erf(float x) {
    return sign(x)*sqrt(1.0-exp2(-1.78776*x*x));
}

// Gaussian blur in 1D looks good enough.
float shadow(float x, float blurRadius)
{
    float sigma = 0.57735 * blurRadius + 0.5;
    return 0.5*(1.0 - erf(x/(sigma*1.414213)));
}

half4 main(vec2 fragCoord) {
    vec2 rectSize = u_rectBounds.zw - u_rectBounds.xy;
    vec2 halfDims = rectSize * 0.5;
    vec2 rectCenter = u_rectBounds.xy + halfDims;

    // Ambient Shadow Calculation
    vec2 ambientHalfDims = halfDims + u_ambientSpreadRadius;
    vec2 ambientP = fragCoord - rectCenter - u_ambientOffset;
    float ambientDist = sdRoundRect(ambientP, ambientHalfDims, u_cornerRadius + u_ambientSpreadRadius);
    float ambientIntensity = shadow(ambientDist, u_ambientBlurRadius);
    vec4 ambientColor = u_ambientShadowColor * ambientIntensity;

    // Key Shadow Calculation
    vec2 keyHalfDims = halfDims + u_keySpreadRadius;
    vec2 keyP = fragCoord - rectCenter - u_keyOffset;
    float keyDist = sdRoundRect(keyP, keyHalfDims, u_cornerRadius + u_keySpreadRadius);
    float keyIntensity = shadow(keyDist, u_keyBlurRadius);
    vec4 keyColor = u_keyShadowColor * keyIntensity;

    // Blend the two shadow colors (standard src-over)
    return keyColor + ambientColor * (1.0 - keyColor.a);
}
)");

BoxShadowUtils::BoxShadowUtils(RuntimeEffectManager& manager) : mManager(manager) {}

void BoxShadowUtils::drawBoxShadows(SkCanvas* canvas, const SkRect& rect, float cornerRadius,
                                    const android::gui::BoxShadowSettings& settings,
                                    bool shouldDrawFpkRect) {
    SFTRACE_CALL();

    if (settings.boxShadows.size() == 0) {
        return;
    }

    sk_sp<SkRuntimeEffect> effect = mManager.mKnownEffects[kBoxShadowEffect];

    android::gui::BoxShadowSettings::BoxShadowParams keyParams = settings.boxShadows[0];
    android::gui::BoxShadowSettings::BoxShadowParams ambientParams;
    if (settings.boxShadows.size() > 1) {
        ambientParams = settings.boxShadows[1];
    }

    // Calculate the total bounding box needed to draw both shadows.
    SkRect keyShadowRect = rect.makeOutset(keyParams.spreadRadius, keyParams.spreadRadius)
                                   .makeOffset(keyParams.offsetX, keyParams.offsetY);

    float keyOutset = convertBlurUserRadiusToSigma(keyParams.blurRadius) * 3.0f;
    SkRect keyDrawRect = keyShadowRect.makeOutset(keyOutset, keyOutset);

    SkRect ambientShadowRect =
            rect.makeOutset(ambientParams.spreadRadius, ambientParams.spreadRadius)
                    .makeOffset(ambientParams.offsetX, ambientParams.offsetY);
    float ambientOutset = convertBlurUserRadiusToSigma(ambientParams.blurRadius) * 3.0f;
    SkRect ambientDrawRect = ambientShadowRect.makeOutset(ambientOutset, ambientOutset);

    SkRect unionDrawRect = keyDrawRect;
    unionDrawRect.join(ambientDrawRect);

    // Set up the shader
    SkRuntimeShaderBuilder builder(effect);
    builder.uniform("u_rectBounds") = SkV4{rect.fLeft, rect.fTop, rect.fRight, rect.fBottom};
    builder.uniform("u_cornerRadius") = cornerRadius;

    builder.uniform("u_keyShadowColor") = SkColor4f::FromColor(keyParams.color);
    builder.uniform("u_keyOffset") = SkV2{keyParams.offsetX, keyParams.offsetY};
    builder.uniform("u_keyBlurRadius") = keyParams.blurRadius;
    builder.uniform("u_keySpreadRadius") = keyParams.spreadRadius;

    builder.uniform("u_ambientShadowColor") = SkColor4f::FromColor(ambientParams.color);
    builder.uniform("u_ambientOffset") = SkV2{ambientParams.offsetX, ambientParams.offsetY};
    builder.uniform("u_ambientBlurRadius") = ambientParams.blurRadius;
    builder.uniform("u_ambientSpreadRadius") = ambientParams.spreadRadius;

    SkPaint shadowPaint;
    shadowPaint.setAntiAlias(false);
    shadowPaint.setShader(builder.makeShader());

    // Draw the combined shadow in a single draw call.
    canvas->drawRect(unionDrawRect, shadowPaint);

    if (shouldDrawFpkRect) {
        SFTRACE_NAME("FPKOptimization");
        // This optimization is just for Ganesh and can be removed once graphite is
        // enabled.
        // On a device with ARM Mali-G710 MP7 with 4 chrome windows open this
        // reduces GPU work period from 16ms to 10ms.
        float kSin45Deg = 0.70710678118f;
        SkRect killRect = rect;
        float inset = (1.0f - kSin45Deg) * cornerRadius;
        killRect.inset(inset, inset);

        SkPaint paint;
        paint.setAntiAlias(false);
        paint.setColor(0);
        paint.setBlendMode(SkBlendMode::kSrc);
        canvas->drawRect(killRect, paint);
    }
}

} // namespace android::renderengine::skia