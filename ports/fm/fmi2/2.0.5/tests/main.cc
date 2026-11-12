// Builds a small FMU in C++ against <fmi2Functions.h> with
// FMI2_OVERRIDE_FUNCTION_PREFIX and FMI2_ACTUAL_FUNCTION_PREFIX, which win
// over FMI2_FUNCTION_PREFIX, and drives it through <fmi2FunctionTypes.h>'s
// *TYPE pointer types: Integer, Boolean and String variables, co-simulation
// steps, and an FMU state saved, serialized, restored, and freed.  C++98
// (C++14 with MSVC), as the probe compiles it.
#define FMI2_FUNCTION_PREFIX Ignored_
#define FMI2_OVERRIDE_FUNCTION_PREFIX
#define FMI2_ACTUAL_FUNCTION_PREFIX Counter_
#include <fmi2Functions.h>

#define NAME(name) NAME_STRING(name)
#define NAME_STRING(name) #name

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

const fmi2ValueReference kCount = 0;
const fmi2ValueReference kIncrement = 1;
const fmi2ValueReference kPaused = 2;
const fmi2ValueReference kLabel = 3;

struct Counter {
    Counter() : time(0.0), count(0), increment(1), paused(fmi2False), label("counter") {}
    fmi2Real time;
    fmi2Integer count;
    fmi2Integer increment;
    fmi2Boolean paused;
    std::string label;
};

Counter* self(void* p) {
    return static_cast<Counter*>(p);
}

// A serialized state: time, count, increment, and paused, then the label's
// characters.
const size_t kFixedSize = sizeof(fmi2Real) + 2 * sizeof(fmi2Integer) + sizeof(fmi2Boolean);

void put(fmi2Byte*& out, const void* value, size_t size) {
    std::memcpy(out, value, size);
    out += size;
}

void take(const fmi2Byte*& in, void* value, size_t size) {
    std::memcpy(value, in, size);
    in += size;
}

}  // namespace

// The FMU.  <fmi2Functions.h> declared these extern "C" as Counter_fmi2*.

const char* fmi2GetTypesPlatform() {
    return fmi2TypesPlatform;
}

const char* fmi2GetVersion() {
    return fmi2Version;
}

fmi2Component fmi2Instantiate(fmi2String, fmi2Type fmuType, fmi2String, fmi2String,
                              const fmi2CallbackFunctions*, fmi2Boolean, fmi2Boolean) {
    if (fmuType != fmi2CoSimulation)
        return NULL;
    return new (std::nothrow) Counter;
}

void fmi2FreeInstance(fmi2Component c) {
    delete self(c);
}

fmi2Status fmi2SetupExperiment(fmi2Component c, fmi2Boolean, fmi2Real, fmi2Real startTime,
                               fmi2Boolean, fmi2Real) {
    self(c)->time = startTime;
    return fmi2OK;
}

fmi2Status fmi2EnterInitializationMode(fmi2Component) {
    return fmi2OK;
}

fmi2Status fmi2ExitInitializationMode(fmi2Component) {
    return fmi2OK;
}

fmi2Status fmi2GetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                          fmi2Integer value[]) {
    for (size_t i = 0; i < nvr; ++i) {
        if (vr[i] == kCount)
            value[i] = self(c)->count;
        else if (vr[i] == kIncrement)
            value[i] = self(c)->increment;
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status fmi2SetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                          const fmi2Integer value[]) {
    for (size_t i = 0; i < nvr; ++i) {
        if (vr[i] != kIncrement)
            return fmi2Error;
        self(c)->increment = value[i];
    }
    return fmi2OK;
}

fmi2Status fmi2GetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                          fmi2Boolean value[]) {
    if (nvr != 1 || vr[0] != kPaused)
        return fmi2Error;
    value[0] = self(c)->paused;
    return fmi2OK;
}

fmi2Status fmi2SetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                          const fmi2Boolean value[]) {
    if (nvr != 1 || vr[0] != kPaused)
        return fmi2Error;
    self(c)->paused = value[0];
    return fmi2OK;
}

fmi2Status fmi2GetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                         fmi2String value[]) {
    if (nvr != 1 || vr[0] != kLabel)
        return fmi2Error;
    value[0] = self(c)->label.c_str();
    return fmi2OK;
}

fmi2Status fmi2SetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                         const fmi2String value[]) {
    if (nvr != 1 || vr[0] != kLabel)
        return fmi2Error;
    self(c)->label = value[0];
    return fmi2OK;
}

fmi2Status fmi2DoStep(fmi2Component c, fmi2Real currentCommunicationPoint,
                      fmi2Real communicationStepSize, fmi2Boolean) {
    Counter* counter = self(c);
    if (currentCommunicationPoint != counter->time)
        return fmi2Error;
    if (!counter->paused)
        counter->count += counter->increment;
    counter->time += communicationStepSize;
    return fmi2OK;
}

