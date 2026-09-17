// Meta Quest build: draws one depth layer of the game as a textured quad in 3D space
in vec3 a_Position;
in vec2 a_Texcoord0;

uniform mat4 u_MVPMatrix;

out vec2 v_Texcoord0;

void main() {
    v_Texcoord0 = a_Texcoord0;
    gl_Position = u_MVPMatrix * vec4(a_Position, 1.0);
}
