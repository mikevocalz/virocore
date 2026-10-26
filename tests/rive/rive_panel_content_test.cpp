// Host test for VRORivePanelContent teardown order. Imports
// tests/rive/fixtures/panel-content.riv with rive's NoOpFactory and replaces
// live content the way VRORivePanelScene::load and selectArtboard do. Main's
// animation binds a keyframe value to the view model; once the state machine
// has advanced, its LinearAnimationInstance holds a data bind on the
// artboard and removes it in its destructor. Releasing the artboard before
// the state machine is a heap-use-after-free that AddressSanitizer reports.
// Run by scripts/test-rive-panel-content.sh, which builds with
// -fsanitize=address.

#include "VRORivePanelContent.h"

#include "utils/no_op_factory.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

void expect(bool condition, const char *what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

// Instantiates and advances far enough to resolve the keyframe bind.
VRORivePanelContent animated(const rive::File &file, const std::string &artboard) {
    VRORivePanelContent content;
    std::string error;
    expect(VRORivePanelInstantiate(file, artboard, std::string(), &content, &error),
           "instantiate succeeds");
    expect(error.empty(), "no instantiate note");
    expect(content.artboard != nullptr, "artboard instantiated");
    expect(content.stateMachine != nullptr, "default state machine instantiated");
    expect(content.viewModel != nullptr, "default view model bound");
    if (content.stateMachine != nullptr) content.stateMachine->advanceAndApply(0.5f);
    return content;
}

void testReplaceContent(const rive::File &file) {
    // Main's source artboard indexes the keyframe bind the test relies on.
    VRORivePanelContent main = animated(file, "Main");
    expect(main.artboard != nullptr && main.artboard->artboardSource() != nullptr &&
               main.artboard->artboardSource()->hasKeyFrameSourceBinds(),
           "fixture binds a keyframe value");

    // selectArtboard: live content replaced by a move.
    main = animated(file, "Second");
    expect(main.artboard != nullptr && main.artboard->name() == "Second",
           "move assignment takes the new artboard");

    // load: live content cleared, then replaced.
    main = animated(file, "Main");
    main.reset();
    expect(main.artboard == nullptr && main.stateMachine == nullptr && main.viewModel == nullptr,
           "reset clears every member");
    main = animated(file, "Main");

    // Self-move keeps the content.
    VRORivePanelContent &alias = main;
    main = std::move(alias);
    expect(main.artboard != nullptr, "self move assignment is a no-op");
    // Destructor tears down the last content.
}

void testMissingNames(const rive::File &file) {
    VRORivePanelContent content = animated(file, "Main");
    std::string error;
    expect(!VRORivePanelInstantiate(file, "Nope", std::string(), &content, &error),
           "unknown artboard is refused");
    expect(content.artboard != nullptr && content.artboard->name() == "Main",
           "refused instantiate keeps the previous content");

    error.clear();
    VRORivePanelContent fallback;
    expect(VRORivePanelInstantiate(file, "Main", "Nope", &fallback, &error),
           "unknown state machine falls back");
    expect(!error.empty(), "fallback is reported");
    expect(fallback.stateMachine != nullptr && fallback.stateMachine->name() == "Spin",
           "fallback is the default state machine");
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s panel-content.riv\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    rive::NoOpFactory factory;
    rive::ImportResult result = rive::ImportResult::malformed;
    rive::rcp<rive::File> file =
        rive::File::import(rive::Span<const uint8_t>(bytes.data(), bytes.size()), &factory, &result);
    if (file == nullptr || result != rive::ImportResult::success) {
        std::fprintf(stderr, "FAIL: could not import %s\n", argv[1]);
        return 1;
    }
    testReplaceContent(*file);
    testMissingNames(*file);
    if (gFailures != 0) {
        std::fprintf(stderr, "rive panel content: %d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("rive panel content: %d checks passed\n", gChecks);
    return 0;
}
