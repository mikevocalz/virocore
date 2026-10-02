#ifndef VRO_BACKEND_SEMANTIC_CONTRACT_H
#define VRO_BACKEND_SEMANTIC_CONTRACT_H
#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct VROBackendSemanticQuaternion { float x,y,z,w; } VROBackendSemanticQuaternion;
static inline float viro_backend_units_from_meters(float meters){ return meters; }
static inline float viro_backend_meters_from_units(float units){ return units; }
static inline float viro_backend_polyline_radius_meters(float thickness_meters){ return thickness_meters * 0.5f; }
static inline float viro_backend_degrees_to_radians(float degrees){ return degrees * 0.01745329251994329576923690768489f; }
static inline VROBackendSemanticQuaternion viro_backend_quaternion_from_euler_degrees(float x_deg,float y_deg,float z_deg){
    const double x=(double)viro_backend_degrees_to_radians(x_deg);
    const double y=(double)viro_backend_degrees_to_radians(y_deg);
    const double z=(double)viro_backend_degrees_to_radians(z_deg);
    const double sr=sin(x*.5), cr=cos(x*.5), sp=sin(y*.5), cp=cos(y*.5), sy=sin(z*.5), cy=cos(z*.5);
    const double cpcy=cp*cy, spcy=sp*cy, cpsy=cp*sy, spsy=sp*sy;
    VROBackendSemanticQuaternion q={(float)(sr*cpcy-cr*spsy),(float)(cr*spcy+sr*cpsy),(float)(cr*cpsy-sr*spcy),(float)(cr*cpcy+sr*spsy)};
    const float n=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(n>0){ q.x/=n; q.y/=n; q.z/=n; q.w/=n; }
    return q;
}
static inline float viro_backend_hit_distance_meters(float meters){ return meters; }
#ifdef __cplusplus
}
#endif
#endif
