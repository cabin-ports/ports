#include <com/amazonaws/kinesis/video/state/Include.h>
#include <stdio.h>

#define EXPECT(c)                                                                                                                                    \
    do {                                                                                                                                             \
        if (!(c)) {                                                                                                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                                              \
            return 1;                                                                                                                                \
        }                                                                                                                                            \
    } while (0)

static UINT64 executed;

static UINT64 fixedTime(UINT64 customData)
{
    (void) customData;
    return 0;
}

static STATUS toB(UINT64 customData, PUINT64 pNext)
{
    (void) customData;
    *pNext = 2;
    return STATUS_SUCCESS;
}

static STATUS toMissing(UINT64 customData, PUINT64 pNext)
{
    (void) customData;
    *pNext = 99;
    return STATUS_SUCCESS;
}

static STATUS execute(UINT64 customData, UINT64 delay)
{
    (void) customData;
    (void) delay;
    executed++;
    return STATUS_SUCCESS;
}

int main(void)
{
    StateMachineState states[2] = {
        {1, 3, toB, execute, NULL, 0, STATUS_SUCCESS},
        {2, 3, toMissing, execute, NULL, 0, STATUS_SUCCESS},
    };
    PStateMachine pMachine = NULL;
    PStateMachineState pState = NULL;

    EXPECT(createStateMachine(states, 2, 0, fixedTime, 0, &pMachine) == STATUS_SUCCESS);
    EXPECT(getStateMachineCurrentState(pMachine, &pState) == STATUS_SUCCESS && pState->state == 1);
    EXPECT(stepStateMachine(pMachine) == STATUS_SUCCESS);
    EXPECT(executed == 1);
    EXPECT(getStateMachineCurrentState(pMachine, &pState) == STATUS_SUCCESS && pState->state == 2);
    EXPECT(stepStateMachine(pMachine) == STATUS_STATE_MACHINE_STATE_NOT_FOUND);
    EXPECT(freeStateMachine(pMachine) == STATUS_SUCCESS);
    return 0;
}
