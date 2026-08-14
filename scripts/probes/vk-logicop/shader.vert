#version 450
layout(location = 0) out vec2 uv;
void main() {
    vec2 p = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    uv = p;
    gl_Position = vec4(p.x == 0.0 ? -1.0 : 3.0, p.y == 0.0 ? -1.0 : 3.0, 0.0, 1.0);
}
