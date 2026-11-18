#define CLAY_IMPLEMENTATION
#include <clay.h>

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

static void on_error(Clay_ErrorData error) {
    std::printf("clay error: %.*s\n", static_cast<int>(error.errorText.length), error.errorText.chars);
    std::abort();
}

static bool box_is(Clay_BoundingBox box, float x, float y, float width, float height) {
    return box.x == x && box.y == y && box.width == width && box.height == height;
}

int main() {
    const uint32_t size = Clay_MinMemorySize();
    void* memory = std::malloc(size);
    check(memory != nullptr, "arena memory");
    if (memory == nullptr)
        return 1;
    const Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, memory);
    Clay_Initialize(arena, Clay_Dimensions{ 800, 600 },
                    Clay_ErrorHandler{ .errorHandlerFunction = on_error, .userData = nullptr });

    // C++20 designated initializers, in declaration order.
    Clay_BeginLayout();
    CLAY({ .id = CLAY_ID("Outer"),
           .layout = { .sizing = { CLAY_SIZING_GROW(0, 0), CLAY_SIZING_GROW(0, 0) },
                       .padding = CLAY_PADDING_ALL(16),
                       .layoutDirection = CLAY_TOP_TO_BOTTOM },
           .backgroundColor = { 255, 0, 0, 255 } }) {
        CLAY({ .id = CLAY_ID("Child"),
               .layout = { .sizing = { CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(50) } },
               .backgroundColor = { 0, 0, 255, 255 } }) {}
    }
    const Clay_RenderCommandArray commands = Clay_EndLayout();

    check(commands.length == 2, "render command count");
    if (commands.length != 2)
        return 1;
    check(commands.internalArray[0].commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE, "outer is a rectangle");
    check(box_is(commands.internalArray[0].boundingBox, 0, 0, 800, 600), "outer box");
    check(commands.internalArray[1].commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE, "child is a rectangle");
    check(box_is(commands.internalArray[1].boundingBox, 16, 16, 100, 50), "child box");

    const Clay_ElementData child = Clay_GetElementData(CLAY_ID("Child"));
    check(child.found, "child found");
    check(box_is(child.boundingBox, 16, 16, 100, 50), "child element box");

    std::free(memory);
    if (failures != 0)
        return 1;
    std::printf("clay C++ consumer test passed (Clay_ChildAlignment %zu, Clay_LayoutConfig %zu bytes)\n",
                sizeof(Clay_ChildAlignment), sizeof(Clay_LayoutConfig));
    return 0;
}
