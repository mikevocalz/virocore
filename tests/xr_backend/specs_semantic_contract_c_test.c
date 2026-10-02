#include "VROBackendSemanticContract.h"
#include <assert.h>
#include <math.h>
static int closef(float a,float b){ return fabsf(a-b)<0.00001f; }
int main(void){
  assert(closef(viro_backend_units_from_meters(-1.5f),-1.5f));
  assert(closef(viro_backend_meters_from_units(5.03298187f),5.03298187f));
  assert(closef(viro_backend_polyline_radius_meters(.008f),.004f));
  assert(closef(viro_backend_hit_distance_meters(5.03298187f),5.03298187f));
  VROBackendSemanticQuaternion q=viro_backend_quaternion_from_euler_degrees(0,0,90);
  assert(closef(q.x,0)); assert(closef(q.y,0));
  assert(closef(q.z,.70710678118f)); assert(closef(q.w,.70710678118f));
  return 0;
}
