#include "zmodem.h"
#include "misc.h"
#include <stdint.h>

const char HEX_PREFIX[] = {ZPAD, ZPAD, ZDLE, ZHEX};
const char HEX_ARRAY[] = "0123456789abcdef";

ZmodemResult decodeStruct(
    char* to_struct, const int struct_len, 
    char* buffer, int* buffer_len, const int buffer_size,
    const char* input, uint64_t* input_parsed_len, const int input_len)
{
    int bufferLen = *buffer_len;
    int inputLen = input_len - *input_parsed_len;
    if (inputLen + bufferLen < struct_len){
        if (inputLen + bufferLen > buffer_size){
            memset(buffer, 0, buffer_size);
            *buffer_len = 0;
            return ZR_ERROR;
        }
        memcpy(buffer, input+*input_parsed_len, inputLen);
        
        *buffer_len += inputLen; 
        *input_parsed_len += inputLen;
        return ZR_PARTLY;
    }else{
        memcpy(to_struct, buffer, bufferLen);
        memcpy(to_struct + bufferLen, input + *input_parsed_len, struct_len - bufferLen);
        memset(buffer, 0, buffer_size);
        *buffer_len = 0;
        *input_parsed_len += struct_len - bufferLen;
        return ZR_DONE;
    }
    return ZR_ERROR;
}

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

ZmodemResult stateCheckEncoding(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    if (len >= 3 && 0 == memcmp(str, "rz\r", 3)){
        zm->state = STATE_DUMP;
        *decodeIndex += 3;
        return ZR_DONE;
    }
    return ZR_ERROR;
}


ZmodemResult stateZrqinit(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
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
            
        case STATE_CHK_ENC:
            result = stateCheckEncoding(zm, str, len, &decodeIndex);
            break;
            
        case STATE_ZRQINIT:
            result = stateZrqinit(zm, str, len, &decodeIndex);
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


