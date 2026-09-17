#version 330 core

// Post-process pass: always-on screen appearance (brightness/contrast/
// saturation), plus CRT/bloom stylization (scanlines, vignette, a crude
// single-pass bloom, chromatic "color tear") gated by uCrtEnabled and
// scaled by uCrtEffectStrength. Deliberately not a crt-geom/guest-advanced
// port -- see PLAN.md Phase 5 note to keep this pass simple.
//
// With every uniform at its default (see App's member initializers), this
// reproduces the original fixed-look pass exactly.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec2 uResolution;

uniform float uBrightness;        // additive, e.g. -0.5..0.5
uniform float uContrast;          // multiplier around the mid-gray pivot
uniform float uSaturation;        // 0 = grayscale, 1 = normal, >1 = oversaturated

uniform float uCrtEnabled;        // 0.0 or 1.0
uniform float uCrtEffectStrength; // overall multiplier on the CRT delta (unclamped -- >1 exaggerates)
uniform float uBloomStrength;
uniform float uScanlineCount;
uniform float uVignetteStrength;
uniform float uColorTear;         // channel-shift amount, in pixels

vec3 sampleBloom(vec2 uv) {
    vec2 texel = 1.0 / uResolution;
    vec3 sum = vec3(0.0);
    float total = 0.0;

    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 offset = vec2(float(x), float(y)) * texel * 1.5;
            vec3 c = texture(uTexture, uv + offset).rgb;
            float bright = max(max(c.r, c.g), c.b);
            float w = bright * bright;  // emphasize bright areas only
            sum += c * w;
            total += w;
        }
    }
    return total > 0.0001 ? sum / total : vec3(0.0);
}

void main() {
    vec3 base = texture(uTexture, vUV).rgb;

    // Color tear: a cheap chromatic-aberration-style channel shift,
    // standing in for analog signal bleed. Skipped entirely at 0 (the
    // default) so the common case avoids the extra texture fetches.
    if (uColorTear > 0.0001) {
        vec2 texel = 1.0 / uResolution;
        vec2 offset = vec2(uColorTear * texel.x, 0.0);
        base = vec3(texture(uTexture, vUV + offset).r, texture(uTexture, vUV).g,
                    texture(uTexture, vUV - offset).b);
    }

    vec3 bloom = sampleBloom(vUV) * uBloomStrength;
    vec3 crtColor = base + bloom * 0.35;

    // Scanlines: darken periodically at a configurable line count,
    // independent of the output's actual pixel resolution.
    float scanline = sin(vUV.y * uScanlineCount * 3.14159265);
    float scanlineMul = mix(0.80, 1.0, scanline * 0.5 + 0.5);
    crtColor *= scanlineMul;

    // Vignette: darken toward the corners.
    vec2 centered = vUV * 2.0 - 1.0;
    float vignette = 1.0 - dot(centered, centered) * uVignetteStrength;
    crtColor *= clamp(vignette, 0.0, 1.0);

    // Blend the stylized CRT look in by its delta from the base image, so
    // uCrtEffectStrength can exaggerate past 1.0 instead of just lerping
    // up to a hard "fully on" ceiling; uCrtEnabled fully gates it off.
    float crtMix = uCrtEnabled * uCrtEffectStrength;
    vec3 color = base + (crtColor - base) * crtMix;

    // Screen appearance: always applied, independent of the CRT toggle --
    // these represent display calibration, not a stylistic effect.
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, uSaturation);
    color = (color - 0.5) * uContrast + 0.5 + uBrightness;

    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
