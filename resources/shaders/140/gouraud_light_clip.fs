#version 140

uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
in vec2 intensity;

in vec3 clipping_planes_dots;

out vec4 out_color;

void main()
{
    if (clipping_planes_dots.x < 0.0 || clipping_planes_dots.y < 0.0 || clipping_planes_dots.z < 0.0)
        discard;

    out_color = vec4(vec3(intensity.y) + uniform_color.rgb * (intensity.x + emission_factor), uniform_color.a);
}