fmi2Status fmi2GetFMUstate(fmi2Component c, fmi2FMUstate* FMUstate) {
    *FMUstate = new (std::nothrow) Counter(*self(c));
    return *FMUstate != NULL ? fmi2OK : fmi2Error;
}

fmi2Status fmi2SetFMUstate(fmi2Component c, fmi2FMUstate FMUstate) {
    *self(c) = *self(FMUstate);
    return fmi2OK;
}

fmi2Status fmi2FreeFMUstate(fmi2Component, fmi2FMUstate* FMUstate) {
    delete self(*FMUstate);
    *FMUstate = NULL;
    return fmi2OK;
}

fmi2Status fmi2SerializedFMUstateSize(fmi2Component, fmi2FMUstate FMUstate, size_t* size) {
    *size = kFixedSize + self(FMUstate)->label.size();
    return fmi2OK;
}

fmi2Status fmi2SerializeFMUstate(fmi2Component, fmi2FMUstate FMUstate,
                                 fmi2Byte serializedState[], size_t size) {
    const Counter* state = self(FMUstate);
    if (size != kFixedSize + state->label.size())
        return fmi2Error;
    fmi2Byte* out = serializedState;
    put(out, &state->time, sizeof state->time);
    put(out, &state->count, sizeof state->count);
    put(out, &state->increment, sizeof state->increment);
    put(out, &state->paused, sizeof state->paused);
    put(out, state->label.data(), state->label.size());
    return fmi2OK;
}

fmi2Status fmi2DeSerializeFMUstate(fmi2Component, const fmi2Byte serializedState[], size_t size,
                                   fmi2FMUstate* FMUstate) {
    if (size < kFixedSize)
        return fmi2Error;
    Counter* state = new (std::nothrow) Counter;
    if (state == NULL)
        return fmi2Error;
    const fmi2Byte* in = serializedState;
    take(in, &state->time, sizeof state->time);
    take(in, &state->count, sizeof state->count);
    take(in, &state->increment, sizeof state->increment);
    take(in, &state->paused, sizeof state->paused);
    state->label.assign(in, size - kFixedSize);
    *FMUstate = state;
    return fmi2OK;
}

fmi2Status fmi2Terminate(fmi2Component) {
    return fmi2OK;
}

// The importer.

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "fmi2 C++ test failed: %s\n", what);
        ++failures;
    }
}

void ignore_log(fmi2ComponentEnvironment, fmi2String, fmi2Status, fmi2String, fmi2String, ...) {}

fmi2DoStepTYPE* do_step = Counter_fmi2DoStep;
fmi2Real now = 0.0;

void step(fmi2Component fmu) {
    check(do_step(fmu, now, 0.5, fmi2False) == fmi2OK, "DoStep");
    now += 0.5;
}

}  // namespace

