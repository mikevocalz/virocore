#include "VROEngineWorldMeshABI.h"
#include <cstdlib>
#include <iostream>
extern "C" int viro_eskiu_world_mesh_chunk_validate(VROEngineWorldMeshChunk *);
static void expect(bool ok,const char*m){if(!ok){std::cerr<<"FAIL "<<m<<"\n";std::exit(1);}}
int main(){
 VROEngineVertexAttribute attr{VRO_ENGINE_VERTEX_POSITION,VRO_ENGINE_COMPONENT_FLOAT32,3,0};
 float v[]={0,0,0,1,0,0,0,1,0}; uint32_t idx[]={0,1,2}; float conf[]={1,.8f,.9f};
 VROEngineWorldMeshChunk c{};c.struct_size=sizeof(c);c.update_kind=VRO_ENGINE_WORLD_MESH_UPSERT;c.chunk_id=7;c.version=1;c.source=VRO_ENGINE_WORLD_MESH_SOURCE_LIDAR;
 c.geometry.struct_size=sizeof(VROEngineGeometryDesc);c.geometry.topology=VRO_ENGINE_TOPOLOGY_TRIANGLES;c.geometry.vertex_stride_bytes=12;c.geometry.vertex_count=3;c.geometry.index_type=VRO_ENGINE_INDEX_UINT32;c.geometry.index_count=3;c.geometry.attribute_count=1;c.geometry.attributes=&attr;c.geometry.vertices={reinterpret_cast<const uint8_t*>(v),sizeof(v)};c.geometry.indices={reinterpret_cast<const uint8_t*>(idx),sizeof(idx)};c.geometry.geometry_id=7;c.geometry.version=1;c.confidences={reinterpret_cast<const uint8_t*>(conf),sizeof(conf)};
 auto same=[&](const char*l){expect((int)viro_engine_world_mesh_chunk_validate(&c)==viro_eskiu_world_mesh_chunk_validate(&c),l);};
 same("valid"); c.confidences.length=4;same("short confidence");c.confidences.length=sizeof(conf);
 c.source=99;same("bad source");c.source=1;
 VROEngineWorldMeshChunk rm{};rm.struct_size=sizeof(rm);rm.update_kind=VRO_ENGINE_WORLD_MESH_REMOVE;rm.chunk_id=7;rm.version=2;rm.source=1;
 expect((int)viro_engine_world_mesh_chunk_validate(&rm)==viro_eskiu_world_mesh_chunk_validate(&rm),"remove");
 rm.confidences.data=reinterpret_cast<const uint8_t*>(conf);expect((int)viro_engine_world_mesh_chunk_validate(&rm)==viro_eskiu_world_mesh_chunk_validate(&rm),"remove payload invalid");
 std::cout<<"C++ / Eskiu world mesh differential validation: PASS\n";
}
