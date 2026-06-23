#version 440

// Display one image texture, fit/zoom/pan'd into the window via a UV transform.
// window-uv (v_uv, 0..1 across the window) maps to image-uv:
//     iuv = (v_uv - 0.5) * uvScale + 0.5 + uvOffset
// Out-of-range iuv is letterbox -> true black (0 nits in scRGB, black in sRGB).
//
// Output depends on the monitor's current mode (u.sdr):
//   HDR: linear scRGB * scale (203/80) for the Windows-scRGB surface.
//   SDR: HDR images tone-mapped (Reinhard, white point), SDR images highlight
//        rolled off, then sRGB-encoded, for an sRGB surface.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform U {
    float scale;     // 203/80 for the Windows-scRGB surface
    float sdr;       // >0.5 -> SDR output (tone-map + sRGB encode)
    float imageHdr;  // >0.5 -> the image is true HDR (needs range compression on SDR)
    float rot;       // rotation quadrant: 0/1/2/3 == 0/90/180/270 clockwise
    vec2 uvScale;    // window-uv -> display-uv scale (fit/zoom)
    vec2 uvOffset;   // window-uv -> display-uv offset (pan)
    float bt2020;    // >0.5 -> image is in BT.2020 primaries; convert to BT.709 for display
    float _pad0;
    float _pad1;
    float _pad2;
} u;

// Linear BT.2020 -> linear BT.709 primaries (matches the old decode-time conversion).
vec3 bt2020ToBt709(vec3 c)
{
    return vec3(
         1.660491 * c.r - 0.587641 * c.g - 0.072850 * c.b,
        -0.124550 * c.r + 1.132900 * c.g - 0.008349 * c.b,
        -0.018151 * c.r - 0.100579 * c.g + 1.118730 * c.b);
}

layout(binding = 1) uniform sampler2D tex;

// Soft highlight roll-off: linear below the knee, asymptotes to 1.0 above it.
float rolloff(float v)
{
    const float k = 0.8;
    return v <= k ? v : k + (1.0 - k) * (1.0 - exp(-(v - k) / (1.0 - k)));
}

vec3 srgbEncode(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

void main()
{
    // window-uv -> display-uv (the image as the user sees it, after rotation).
    vec2 duv = (v_uv - 0.5) * u.uvScale + 0.5 + u.uvOffset;

    // Letterbox / pillarbox: anything outside the image is true black.
    if (duv.x < 0.0 || duv.x > 1.0 || duv.y < 0.0 || duv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // display-uv -> texture-uv: clockwise rotation swizzle.
    int rot = int(u.rot + 0.5);
    vec2 iuv;
    if (rot == 1)      iuv = vec2(duv.y, 1.0 - duv.x);
    else if (rot == 2) iuv = vec2(1.0 - duv.x, 1.0 - duv.y);
    else if (rot == 3) iuv = vec2(1.0 - duv.y, duv.x);
    else               iuv = duv;

    vec3 color = texture(tex, iuv).rgb;

    // Image is stored in its native primaries; convert BT.2020 -> BT.709 for the
    // BT.709-primaries surface. Clamp out-of-709-gamut negatives for safe display
    // (export keeps the full BT.2020 data via a separate path).
    if (u.bt2020 > 0.5)
        color = max(bt2020ToBt709(color), vec3(0.0));

    if (u.sdr > 0.5) {
        if (u.imageHdr > 0.5) {
            // True HDR content on an SDR monitor: compress the whole range (Reinhard
            // with a white point) so it reads like a normal photo, not a bright wash.
            float L = dot(color, vec3(0.2126, 0.7152, 0.0722));
            const float Lw = 6.0;
            float Lt = L * (1.0 + L / (Lw * Lw)) / (1.0 + L);
            color *= (L > 1e-4) ? (Lt / L) : 1.0;
        } else {
            // SDR content: roll highlights off (near-identity), keep white.
            color = vec3(rolloff(color.r), rolloff(color.g), rolloff(color.b));
        }
        fragColor = vec4(srgbEncode(color), 1.0);
    } else {
        // HDR monitor: linear scRGB scaled for the Windows-scRGB surface.
        fragColor = vec4(color * u.scale, 1.0);
    }
}
