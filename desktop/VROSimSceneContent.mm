//
//  VROSimSceneContent.mm
//

#include "VROSimSceneContent.h"
#include "VROData.h"
#include "VROLight.h"
#include "VRONode.h"
#include "VROTexture.h"
#include <cstdint>
#include <vector>

std::shared_ptr<VROTexture> VROSimMakeCheckerTexture(int size, int cells) {
    std::vector<uint8_t> px(size * size * 4);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x / cell) + (y / cell)) % 2 == 0;
            uint8_t *p = &px[(y * size + x) * 4];
            p[0] = dark ? 235 : 245;
            p[1] = dark ? 120 : 245;
            p[2] = dark ? 40 : 245;
            p[3] = 255;
        }
    }
    std::vector<std::shared_ptr<VROData>> data = {
        std::make_shared<VROData>(px.data(), (int)px.size(), VRODataOwnership::Copy)};
    return std::make_shared<VROTexture>(VROTextureType::Texture2D, VROTextureFormat::RGBA8,
                                        VROTextureInternalFormat::RGBA8, true,
                                        VROMipmapMode::None, data, size, size,
                                        std::vector<uint32_t>{});
}

void VROSimAddLights(const std::shared_ptr<VRONode> &root) {
    auto ambient = std::make_shared<VROLight>(VROLightType::Ambient);
    ambient->setColor({1, 1, 1});
    ambient->setIntensity(300);
    root->addLight(ambient);

    auto sun = std::make_shared<VROLight>(VROLightType::Directional);
    sun->setColor({1, 1, 1});
    sun->setIntensity(900);
    sun->setDirection({-0.5f, -1.0f, -0.7f});
    root->addLight(sun);
}
