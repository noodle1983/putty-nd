#include "zmodem.h"
#include "misc.h"


void initZmodem(zmodem *zm, void* handle, int (*send) (void *handle, char *buf, int len))
{
    zm->state = STATE_IDLE;
    zm->handle = handle;
    zm->send = send;
}

int stateIdle(zmodem *zm, const char* const str, const int len)
{
    debug(("\nrecv[%s]\n", str));
    return -1;
}

int processZmodem(zmodem *zm, const char* const str, const int len)
{
    int result = 0;
    switch(zm->state)
    {
    case STATE_IDLE:
        result = stateIdle(zm, str, len);
        break;

    }
    return result;
}


