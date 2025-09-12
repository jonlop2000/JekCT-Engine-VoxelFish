$input a_position, a_normal
$output v_normal

#include <bgfx_shader.sh>

void main()
{
    // Transform normal to view space (for proper lighting)
    vec3 normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    v_normal = normal;
    
    // Transform position to clip space
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
