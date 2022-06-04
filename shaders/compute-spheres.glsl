#version 450

// windows of 8x8.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform writeonly image2D img;


#define MAT_LAMBERTIAN_ID 0
#define MAT_METAL_ID 1
#define MAT_DIELECTRIC_ID 2


struct Sphere {
  vec3 center;
  float radius;
  uint  mat_id; // assigned material ID. Nothing to do with previous `World``s material index.
}

struct Hit {
  vec3 point;
  vec3 normal;
  float selected_t;
  bool  is_hit;
  bool  is_facing_outward;
}
/**/
struct Ray {
  vec3 origin;
  vec3 direction; // normalized
}

void make_facing_outward(inout Hit hit, in Ray ray) {
  if (dot(ray.direction, hit.normal) < 0.) {
    hit.normal *= -1.;
  }
}

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 randvec(vec2 co)

layout(set = 0, binding = 1) readonly buffer Spheres {
  Sphere spheres[];
} world;





void main() {
  vec2 uv = (gl_GlobalInvocationID.xy + vec2(0.5)) / vec2(imageSize(img).xy);


  // let's assign a different color white/blue/red depending on its place first,
  // to know that the UVs are OK and that the shader works.
  vec3 col = vec3(uv.x, 0., uv.y) * length(uv);

  imageStore(img, vec4(col, 1.0));
}

