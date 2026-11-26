/* Exercises agent creation, local ICE attributes and state queries, none
   of which opens a socket. */
#include <juice/juice.h>

#include <stdio.h>
#include <string.h>

static int ok = 1;

static void check(int pass, const char *what)
{
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

int main(void)
{
    juice_config_t config;
    memset(&config, 0, sizeof config);
    juice_agent_t *agent = juice_create(&config);
    check(agent != NULL, "juice_create");
    if (!agent)
        return 1;

    check(juice_get_state(agent) == JUICE_STATE_DISCONNECTED, "initial state");
    check(juice_set_local_ice_attributes(agent, "abcd", "short") == JUICE_ERR_INVALID,
          "short pwd rejected");
    check(juice_set_local_ice_attributes(agent, "abcd", "0123456789abcdefghijkl") == 0,
          "valid attributes");

    char sdp[JUICE_MAX_SDP_STRING_LEN];
    check(juice_get_local_description(agent, sdp, sizeof sdp) == 0, "local description");
    check(strcmp(sdp, "a=ice-ufrag:abcd\r\n"
                      "a=ice-pwd:0123456789abcdefghijkl\r\n"
                      "a=ice-options:ice2 trickle\r\n") == 0,
          "local description text");

    check(strcmp(juice_state_to_string(JUICE_STATE_COMPLETED), "completed") == 0,
          "state name");
    check(strcmp(juice_state_to_string(JUICE_STATE_FAILED), "failed") == 0, "failed name");

    juice_destroy(agent);
    return ok ? 0 : 1;
}
