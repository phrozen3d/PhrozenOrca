#version 110

uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
varying vec2 intensity;

varying vec3 clipping_planes_dots;

void main()
{
    if (clipping_planes_dots.x < 0.0 || clipping_planes_dots.y < 0.0 || clipping_planes_dots.z < 0.0)
        discard;

    gl_FragColor = vec4(vec3(intensity.y) + uniform_color.rgb * (intensity.x + emission_factor), uniform_color.a);
}
