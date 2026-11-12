// Builds a small FMU in C++ against <fmi3Functions.h> with
// FMI3_OVERRIDE_FUNCTION_PREFIX and FMI3_ACTUAL_FUNCTION_PREFIX, which win
// over FMI3_FUNCTION_PREFIX, and drives it through <fmi3FunctionTypes.h>'s
// *TYPE pointer types: Int32 and String variables, co-simulation steps, and
// saving, restoring, and freeing an FMU state.
#define FMI3_FUNCTION_PREFIX Ignored_
#define FMI3_OVERRIDE_FUNCTION_PREFIX
#define FMI3_ACTUAL_FUNCTION_PREFIX Counter_
#include <fmi3Functions.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

namespace {

const fmi3ValueReference kCount = 0;
const fmi3ValueReference kIncrement = 1;
const fmi3ValueReference kLabel = 2;

struct Counter {
    fmi3Float64 time = 0.0;
    fmi3Int32 count = 0;
    fmi3Int32 increment = 1;
    std::string label = "counter";
};

Counter* self(fmi3Instance instance) {
    return static_cast<Counter*>(instance);
}

}  // namespace

// The FMU.  <fmi3Functions.h> declared these extern "C" as Counter_fmi3*.

const char* fmi3GetVersion() {
    return fmi3Version;
}

fmi3Instance fmi3InstantiateCoSimulation(fmi3String, fmi3String, fmi3String, fmi3Boolean,
                                         fmi3Boolean, fmi3Boolean, fmi3Boolean,
                                         const fmi3ValueReference[], size_t,
                                         fmi3InstanceEnvironment, fmi3LogMessageCallback,
                                         fmi3IntermediateUpdateCallback) {
    return new (std::nothrow) Counter;
}

void fmi3FreeInstance(fmi3Instance instance) {
    delete self(instance);
}

fmi3Status fmi3EnterInitializationMode(fmi3Instance instance, fmi3Boolean, fmi3Float64,
                                       fmi3Float64 startTime, fmi3Boolean, fmi3Float64) {
    self(instance)->time = startTime;
    return fmi3OK;
}

fmi3Status fmi3ExitInitializationMode(fmi3Instance) {
    return fmi3OK;
}

fmi3Status fmi3GetInt32(fmi3Instance instance, const fmi3ValueReference valueReferences[],
                        size_t nValueReferences, fmi3Int32 values[], size_t nValues) {
    if (nValues != nValueReferences)
        return fmi3Error;
    for (size_t i = 0; i < nValueReferences; ++i) {
        if (valueReferences[i] == kCount)
            values[i] = self(instance)->count;
        else if (valueReferences[i] == kIncrement)
            values[i] = self(instance)->increment;
        else
            return fmi3Error;
    }
    return fmi3OK;
}

fmi3Status fmi3SetInt32(fmi3Instance instance, const fmi3ValueReference valueReferences[],
                        size_t nValueReferences, const fmi3Int32 values[], size_t nValues) {
    if (nValues != nValueReferences)
        return fmi3Error;
    for (size_t i = 0; i < nValueReferences; ++i) {
        if (valueReferences[i] != kIncrement)
            return fmi3Error;
        self(instance)->increment = values[i];
    }
    return fmi3OK;
}

fmi3Status fmi3GetString(fmi3Instance instance, const fmi3ValueReference valueReferences[],
                         size_t nValueReferences, fmi3String values[], size_t nValues) {
    if (nValueReferences != 1 || nValues != 1 || valueReferences[0] != kLabel)
        return fmi3Error;
    values[0] = self(instance)->label.c_str();
    return fmi3OK;
}

fmi3Status fmi3SetString(fmi3Instance instance, const fmi3ValueReference valueReferences[],
                         size_t nValueReferences, const fmi3String values[], size_t nValues) {
    if (nValueReferences != 1 || nValues != 1 || valueReferences[0] != kLabel)
        return fmi3Error;
    self(instance)->label = values[0];
    return fmi3OK;
}

fmi3Status fmi3DoStep(fmi3Instance instance, fmi3Float64, fmi3Float64 communicationStepSize,
                      fmi3Boolean, fmi3Boolean* eventHandlingNeeded,
                      fmi3Boolean* terminateSimulation, fmi3Boolean* earlyReturn,
                      fmi3Float64* lastSuccessfulTime) {
    Counter* c = self(instance);
    c->count += c->increment;
    c->time += communicationStepSize;
    *eventHandlingNeeded = fmi3False;
    *terminateSimulation = fmi3False;
    *earlyReturn = fmi3False;
    *lastSuccessfulTime = c->time;
    return fmi3OK;
}

fmi3Status fmi3GetFMUState(fmi3Instance instance, fmi3FMUState* FMUState) {
    *FMUState = new (std::nothrow) Counter(*self(instance));
    return *FMUState != nullptr ? fmi3OK : fmi3Error;
}

