#include "zmodem.h"
#include "misc.h"
#include <stdint.h>
#include <assert.h>

const char HEX_PREFIX[] = {ZPAD, ZPAD, ZDLE, ZHEX};
const char HEX_ARRAY[] = "0123456789abcdef";

ZmodemResult decodeStruct(
    void* to_struct, const int struct_len, 
    char* buffer, int* buffer_len, const int buffer_size,
    const char* const input, uint64_t* input_parsed_len, const int input_len)
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

    memset(zm->buffer, 0, sizeof(zm->buffer));
    zm->buf_len = 0;
}

ZmodemResult stateIdle(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    if (len >= 3 && 0 == memcmp(str, "rz\r", 3)){
        zm->state = STATE_CHK_ENC;
        *decodeIndex += 3;
        return ZR_DONE;
    }
    return ZR_ERROR;
}

ZmodemResult stateCheckEncoding(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    enc_header_t enc_header;
    ZmodemResult result = decodeStruct(&enc_header, sizeof(enc_header_t), 
            zm->buffer, &zm->buf_len, sizeof(zm->buffer), 
            str, decodeIndex, len);
    if (ZR_DONE == result){
        if (0 == memcmp(enc_header.hex_pre, HEX_PREFIX, sizeof(HEX_PREFIX))){
            zm->state = STATE_PARSE_HEX;
        }else{
            zm->state = STATE_EXIT;
        }
    }
    return result;
}

ZmodemResult stateParseHex(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    hex_t hexframe;
    ZmodemResult result = decodeStruct(&hexframe, sizeof(hex_t), 
            zm->buffer, &zm->buf_len, sizeof(zm->buffer), 
            str, decodeIndex, len);
    if (ZR_DONE == result){
        zm->state = STATE_PARSE_LINESEEDXON;
    }
    return result;
}

ZmodemResult stateParseLineseedXon(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{

    lineseedxon_t lineseedxon;
    ZmodemResult result = decodeStruct(&lineseedxon, sizeof(lineseedxon_t), 
            zm->buffer, &zm->buf_len, sizeof(zm->buffer), 
            str, decodeIndex, len);
    if (ZR_DONE == result){
        assert ('\r' == lineseedxon.lineseed[0] || '\n' == lineseedxon.lineseed[0]);
    }
    
    return result;
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
            
        case STATE_PARSE_HEX:
            result = stateParseHex(zm, str, len, &decodeIndex);
            break;

        case STATE_PARSE_LINESEEDXON:
            result = stateParseLineseedXon(zm, str, len, &decodeIndex);
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


