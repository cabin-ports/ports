/* Builds a small co-simulation FMU against <fmi2Functions.h> with
   FMI2_FUNCTION_PREFIX, the convention for FMUs linked statically, and
   drives it as an importer does: through pointers of <fmi2FunctionTypes.h>'s
   *TYPE function types, bound to the prefixed names.  The FMU allocates its
   instance and logs its errors through the importer's fmi2CallbackFunctions,
   and integrates a constant inflow, level' = inflow, in steps that are exact
   in binary floating point.  C89 (C11 with MSVC), as the probe compiles
   it. */
#define FMI2_FUNCTION_PREFIX Tank_
#include <fmi2Functions.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TANK_GUID "{2f0c9a51-tank}"

enum { VR_TIME = 0, VR_LEVEL = 1, VR_INFLOW = 2 };

typedef struct {
    fmi2CallbackFunctions functions;
    char name[32];
    fmi2Real time;
    fmi2Real level;
    fmi2Real inflow;
} Tank;

/* The FMU.  The macros of <fmi2Functions.h> turn each fmi2* name below
   into Tank_fmi2*. */

const char* fmi2GetTypesPlatform(void) {
    return fmi2TypesPlatform;
}

const char* fmi2GetVersion(void) {
    return fmi2Version;
}

fmi2Component fmi2Instantiate(fmi2String instanceName,
                              fmi2Type fmuType,
                              fmi2String fmuGUID,
                              fmi2String fmuResourceLocation,
                              const fmi2CallbackFunctions* functions,
                              fmi2Boolean visible,
                              fmi2Boolean loggingOn) {
    Tank* t;
    (void)fmuResourceLocation;
    (void)visible;
    (void)loggingOn;
    if (fmuType != fmi2CoSimulation) {
        functions->logger(functions->componentEnvironment, instanceName, fmi2Error,
                          "logStatusError", "only co-simulation is supported");
        return NULL;
    }
    if (strcmp(fmuGUID, TANK_GUID) != 0) {
        functions->logger(functions->componentEnvironment, instanceName, fmi2Error,
                          "logStatusError", "wrong GUID");
        return NULL;
    }
    /* allocateMemory has calloc's contract, so the instance starts zeroed. */
    t = (Tank*)functions->allocateMemory(1, sizeof *t);
    if (t == NULL)
        return NULL;
    t->functions = *functions;
    strncpy(t->name, instanceName, sizeof t->name - 1);
    return t;
}

void fmi2FreeInstance(fmi2Component c) {
    Tank* t = (Tank*)c;
    t->functions.freeMemory(t);
}

fmi2Status fmi2SetupExperiment(fmi2Component c,
                               fmi2Boolean toleranceDefined,
                               fmi2Real tolerance,
                               fmi2Real startTime,
                               fmi2Boolean stopTimeDefined,
                               fmi2Real stopTime) {
    (void)toleranceDefined;
    (void)tolerance;
    (void)stopTimeDefined;
    (void)stopTime;
    ((Tank*)c)->time = startTime;
    return fmi2OK;
}

fmi2Status fmi2EnterInitializationMode(fmi2Component c) {
    (void)c;
    return fmi2OK;
}

fmi2Status fmi2ExitInitializationMode(fmi2Component c) {
    (void)c;
    return fmi2OK;
}

static fmi2Status reject(Tank* t, fmi2ValueReference vr) {
    t->functions.logger(t->functions.componentEnvironment, t->name, fmi2Error,
                        "logStatusError", "value reference %u not accessible", vr);
    return fmi2Error;
}

fmi2Status fmi2GetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                       fmi2Real value[]) {
    Tank* t = (Tank*)c;
    size_t i;
    for (i = 0; i < nvr; ++i) {
        switch (vr[i]) {
        case VR_TIME: value[i] = t->time; break;
        case VR_LEVEL: value[i] = t->level; break;
        case VR_INFLOW: value[i] = t->inflow; break;
        default: return reject(t, vr[i]);
        }
    }
    return fmi2OK;
}

