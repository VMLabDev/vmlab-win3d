#version 450
layout(location = 0) in vec4 v_col;
layout(location = 0) out vec4 out_col;
void main()
{
    out_col = v_col;
}
