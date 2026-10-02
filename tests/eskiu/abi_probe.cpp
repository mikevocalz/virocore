#include <cmath>
#include <cstdlib>
#include <iostream>

extern "C" {

struct ViroAbiPose {
    float px;
    float py;
    float pz;
    float qx;
    float qy;
    float qz;
    float qw;
    int flags;
};

int viro_eskiu_add(int a, int b);
int viro_eskiu_callback_roundtrip(int value);
float viro_eskiu_pose_checksum(ViroAbiPose *pose);

int viro_cpp_scale(int value) {
    return value * 3;
}

} // extern "C"

static_assert(sizeof(ViroAbiPose) == 32, "ABI probe struct layout changed");

int main() {
    if (viro_eskiu_add(7, 5) != 12) {
        std::cerr << "C++ -> Eskiu scalar call failed\n";
        return EXIT_FAILURE;
    }

    if (viro_eskiu_callback_roundtrip(4) != 13) {
        std::cerr << "Eskiu -> C++ callback failed\n";
        return EXIT_FAILURE;
    }

    ViroAbiPose pose{
        1.0f, 2.0f, 3.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        2
    };
    const float expected = 9.0f;
    const float actual = viro_eskiu_pose_checksum(&pose);
    if (std::fabs(actual - expected) > 0.0001f) {
        std::cerr << "POD struct ABI call failed: expected " << expected
                  << ", got " << actual << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "ViroCore <-> Eskiu ABI probe passed\n";
    return EXIT_SUCCESS;
}
