$input a_position, a_normal
$output v_normal

#include <bgfx_shader.sh>

void main()
{
    // Transform normal to world space using the normal matrix (transpose of inverse model matrix)
    // For uniform scaling, we can use the model matrix directly
    vec3 worldNormal = mul(u_model[0], vec4(a_normal, 0.0)).xyz;
    v_normal = normalize(worldNormal);
    
    // Transform position to clip space
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
