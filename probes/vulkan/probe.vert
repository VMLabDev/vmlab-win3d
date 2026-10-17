#version 450
// Pass-through: the probe sends NDC with y up; Vulkan clip space has y down.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_col;
layout(location = 0) out vec4 v_col;
void main()
{
    gl_Position = vec4(in_pos.x, -in_pos.y, 0.0, 1.0);
    v_col = in_col;
}
