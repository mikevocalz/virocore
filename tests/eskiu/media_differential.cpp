#include "VROEngineMediaABI.h"
#include <cstdlib>
#include <iostream>
#include <vector>

extern "C" {
int viro_eskiu_media_plane_validate(VROEngineMediaPlane *);
int viro_eskiu_media_frame_validate(VROEngineMediaFrame *);
int viro_eskiu_media_frame_payload_bytes(VROEngineMediaFrame *, uint64_t *);
}
static void expect(bool ok,const char *m){ if(!ok){std::cerr<<"FAIL "<<m<<"\n";std::exit(1);} }

int main(){
 std::vector<uint8_t> rgba(64,0x7f);
 VROEngineMediaPlane p{}; p.struct_size=sizeof(p); p.plane_index=0;p.width=4;p.height=4;p.row_stride_bytes=16;p.pixel_stride_bytes=4;p.bytes={rgba.data(),rgba.size()};
 expect((int)viro_engine_media_plane_validate(&p)==viro_eskiu_media_plane_validate(&p),"plane valid");
 auto badp=p; badp.bytes.length=1; expect((int)viro_engine_media_plane_validate(&badp)==viro_eskiu_media_plane_validate(&badp),"plane short");
 VROEngineMediaFrame f{}; f.struct_size=sizeof(f);f.width=4;f.height=4;f.pixel_format=VRO_ENGINE_PIXEL_RGBA8_UNORM;f.color_space=VRO_ENGINE_COLOR_SPACE_SRGB;f.color_range=VRO_ENGINE_COLOR_RANGE_FULL;f.plane_count=1;f.ownership=VRO_ENGINE_MEDIA_BORROWED_CALL;f.planes=&p;
 expect((int)viro_engine_media_frame_validate(&f)==viro_eskiu_media_frame_validate(&f),"frame valid");
 uint64_t a=0,b=0; expect((int)viro_engine_media_frame_payload_bytes(&f,&a)==viro_eskiu_media_frame_payload_bytes(&f,&b)&&a==b,"payload");
 f.ownership=VRO_ENGINE_MEDIA_BORROWED_LEASE;f.lease=0;expect((int)viro_engine_media_frame_validate(&f)==viro_eskiu_media_frame_validate(&f),"lease invalid");
 VROEngineMediaFrame gpu{};gpu.struct_size=sizeof(gpu);gpu.width=1;gpu.height=1;gpu.pixel_format=1;gpu.color_space=0;gpu.color_range=0;gpu.plane_count=0;gpu.ownership=3;gpu.surface=9;
 expect((int)viro_engine_media_frame_validate(&gpu)==viro_eskiu_media_frame_validate(&gpu),"gpu frame");
 std::cout<<"C++ / Eskiu media differential validation: PASS\n";
}