fmi2Status fmi2SetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr,
                       const fmi2Real value[]) {
    Tank* t = (Tank*)c;
    size_t i;
    for (i = 0; i < nvr; ++i) {
        switch (vr[i]) {
        case VR_LEVEL: t->level = value[i]; break;
        case VR_INFLOW: t->inflow = value[i]; break;
        default: return reject(t, vr[i]);
        }
    }
    return fmi2OK;
}

fmi2Status fmi2DoStep(fmi2Component c,
                      fmi2Real currentCommunicationPoint,
                      fmi2Real communicationStepSize,
                      fmi2Boolean noSetFMUStatePriorToCurrentPoint) {
    Tank* t = (Tank*)c;
    (void)noSetFMUStatePriorToCurrentPoint;
    if (currentCommunicationPoint != t->time) {
        t->functions.logger(t->functions.componentEnvironment, t->name, fmi2Error,
                            "logStatusError", "communication point is not the FMU's time");
        return fmi2Error;
    }
    t->level += t->inflow * communicationStepSize;
    t->time += communicationStepSize;
    return fmi2OK;
}

/* Only fmi2LastSuccessfulTime is a Real status; the other kinds are
   discarded, as the standard has an FMU do for a kind it does not provide. */
fmi2Status fmi2GetRealStatus(fmi2Component c, const fmi2StatusKind s, fmi2Real* value) {
    if (s != fmi2LastSuccessfulTime)
        return fmi2Discard;
    *value = ((Tank*)c)->time;
    return fmi2OK;
}

fmi2Status fmi2Terminate(fmi2Component c) {
    (void)c;
    return fmi2OK;
}

/* The importer. */

typedef struct {
    int messages;
    fmi2Status status;
    char instance[32];
    char category[32];
    unsigned int argument;
} Journal;

/* Records each message; a "%u" in it carries an unsigned argument. */
static void on_log(fmi2ComponentEnvironment componentEnvironment,
                   fmi2String instanceName,
                   fmi2Status status,
                   fmi2String category,
                   fmi2String message,
                   ...) {
    Journal* journal = (Journal*)componentEnvironment;
    ++journal->messages;
    journal->status = status;
    strncpy(journal->instance, instanceName, sizeof journal->instance - 1);
    strncpy(journal->category, category, sizeof journal->category - 1);
    if (strstr(message, "%u") != NULL) {
        va_list args;
        va_start(args, message);
        journal->argument = va_arg(args, unsigned int);
        va_end(args);
    }
}

static int live_allocations = 0;

static void* allocate(size_t nobj, size_t size) {
    void* p = calloc(nobj, size);
    if (p != NULL)
        ++live_allocations;
    return p;
}

static void release(void* obj) {
    if (obj != NULL)
        --live_allocations;
    free(obj);
}

static int failures = 0;

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "fmi2 C test failed: %s\n", what);
        ++failures;
    }
}

