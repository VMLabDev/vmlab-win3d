#version 450
// Quads in a 7x4 grid from gl_VertexIndex alone (no vertex buffers), or with
// full = 1 one quad over the whole target (Godot's blit).
layout(push_constant) uniform pc { float full; };
void main() {
    int q = gl_VertexIndex / 4, c = gl_VertexIndex % 4;
    vec2 o = vec2(float(q % 7) * 0.27 - 0.95, float(q / 7) * 0.45 - 0.9);
    vec2 d = vec2(float(c & 1), float(c >> 1)) * 0.2;
    vec2 f = vec2(float(c & 1), float(c >> 1)) * 2.0 - 1.0;
    gl_Position = vec4(full > 0.5 ? f : o + d, 0.5, 1.0);
}
