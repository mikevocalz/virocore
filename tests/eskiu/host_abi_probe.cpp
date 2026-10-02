#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

extern "C" {

struct ViroEskiuPose {
    float px;
    float py;
    float pz;
    float qx;
    float qy;
    float qz;
    float qw;
};

int viro_eskiu_contract_version();
void viro_eskiu_write_identity(ViroEskiuPose *pose);
float viro_eskiu_pose_position_sum(const ViroEskiuPose *pose);
int viro_eskiu_call_cpp(int base, int delta);

int viro_cpp_accumulate(int base, int delta) {
    return base + delta;
}

}

static_assert(sizeof(ViroEskiuPose) == sizeof(float) * 7,
              "Pose layout must remain seven tightly packed floats");
static_assert(offsetof(ViroEskiuPose, qw) == sizeof(float) * 6,
              "Pose field offsets are part of the cross-language memory view");

int main() {
    assert(viro_eskiu_contract_version() == 1);

    ViroEskiuPose identity{
        99.0f, 99.0f, 99.0f,
        99.0f, 99.0f, 99.0f, 99.0f
    };
    viro_eskiu_write_identity(&identity);
    assert(std::fabs(identity.px) < 0.00001f);
    assert(std::fabs(identity.py) < 0.00001f);
    assert(std::fabs(identity.pz) < 0.00001f);
    assert(std::fabs(identity.qx) < 0.00001f);
    assert(std::fabs(identity.qy) < 0.00001f);
    assert(std::fabs(identity.qz) < 0.00001f);
    assert(std::fabs(identity.qw - 1.0f) < 0.00001f);

    const ViroEskiuPose sample{
        1.0f, 2.0f, 3.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    assert(std::fabs(viro_eskiu_pose_position_sum(&sample) - 6.0f) < 0.00001f);
    assert(viro_eskiu_call_cpp(40, 2) == 42);

    std::cout << "Eskiu host C ABI smoke: PASS\n";
    return 0;
}
