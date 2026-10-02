#include "VROBackendSemanticContract.h"
#include "VROQuaternion.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
static bool closef(float a,float b){ return std::fabs(a-b)<0.00001f; }
static bool expect(bool c,const char *m){ if(!c) std::cerr<<"FAIL: "<<m<<"\n"; return c; }
int main(){
 bool ok=true;
 const float x=20,y=30,z=40;
 VROBackendSemanticQuaternion r=viro_backend_quaternion_from_euler_degrees(x,y,z);
 VROQuaternion q(viro_backend_degrees_to_radians(x),viro_backend_degrees_to_radians(y),viro_backend_degrees_to_radians(z));
 ok&=expect(closef(r.x,q.X),"quaternion X parity");
 ok&=expect(closef(r.y,q.Y),"quaternion Y parity");
 ok&=expect(closef(r.z,q.Z),"quaternion Z parity");
 ok&=expect(closef(r.w,q.W),"quaternion W parity");
 ok&=expect(closef(viro_backend_units_from_meters(1),1),"meter units");
 ok&=expect(closef(viro_backend_polyline_radius_meters(.02f),.01f),"polyline diameter");
 ok&=expect(closef(viro_backend_hit_distance_meters(3.25f),3.25f),"hit distance");
 return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
