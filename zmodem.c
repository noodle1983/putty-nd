#include "zmodem.h"
#include "misc.h"
#include <stdint.h>

void initZmodem(zmodem_t *zm, void* handle, int (*send) (void *handle, char *buf, int len))
{
    zm->state = STATE_IDLE;
    zm->handle = handle;
    zm->send = send;
}

ZmodemResult stateIdle(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    if (len >= 3 && 0 == memcmp(str, "rz\r", 3)){
        zm->state = STATE_DUMP;
        *decodeIndex += 3;
        return ZR_DONE;
    }
    return ZR_ERROR;
}

ZmodemResult stateDump(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    debug(("\nrecv%d[", len));
    int i = *decodeIndex;
    for (; i < len; i++)
        debug(("0x%x ", (unsigned)str[i]));
    debug(("]\n"));
    zm->state = STATE_IDLE;
    *decodeIndex = len;
    return ZR_DONE;
}

ZmodemResult processZmodem(zmodem_t *zm, const char* const str, const int len)
{
    int result = ZR_DONE;
    uint64_t decodeIndex = 0;
    
    do{
        switch(zm->state)
        {
        case STATE_IDLE:
            result = stateIdle(zm, str, len, &decodeIndex);
            break;
            
        case STATE_DUMP:
            result = stateDump(zm, str, len, &decodeIndex);
            break;
            
        default:
            zm->state = STATE_IDLE;
            decodeIndex = len;
            result = ZR_ERROR;
            break;
        }
    }while(ZR_DONE == result && STATE_IDLE != zm->state);
    
    return result;
}


