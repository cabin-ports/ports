#include <gdextension_interface.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                  \
        }                                                                \
    } while (0)

/* The host side of the handshake. */
static unsigned char arena[64];
static void *host_mem_alloc(size_t p_bytes) { return p_bytes <= sizeof arena ? arena : NULL; }
static void host_get_godot_version(GDExtensionGodotVersion *r) {
    r->major = 4;
    r->minor = 7;
    r->patch = 0;
    r->string = "Godot Engine v4.7.stable";
}
static GDExtensionInterfaceFunctionPtr host_get_proc_address(const char *p_name) {
    if (strcmp(p_name, "mem_alloc") == 0) return (GDExtensionInterfaceFunctionPtr)host_mem_alloc;
    if (strcmp(p_name, "get_godot_version") == 0) return (GDExtensionInterfaceFunctionPtr)host_get_godot_version;
    return NULL;
}

/* The extension side: an entry point of the documented signature. */
static int initialized_level = -1, deinitialized_level = -1;
static void on_init(void *p_userdata, GDExtensionInitializationLevel p_level) {
    *(int *)p_userdata += 1;
    initialized_level = (int)p_level;
}
static void on_deinit(void *p_userdata, GDExtensionInitializationLevel p_level) {
    *(int *)p_userdata += 10;
    deinitialized_level = (int)p_level;
}
static int callbacks_seen = 0;
static void *allocated = NULL;
static GDExtensionBool entry(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library,
                             GDExtensionInitialization *r_initialization) {
    GDExtensionInterfaceGetGodotVersion get_version =
        (GDExtensionInterfaceGetGodotVersion)p_get_proc_address("get_godot_version");
    GDExtensionInterfaceMemAlloc mem_alloc = (GDExtensionInterfaceMemAlloc)p_get_proc_address("mem_alloc");
    GDExtensionGodotVersion v;
    (void)p_library;
    if (!get_version || !mem_alloc) return 0;
    get_version(&v);
    if (v.major != 4) return 0;
    allocated = mem_alloc(16);
    r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
    r_initialization->userdata = &callbacks_seen;
    r_initialization->initialize = on_init;
    r_initialization->deinitialize = on_deinit;
    return 1;
}

int main(void) {
    GDExtensionInitializationFunction fn = entry;
    GDExtensionInitialization init;
    GDExtensionInterfaceGetProcAddress get_proc = host_get_proc_address;
    GDExtensionGodotVersion version;
    GDExtensionCallError err;

    /* Enumerator values are ABI. */
    CHECK(GDEXTENSION_VARIANT_TYPE_NIL == 0);
    CHECK(GDEXTENSION_VARIANT_TYPE_BOOL == 1);
    CHECK(GDEXTENSION_VARIANT_TYPE_INT == 2);
    CHECK(GDEXTENSION_VARIANT_TYPE_FLOAT == 3);
    CHECK(GDEXTENSION_VARIANT_TYPE_STRING == 4);
    CHECK(GDEXTENSION_VARIANT_TYPE_VARIANT_MAX == 39);
    CHECK(GDEXTENSION_INITIALIZATION_CORE == 0);
    CHECK(GDEXTENSION_INITIALIZATION_SERVERS == 1);
    CHECK(GDEXTENSION_INITIALIZATION_SCENE == 2);
    CHECK(GDEXTENSION_INITIALIZATION_EDITOR == 3);
    CHECK(GDEXTENSION_MAX_INITIALIZATION_LEVEL == 4);
    CHECK(GDEXTENSION_CALL_OK == 0);
    CHECK(GDEXTENSION_CALL_ERROR_METHOD_NOT_CONST == 6);

    /* Fundamental typedef widths; char16_t and char32_t come from the header in C. */
    CHECK(sizeof(GDExtensionInt) == 8);
    CHECK(sizeof(GDExtensionBool) == 1);
    CHECK(sizeof(char32_t) == 4);
    CHECK(sizeof(char16_t) == 2);

    /* The handshake: the host's proc-address table reached through the typedefs. */
    memset(&init, 0, sizeof init);
    CHECK(get_proc("nonexistent") == NULL);
    CHECK(fn(get_proc, NULL, &init) == 1);
    CHECK(allocated == arena);
    CHECK(init.minimum_initialization_level == GDEXTENSION_INITIALIZATION_SCENE);
    CHECK(init.userdata == &callbacks_seen);
    init.initialize(init.userdata, GDEXTENSION_INITIALIZATION_EDITOR);
    init.deinitialize(init.userdata, GDEXTENSION_INITIALIZATION_CORE);
    CHECK(callbacks_seen == 11);
    CHECK(initialized_level == 3);
    CHECK(deinitialized_level == 0);

    /* Struct layout: fields assigned and read back. */
    host_get_godot_version(&version);
    CHECK(version.major == 4 && version.minor == 7 && version.patch == 0);
    CHECK(strcmp(version.string, "Godot Engine v4.7.stable") == 0);
    err.error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
    err.argument = 2;
    err.expected = 3;
    CHECK(err.error == 4 && err.argument == 2 && err.expected == 3);

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