int main() {
    fmi2GetTypesPlatformTYPE* get_types_platform = Counter_fmi2GetTypesPlatform;
    fmi2GetVersionTYPE* get_version = Counter_fmi2GetVersion;
    fmi2InstantiateTYPE* instantiate = Counter_fmi2Instantiate;
    fmi2SetupExperimentTYPE* setup_experiment = Counter_fmi2SetupExperiment;
    fmi2EnterInitializationModeTYPE* enter_initialization = Counter_fmi2EnterInitializationMode;
    fmi2ExitInitializationModeTYPE* exit_initialization = Counter_fmi2ExitInitializationMode;
    fmi2GetIntegerTYPE* get_integer = Counter_fmi2GetInteger;
    fmi2SetIntegerTYPE* set_integer = Counter_fmi2SetInteger;
    fmi2GetBooleanTYPE* get_boolean = Counter_fmi2GetBoolean;
    fmi2SetBooleanTYPE* set_boolean = Counter_fmi2SetBoolean;
    fmi2GetStringTYPE* get_string = Counter_fmi2GetString;
    fmi2SetStringTYPE* set_string = Counter_fmi2SetString;
    fmi2GetFMUstateTYPE* get_state = Counter_fmi2GetFMUstate;
    fmi2SetFMUstateTYPE* set_state = Counter_fmi2SetFMUstate;
    fmi2FreeFMUstateTYPE* free_state = Counter_fmi2FreeFMUstate;
    fmi2SerializedFMUstateSizeTYPE* serialized_size = Counter_fmi2SerializedFMUstateSize;
    fmi2SerializeFMUstateTYPE* serialize = Counter_fmi2SerializeFMUstate;
    fmi2DeSerializeFMUstateTYPE* deserialize = Counter_fmi2DeSerializeFMUstate;
    fmi2TerminateTYPE* terminate = Counter_fmi2Terminate;
    fmi2FreeInstanceTYPE* free_instance = Counter_fmi2FreeInstance;

    check(std::strcmp(NAME(fmi2DoStep), "Counter_fmi2DoStep") == 0,
          "FMI2_ACTUAL_FUNCTION_PREFIX wins over FMI2_FUNCTION_PREFIX");
    check(std::strcmp(get_version(), "2.0") == 0 &&
              std::strcmp(get_types_platform(), "default") == 0,
          "fmi2GetVersion and fmi2GetTypesPlatform");

    // The FMU allocates with new, as one declaring
    // canNotUseMemoryManagementFunctions does, and logs nothing.
    fmi2CallbackFunctions callbacks = {ignore_log, NULL, NULL, NULL, NULL};
    check(instantiate("counter", fmi2ModelExchange, "{counter}", "", &callbacks, fmi2False,
                      fmi2False) == NULL,
          "model exchange is refused");
    fmi2Component fmu = instantiate("counter", fmi2CoSimulation, "{counter}", "", &callbacks,
                                    fmi2False, fmi2False);
    if (fmu == NULL) {
        std::fprintf(stderr, "fmi2 C++ test failed: instantiation\n");
        return 1;
    }
    check(setup_experiment(fmu, fmi2False, 0.0, now, fmi2False, 0.0) == fmi2OK &&
              enter_initialization(fmu) == fmi2OK && exit_initialization(fmu) == fmi2OK,
          "initialization");

    const fmi2ValueReference increment_vr[] = {kIncrement};
    const fmi2Integer three[] = {3};
    const fmi2ValueReference paused_vr[] = {kPaused};
    const fmi2Boolean yes[] = {fmi2True};
    const fmi2Boolean no[] = {fmi2False};
    const fmi2ValueReference label_vr[] = {kLabel};
    const fmi2String steps[] = {"steps"};
    const fmi2String changed[] = {"changed"};
    check(set_integer(fmu, increment_vr, 1, three) == fmi2OK &&
              set_string(fmu, label_vr, 1, steps) == fmi2OK,
          "setting Integer and String inputs");
    step(fmu);
    step(fmu);

    // The importer's clock goes back with the state.
    const fmi2Real saved_time = now;
    fmi2FMUstate saved = NULL;
    check(get_state(fmu, &saved) == fmi2OK && saved != NULL, "fmi2GetFMUstate");
    size_t size = 0;
    check(serialized_size(fmu, saved, &size) == fmi2OK && size == kFixedSize + 5,
          "fmi2SerializedFMUstateSize");
    std::vector<fmi2Byte> bytes(size);
    check(serialize(fmu, saved, &bytes[0], size) == fmi2OK, "fmi2SerializeFMUstate");
    check(serialize(fmu, saved, &bytes[0], size - 1) == fmi2Error,
          "serializing into too small a buffer");

    // A paused step leaves the count alone.
    check(set_boolean(fmu, paused_vr, 1, yes) == fmi2OK, "pausing");
    step(fmu);
    check(set_boolean(fmu, paused_vr, 1, no) == fmi2OK, "resuming");
    step(fmu);
    step(fmu);
    check(set_string(fmu, label_vr, 1, changed) == fmi2OK, "changing the label");

    const fmi2ValueReference int_vrs[] = {kCount, kIncrement};
    fmi2Integer ints[2] = {0, 0};
    fmi2Boolean paused[1] = {fmi2True};
    check(get_integer(fmu, int_vrs, 2, ints) == fmi2OK && ints[0] == 12 && ints[1] == 3 &&
              get_boolean(fmu, paused_vr, 1, paused) == fmi2OK && paused[0] == fmi2False,
          "four counted steps of 3 and a paused one");

    fmi2String label[1] = {NULL};
    check(set_state(fmu, saved) == fmi2OK && get_integer(fmu, int_vrs, 2, ints) == fmi2OK &&
              ints[0] == 6 && get_string(fmu, label_vr, 1, label) == fmi2OK &&
              std::string(label[0]) == "steps",
          "restoring the state after two steps");
    check(free_state(fmu, &saved) == fmi2OK && saved == NULL, "fmi2FreeFMUstate");
    check(do_step(fmu, now, 0.5, fmi2False) == fmi2Error,
          "the restored state has the time it was saved at");
    now = saved_time;

    step(fmu);
    fmi2FMUstate restored = NULL;
    check(deserialize(fmu, &bytes[0], bytes.size(), &restored) == fmi2OK && restored != NULL &&
              set_state(fmu, restored) == fmi2OK &&
              get_integer(fmu, int_vrs, 2, ints) == fmi2OK && ints[0] == 6 && ints[1] == 3 &&
              get_string(fmu, label_vr, 1, label) == fmi2OK && std::string(label[0]) == "steps",
          "restoring a deserialized state");
    check(free_state(fmu, &restored) == fmi2OK && restored == NULL, "freeing it");

    const fmi2ValueReference count_vr[] = {kCount};
    check(set_integer(fmu, count_vr, 1, three) == fmi2Error, "count is not settable");

    check(terminate(fmu) == fmi2OK, "fmi2Terminate");
    free_instance(fmu);

    if (failures != 0)
        return 1;
    std::printf("fmi2 %s C++ ok\n", fmi2Version);
    return 0;
}
