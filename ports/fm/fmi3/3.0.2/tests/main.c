/* Builds a small co-simulation FMU against <fmi3Functions.h> with
   FMI3_FUNCTION_PREFIX, the convention for FMUs linked statically, and
   drives it as an importer does: through pointers of <fmi3FunctionTypes.h>'s
   *TYPE function types, bound to the prefixed names.  The FMU integrates a
   constant rate, x' = rate, in steps that are exact in binary floating
   point. */
#define FMI3_FUNCTION_PREFIX Ramp_
#include <fmi3Functions.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAMP_TOKEN "{6f1d2c3a-ramp}"

enum { VR_TIME = 0, VR_X = 1, VR_RATE = 2 };

typedef struct {
    fmi3Float64 time;
    fmi3Float64 x;
    fmi3Float64 rate;
    fmi3InstanceEnvironment env;
    fmi3LogMessageCallback log;
} Ramp;

/* The FMU.  The macros of <fmi3Functions.h> turn each fmi3* name below
   into Ramp_fmi3*. */

const char* fmi3GetVersion(void) {
    return fmi3Version;
}

fmi3Instance fmi3InstantiateCoSimulation(fmi3String instanceName,
                                         fmi3String instantiationToken,
                                         fmi3String resourcePath,
                                         fmi3Boolean visible,
                                         fmi3Boolean loggingOn,
                                         fmi3Boolean eventModeUsed,
                                         fmi3Boolean earlyReturnAllowed,
                                         const fmi3ValueReference requiredIntermediateVariables[],
                                         size_t nRequiredIntermediateVariables,
                                         fmi3InstanceEnvironment instanceEnvironment,
                                         fmi3LogMessageCallback logMessage,
                                         fmi3IntermediateUpdateCallback intermediateUpdate) {
    Ramp* r;
    (void)instanceName;
    (void)resourcePath;
    (void)visible;
    (void)loggingOn;
    (void)eventModeUsed;
    (void)earlyReturnAllowed;
    (void)requiredIntermediateVariables;
    (void)nRequiredIntermediateVariables;
    (void)intermediateUpdate;
    if (strcmp(instantiationToken, RAMP_TOKEN) != 0) {
        logMessage(instanceEnvironment, fmi3Error, "logStatusError", "wrong instantiation token");
        return NULL;
    }
    r = (Ramp*)calloc(1, sizeof *r);
    if (r == NULL)
        return NULL;
    r->rate = 1.0;
    r->env = instanceEnvironment;
    r->log = logMessage;
    return r;
}

void fmi3FreeInstance(fmi3Instance instance) {
    free(instance);
}

fmi3Status fmi3EnterInitializationMode(fmi3Instance instance,
                                       fmi3Boolean toleranceDefined,
                                       fmi3Float64 tolerance,
                                       fmi3Float64 startTime,
                                       fmi3Boolean stopTimeDefined,
                                       fmi3Float64 stopTime) {
    (void)toleranceDefined;
    (void)tolerance;
    (void)stopTimeDefined;
    (void)stopTime;
    ((Ramp*)instance)->time = startTime;
    return fmi3OK;
}

fmi3Status fmi3ExitInitializationMode(fmi3Instance instance) {
    (void)instance;
    return fmi3OK;
}

static fmi3Status reject(Ramp* r, const char* message) {
    r->log(r->env, fmi3Error, "logStatusError", message);
    return fmi3Error;
}

fmi3Status fmi3GetFloat64(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          fmi3Float64 values[],
                          size_t nValues) {
    Ramp* r = (Ramp*)instance;
    size_t i;
    if (nValues != nValueReferences)
        return reject(r, "one value per reference");
    for (i = 0; i < nValueReferences; ++i) {
        switch (valueReferences[i]) {
        case VR_TIME: values[i] = r->time; break;
        case VR_X: values[i] = r->x; break;
        case VR_RATE: values[i] = r->rate; break;
        default: return reject(r, "unknown value reference");
        }
    }
    return fmi3OK;
}

fmi3Status fmi3SetFloat64(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          const fmi3Float64 values[],
                          size_t nValues) {
    Ramp* r = (Ramp*)instance;
    size_t i;
    if (nValues != nValueReferences)
        return reject(r, "one value per reference");
    for (i = 0; i < nValueReferences; ++i) {
        switch (valueReferences[i]) {
        case VR_X: r->x = values[i]; break;
        case VR_RATE: r->rate = values[i]; break;
        default: return reject(r, "not a settable value reference");
        }
    }
    return fmi3OK;
}

fmi3Status fmi3DoStep(fmi3Instance instance,
                      fmi3Float64 currentCommunicationPoint,
                      fmi3Float64 communicationStepSize,
                      fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                      fmi3Boolean* eventHandlingNeeded,
                      fmi3Boolean* terminateSimulation,
                      fmi3Boolean* earlyReturn,
                      fmi3Float64* lastSuccessfulTime) {
    Ramp* r = (Ramp*)instance;
    (void)noSetFMUStatePriorToCurrentPoint;
    if (currentCommunicationPoint != r->time)
        return reject(r, "communication point is not the FMU's time");
    r->x += r->rate * communicationStepSize;
    r->time += communicationStepSize;
    *eventHandlingNeeded = fmi3False;
    *terminateSimulation = fmi3False;
    *earlyReturn = fmi3False;
    *lastSuccessfulTime = r->time;
    return fmi3OK;
}

