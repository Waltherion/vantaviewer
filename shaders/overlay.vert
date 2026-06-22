#version 440

// A 2D overlay quad: position is already in normalised device coords, uv in 0..1.
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 v_uv;

void main()
{
    v_uv = uv;
    gl_Position = vec4(pos, 0.0, 1.0);
}
