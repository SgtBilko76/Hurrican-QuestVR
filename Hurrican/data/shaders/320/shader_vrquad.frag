// Meta Quest build: composite one premultiplied-alpha layer into the (sRGB) eye buffer
uniform sampler2D u_Texture0;
uniform int u_SrgbDecode;

in vec2 v_Texcoord0;

out vec4 v_FragColor;

vec3 srgbToLinear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + vec3(0.055)) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

void main() {
    vec4 c = texture(u_Texture0, v_Texcoord0);
    if (u_SrgbDecode == 1 && c.a > 0.0) {
        // The game draws gamma-encoded colours; the eye buffer is an sRGB target that
        // re-encodes linear values on write. Un-premultiply, linearise, re-premultiply.
        vec3 straight = clamp(c.rgb / c.a, 0.0, 1.0);
        c.rgb = srgbToLinear(straight) * c.a;
    }
    v_FragColor = c;
}