fmi3Status fmi3SetFMUState(fmi3Instance instance, fmi3FMUState FMUState) {
    *self(instance) = *static_cast<Counter*>(FMUState);
    return fmi3OK;
}

fmi3Status fmi3FreeFMUState(fmi3Instance, fmi3FMUState* FMUState) {
    delete static_cast<Counter*>(*FMUState);
    *FMUState = nullptr;
    return fmi3OK;
}

fmi3Status fmi3Terminate(fmi3Instance) {
    return fmi3OK;
}

// The importer.

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "fmi3 C++ test failed: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    fmi3GetVersionTYPE* get_version = Counter_fmi3GetVersion;
    fmi3InstantiateCoSimulationTYPE* instantiate = Counter_fmi3InstantiateCoSimulation;
    fmi3EnterInitializationModeTYPE* enter_initialization = Counter_fmi3EnterInitializationMode;
    fmi3ExitInitializationModeTYPE* exit_initialization = Counter_fmi3ExitInitializationMode;
    fmi3GetInt32TYPE* get_int32 = Counter_fmi3GetInt32;
    fmi3SetInt32TYPE* set_int32 = Counter_fmi3SetInt32;
    fmi3GetStringTYPE* get_string = Counter_fmi3GetString;
    fmi3SetStringTYPE* set_string = Counter_fmi3SetString;
    fmi3DoStepTYPE* do_step = Counter_fmi3DoStep;
    fmi3GetFMUStateTYPE* get_state = Counter_fmi3GetFMUState;
    fmi3SetFMUStateTYPE* set_state = Counter_fmi3SetFMUState;
    fmi3FreeFMUStateTYPE* free_state = Counter_fmi3FreeFMUState;
    fmi3TerminateTYPE* terminate = Counter_fmi3Terminate;
    fmi3FreeInstanceTYPE* free_instance = Counter_fmi3FreeInstance;

    check(std::strcmp(get_version(), fmi3Version) == 0, "fmi3GetVersion");

    fmi3Instance fmu = instantiate("counter", "{counter}", nullptr, fmi3False, fmi3False,
                                   fmi3False, fmi3False, nullptr, 0, nullptr, nullptr, nullptr);
    if (fmu == nullptr) {
        std::fprintf(stderr, "fmi3 C++ test failed: instantiation\n");
        return 1;
    }
    check(enter_initialization(fmu, fmi3False, 0.0, 0.0, fmi3False, 0.0) == fmi3OK &&
              exit_initialization(fmu) == fmi3OK,
          "initialization");

    const fmi3ValueReference increment_vr[] = {kIncrement};
    const fmi3Int32 three[] = {3};
    const fmi3ValueReference label_vr[] = {kLabel};
    const fmi3String steps[] = {"steps"};
    const fmi3String changed[] = {"changed"};
    check(set_int32(fmu, increment_vr, 1, three, 1) == fmi3OK &&
              set_string(fmu, label_vr, 1, steps, 1) == fmi3OK,
          "setting Int32 and String inputs");

    fmi3Float64 time = 0.0;
    fmi3Boolean event = fmi3False, stop = fmi3False, early = fmi3False;
    auto step = [&]() {
        check(do_step(fmu, time, 0.5, fmi3False, &event, &stop, &early, &time) == fmi3OK,
              "DoStep");
    };
    step();
    step();

    fmi3FMUState saved = nullptr;
    check(get_state(fmu, &saved) == fmi3OK && saved != nullptr, "fmi3GetFMUState");
    step();
    step();
    check(set_string(fmu, label_vr, 1, changed, 1) == fmi3OK, "changing the label");

    const fmi3ValueReference int_vrs[] = {kCount, kIncrement};
    fmi3Int32 ints[2] = {0, 0};
    check(get_int32(fmu, int_vrs, 2, ints, 2) == fmi3OK && ints[0] == 12 && ints[1] == 3,
          "four steps of 3");

    check(set_state(fmu, saved) == fmi3OK, "fmi3SetFMUState");
    fmi3String label[] = {nullptr};
    check(get_int32(fmu, int_vrs, 2, ints, 2) == fmi3OK && ints[0] == 6 &&
              get_string(fmu, label_vr, 1, label, 1) == fmi3OK &&
              std::string(label[0]) == "steps",
          "restoring the state after two steps");
    check(free_state(fmu, &saved) == fmi3OK && saved == nullptr, "fmi3FreeFMUState");

    const fmi3ValueReference count_vr[] = {kCount};
    check(set_int32(fmu, count_vr, 1, three, 1) == fmi3Error, "count is not settable");

    check(terminate(fmu) == fmi3OK, "fmi3Terminate");
    free_instance(fmu);

    if (failures != 0)
        return 1;
    std::printf("fmi3 %s C++ ok\n", fmi3Version);
    return 0;
}
