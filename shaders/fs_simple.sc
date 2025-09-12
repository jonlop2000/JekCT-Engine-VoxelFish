$input v_normal

#include <bgfx_shader.sh>

uniform vec4 u_lightDir;

void main()
{
    // Normalize the interpolated normal
    vec3 N = normalize(v_normal);
    
    // Light direction (should be normalized when set from CPU)
    vec3 L = normalize(u_lightDir.xyz);
    
    // Lambert diffuse calculation
    float diff = max(dot(N, L), 0.0);
    
    // Base color (blue-ish)
    vec3 baseColor = vec3(0.2, 0.6, 1.0);
    
    // Combine ambient and diffuse lighting
    vec3 ambient = baseColor * 0.2;
    vec3 diffuse = baseColor * 0.8 * diff;
    vec3 finalColor = ambient + diffuse;
    
    gl_FragColor = vec4(finalColor, 1.0);
}