fmi3Status fmi3Terminate(fmi3Instance instance) {
    (void)instance;
    return fmi3OK;
}

/* The importer. */

typedef struct {
    int messages;
    fmi3Status status;
    char category[32];
} Log;

static void on_log(fmi3InstanceEnvironment env,
                   fmi3Status status,
                   fmi3String category,
                   fmi3String message) {
    Log* journal = (Log*)env;
    (void)message;
    ++journal->messages;
    journal->status = status;
    snprintf(journal->category, sizeof journal->category, "%s", category);
}

static int failures = 0;

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "fmi3 C test failed: %s\n", what);
        ++failures;
    }
}

int main(void) {
    fmi3GetVersionTYPE* get_version = Ramp_fmi3GetVersion;
    fmi3InstantiateCoSimulationTYPE* instantiate = Ramp_fmi3InstantiateCoSimulation;
    fmi3EnterInitializationModeTYPE* enter_initialization = Ramp_fmi3EnterInitializationMode;
    fmi3ExitInitializationModeTYPE* exit_initialization = Ramp_fmi3ExitInitializationMode;
    fmi3SetFloat64TYPE* set_float64 = Ramp_fmi3SetFloat64;
    fmi3GetFloat64TYPE* get_float64 = Ramp_fmi3GetFloat64;
    fmi3DoStepTYPE* do_step = Ramp_fmi3DoStep;
    fmi3TerminateTYPE* terminate = Ramp_fmi3Terminate;
    fmi3FreeInstanceTYPE* free_instance = Ramp_fmi3FreeInstance;

    const fmi3ValueReference inputs[] = {VR_X, VR_RATE};
    const fmi3Float64 start[] = {2.0, 0.5};
    const fmi3ValueReference outputs[] = {VR_TIME, VR_X};
    const fmi3ValueReference unknown[] = {7};
    const fmi3ValueReference time_only[] = {VR_TIME};
    fmi3Float64 values[2] = {0.0, 0.0};
    fmi3Float64 time = 1.0;
    fmi3Boolean event = fmi3True, stop = fmi3True, early = fmi3True;
    fmi3Float64 last = 0.0;
    Log journal = {0, fmi3OK, ""};
    fmi3Instance fmu;
    int i;

    check(sizeof(fmi3Float32) == 4 && sizeof(fmi3Float64) == 8 && sizeof(fmi3Int64) == 8 &&
              sizeof(fmi3UInt8) == 1 && sizeof(fmi3ValueReference) == 4,
          "platform type sizes");
    check(fmi3True == 1 && fmi3False == 0 && fmi3ClockActive && !fmi3ClockInactive,
          "boolean and clock values");
    check(fmi3OK == 0 && fmi3Warning == 1 && fmi3Discard == 2 && fmi3Error == 3 && fmi3Fatal == 4,
          "fmi3Status values");
    check(strcmp(get_version(), "3.0") == 0, "fmi3GetVersion returns fmi3Version");

    check(instantiate("ramp", "{wrong}", NULL, fmi3False, fmi3False, fmi3False, fmi3False, NULL,
                      0, &journal, on_log, NULL) == NULL &&
              journal.messages == 1 && journal.status == fmi3Error &&
              strcmp(journal.category, "logStatusError") == 0,
          "a wrong token is refused through the log callback");

    fmu = instantiate("ramp", RAMP_TOKEN, NULL, fmi3False, fmi3False, fmi3False, fmi3False, NULL,
                      0, &journal, on_log, NULL);
    if (fmu == NULL) {
        fprintf(stderr, "fmi3 C test failed: instantiation\n");
        return 1;
    }
    check(enter_initialization(fmu, fmi3False, 0.0, time, fmi3False, 0.0) == fmi3OK &&
              set_float64(fmu, inputs, 2, start, 2) == fmi3OK &&
              exit_initialization(fmu) == fmi3OK,
          "initialization");

    for (i = 0; i < 4; ++i) {
        check(do_step(fmu, time, 0.25, fmi3True, &event, &stop, &early, &last) == fmi3OK &&
                  !event && !stop && !early,
              "DoStep");
        time = last;
    }
    check(get_float64(fmu, outputs, 2, values, 2) == fmi3OK && values[0] == 2.0 &&
              values[1] == 2.5,
          "four steps of 0.25 at rate 0.5 from x = 2 at t = 1");

    journal.messages = 0;
    check(do_step(fmu, 0.0, 0.25, fmi3True, &event, &stop, &early, &last) == fmi3Error,
          "DoStep from a stale communication point");
    check(get_float64(fmu, unknown, 1, values, 1) == fmi3Error, "unknown value reference");
    check(set_float64(fmu, time_only, 1, start, 1) == fmi3Error, "time is not settable");
    check(journal.messages == 3 && journal.status == fmi3Error, "each error is logged");

    check(terminate(fmu) == fmi3OK, "fmi3Terminate");
    free_instance(fmu);

    if (failures != 0)
        return 1;
    printf("fmi3 %s C ok\n", fmi3Version);
    return 0;
}
