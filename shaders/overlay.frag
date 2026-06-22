#version 440

// Draws a 2D overlay (the info panel, and later crop chrome) over the image. The
// panel is authored in sRGB (8-bit, straight alpha). To match the surface:
//   SDR surface: pass the sRGB-encoded colour straight through.
//   HDR scRGB surface: linearise and scale to ~203 nits so UI reads like SDR white.
// Alpha-blended with SrcAlpha / OneMinusSrcAlpha.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform U {
    float scale; // 203/80 for the Windows-scRGB surface
    float sdr;   // >0.5 -> SDR surface (pass-through)
    float _pad0;
    float _pad1;
} u;

layout(binding = 1) uniform sampler2D panel;

vec3 srgbToLinear(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

void main()
{
    vec4 t = texture(panel, v_uv);
    vec3 c = (u.sdr > 0.5) ? t.rgb : srgbToLinear(t.rgb) * u.scale;
    fragColor = vec4(c, t.a);
}