int main(void) {
    fmi2GetTypesPlatformTYPE* get_types_platform = Tank_fmi2GetTypesPlatform;
    fmi2GetVersionTYPE* get_version = Tank_fmi2GetVersion;
    fmi2InstantiateTYPE* instantiate = Tank_fmi2Instantiate;
    fmi2SetupExperimentTYPE* setup_experiment = Tank_fmi2SetupExperiment;
    fmi2EnterInitializationModeTYPE* enter_initialization = Tank_fmi2EnterInitializationMode;
    fmi2ExitInitializationModeTYPE* exit_initialization = Tank_fmi2ExitInitializationMode;
    fmi2SetRealTYPE* set_real = Tank_fmi2SetReal;
    fmi2GetRealTYPE* get_real = Tank_fmi2GetReal;
    fmi2DoStepTYPE* do_step = Tank_fmi2DoStep;
    fmi2GetRealStatusTYPE* get_real_status = Tank_fmi2GetRealStatus;
    fmi2TerminateTYPE* terminate = Tank_fmi2Terminate;
    fmi2FreeInstanceTYPE* free_instance = Tank_fmi2FreeInstance;

    static const fmi2ValueReference inputs[] = {VR_LEVEL, VR_INFLOW};
    static const fmi2Real start[] = {2.0, 0.5};
    static const fmi2ValueReference outputs[] = {VR_TIME, VR_LEVEL};
    static const fmi2ValueReference unknown[] = {7};
    static const fmi2ValueReference time_only[] = {VR_TIME};
    fmi2Real values[2];
    fmi2Real time = 1.0;
    fmi2Real last = 0.0;
    Journal journal;
    fmi2CallbackFunctions callbacks;
    fmi2Component fmu;
    int i;

    memset(&journal, 0, sizeof journal);
    callbacks.logger = on_log;
    callbacks.allocateMemory = allocate;
    callbacks.freeMemory = release;
    callbacks.stepFinished = NULL;
    callbacks.componentEnvironment = &journal;

    check(fmi2True == 1 && fmi2False == 0, "fmi2Boolean values");
    check(fmi2OK == 0 && fmi2Warning == 1 && fmi2Discard == 2 && fmi2Error == 3 &&
              fmi2Fatal == 4 && fmi2Pending == 5,
          "fmi2Status values");
    check(strcmp(get_types_platform(), "default") == 0, "fmi2GetTypesPlatform");
    check(strcmp(get_version(), "2.0") == 0, "fmi2GetVersion returns fmi2Version");

    check(instantiate("tank", fmi2ModelExchange, TANK_GUID, "", &callbacks, fmi2False,
                      fmi2False) == NULL &&
              journal.messages == 1 && journal.status == fmi2Error &&
              strcmp(journal.instance, "tank") == 0 &&
              strcmp(journal.category, "logStatusError") == 0,
          "model exchange is refused through the logger");
    check(instantiate("tank", fmi2CoSimulation, "{wrong}", "", &callbacks, fmi2False,
                      fmi2False) == NULL &&
              journal.messages == 2 && live_allocations == 0,
          "a wrong GUID is refused");

    fmu = instantiate("tank", fmi2CoSimulation, TANK_GUID, "", &callbacks, fmi2False,
                      fmi2False);
    if (fmu == NULL) {
        fprintf(stderr, "fmi2 C test failed: instantiation\n");
        return 1;
    }
    check(live_allocations == 1, "the instance comes from allocateMemory");
    check(setup_experiment(fmu, fmi2False, 0.0, time, fmi2False, 0.0) == fmi2OK &&
              enter_initialization(fmu) == fmi2OK &&
              set_real(fmu, inputs, 2, start) == fmi2OK &&
              exit_initialization(fmu) == fmi2OK,
          "initialization");

    for (i = 0; i < 4; ++i) {
        check(do_step(fmu, time, 0.25, fmi2True) == fmi2OK, "DoStep");
        time += 0.25;
    }
    check(get_real(fmu, outputs, 2, values) == fmi2OK && values[0] == 2.0 &&
              values[1] == 2.5,
          "four steps of 0.25 at inflow 0.5 from level 2 at t = 1");
    check(get_real_status(fmu, fmi2LastSuccessfulTime, &last) == fmi2OK && last == 2.0,
          "fmi2GetRealStatus(fmi2LastSuccessfulTime)");
    check(get_real_status(fmu, fmi2DoStepStatus, &last) == fmi2Discard,
          "fmi2GetRealStatus of another kind");

    journal.messages = 0;
    check(do_step(fmu, 0.0, 0.25, fmi2True) == fmi2Error,
          "DoStep from a stale communication point");
    check(get_real(fmu, unknown, 1, values) == fmi2Error && journal.argument == 7,
          "an unknown value reference, logged with its number");
    check(set_real(fmu, time_only, 1, start) == fmi2Error && journal.argument == VR_TIME,
          "time is not settable");
    check(journal.messages == 3 && journal.status == fmi2Error, "each error is logged");

    check(terminate(fmu) == fmi2OK, "fmi2Terminate");
    free_instance(fmu);
    check(live_allocations == 0, "the instance goes back through freeMemory");

    if (failures != 0)
        return 1;
    printf("fmi2 %s C ok\n", fmi2Version);
    return 0;
}
