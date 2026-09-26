#version 430 core
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
uniform vec2 u_out_size;
in vec2 v_uv;
out vec4 FragColor;
void main() {
    vec2 scale    = u_out_size / u_tex_size;
    vec2 texel    = v_uv * u_tex_size;
    vec2 texel_f  = fract(texel);
    vec2 texel_i  = floor(texel) + 0.5;
    vec2 frange   = clamp(texel_f / fwidth(texel), 0.0, 1.0);
    vec2 uv_sharp = (texel_i + frange - 0.5) / u_tex_size;
    FragColor     = texture(u_tex, uv_sharp);
}
