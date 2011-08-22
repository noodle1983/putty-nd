#include "zmodem.h"
#include "misc.h"
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include "crctab.c"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const char HEX_PREFIX[] = {ZPAD, ZPAD, ZDLE, ZHEX};
const char HEX_ARRAY[] = "0123456789abcdef";

const char BIN_PREFIX[] = {ZPAD, ZDLE, ZBIN};
const char BIN32_PREFIX[] = {ZPAD, ZDLE, ZBIN32};

void zm_log(zmodem_t *zm, char *fmt, ...)
{
    char *buf;
    va_list ap;

    va_start(ap, fmt);
    buf = dupvprintf(fmt, ap);
    zm->log(zm->log_handle, 1, buf, strlen(buf));
    sfree(buf);
    va_end(ap);
}

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
        memcpy(buffer+*buffer_len, input+*input_parsed_len, inputLen);
        
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


unsigned char decHex(const hex_str_t *h)
{
    return ((h->hex[0] - HEX_ARRAY[0]) << 4) | (h->hex[1] - HEX_ARRAY[0]);
}

void encHex(const unsigned char c, hex_str_t *h)
{
    h->hex[0] = HEX_ARRAY[(c&0xF0) >> 4];
    h->hex[1] = HEX_ARRAY[c&0x0F];
}

void convHex2Plain(const hex_t *hexframe, frame_t* frame)
{
    frame->type = decHex(&(hexframe->type));
    frame->flag[0] = decHex(&(hexframe->flag[0]));
    frame->flag[1] = decHex(&(hexframe->flag[1]));
    frame->flag[2] = decHex(&(hexframe->flag[2]));
    frame->flag[3] = decHex(&(hexframe->flag[3]));
    frame->crc = decHex(&(hexframe->crc[0]))<<8 | decHex(&(hexframe->crc[1]));
}

void convPlain2Hex(const frame_t* frame, hex_t *hexframe)
{
    encHex(frame->type, &(hexframe->type));
    encHex(frame->flag[0], &(hexframe->flag[0]));
    encHex(frame->flag[1], &(hexframe->flag[1]));
    encHex(frame->flag[2], &(hexframe->flag[2]));
    encHex(frame->flag[3], &(hexframe->flag[3]));
    encHex(frame->crc >> 8, &(hexframe->crc [0]));
    encHex(frame->crc & 0x00FF, &(hexframe->crc [1]));
}

unsigned short calcFrameCrc(const frame_t *frame)
{
    int i = 0;
    unsigned short crc;
    crc = updcrc((frame->type & 0x7f), 0);
    for (i = 0; i < 4; i++){
        crc = updcrc(frame->flag[i], crc);
    }
    crc = updcrc(0,updcrc(0,crc));
    return crc;
}

unsigned long calcFrameCrc32(const frame32_t *frame)
{
    int i = 0;
    unsigned long crc = 0xFFFFFFFFL;
    crc = UPDC32((frame->type & 0x7f), crc);
    for (i = 0; i < 4; i++){
        crc = UPDC32(frame->flag[i], crc);
    }
    crc = ~crc;;
    return crc;
}

int handleZrqinit(zmodem_t *zm, const frame_t *frame)
{
    zm_log(zm, "init zmodem session\r\n");
    frame_t zrinit;
    memset(&zrinit, 0, sizeof(frame_t));
    zrinit.type = ZRINIT;
    zrinit.flag[ZF0] = CANFC32|CANFDX|CANOVIO;
    zrinit.crc = calcFrameCrc(&zrinit);
    hex_t hexframe;
    convPlain2Hex(&zrinit, &hexframe);
    char buf[32] = {0};
    int len = 0;
    memcpy(buf+len, HEX_PREFIX, 4);
    len += 4;
    memcpy(buf+len, &hexframe, sizeof (hex_t));
    len += sizeof (hex_t);
    memcpy(buf+len,"\r\n\021", 3);
    len += 3;
    zm->send(zm->handle,buf, len);
    zm_log(zm, "ack init message\r\n");
    return 0;
}

int handleZfile32(zmodem_t *zm, const frame32_t *frame32)
{
    zm_log(zm, "recv file header\r\n");
    zm->state = STATE_PARSE_FILE_NAME;
    return 0;
}

int handleFrame(zmodem_t *zm, const frame_t *frame)
{
    switch (frame->type){
    case ZRQINIT:
        return handleZrqinit(zm, frame);

    case ZFILE:        
    case ZRINIT:
    case ZSINIT:
    case ZACK:
    case ZSKIP:
    case ZNAK:
    case ZABORT:
    case ZFIN:
    case ZRPOS:
    case ZDATA:
    case ZEOF:
    case ZFERR:
    case ZCRC:
    case ZCHALLENGE:
    case ZCOMPL:
    case ZCAN:
    case ZFREECNT:
    case ZCOMMAND:
    case ZSTDERR: 

    default:
        zm_log(zm, "[handleFrame]frame type %d is not implemented!\r\n", frame->type);
        return -1;


    }
    return 0;

}

int handleFrame32(zmodem_t *zm, const frame32_t *frame32)
{
    switch (frame32->type){

    case ZFILE:
        return handleZfile32(zm, frame32);
        
    case ZRQINIT:
    case ZRINIT:
    case ZSINIT:
    case ZACK:
    case ZSKIP:
    case ZNAK:
    case ZABORT:
    case ZFIN:
    case ZRPOS:
    case ZDATA:
    case ZEOF:
    case ZFERR:
    case ZCRC:
    case ZCHALLENGE:
    case ZCOMPL:
    case ZCAN:
    case ZFREECNT:
    case ZCOMMAND:
    case ZSTDERR: 
    default:
        zm_log(zm, "[handleFrame32]frame type %d is not implemented!\r\n", frame32->type);
        return -1;


    }
    return 0;

}

ZmodemResult stateIdle(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    if (len >= 3 && 0 == memcmp(str, "rz\r", 3)){
        zm_log(zm, "see zmodem trigger\r\n");
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
            zm_log(zm, "hex frame: ");
            zm->state = STATE_PARSE_HEX;
        }else if (0 == memcmp(enc_header.hex_pre, BIN_PREFIX, sizeof(BIN_PREFIX))){
            zm_log(zm, "bin frame: ");
            zm->state = STATE_PARSE_BIN;
            zm->buffer[zm->buf_len] = enc_header.hex_pre[3];
            zm->buf_len++;
        }else if (0 == memcmp(enc_header.hex_pre, BIN32_PREFIX, sizeof(BIN32_PREFIX))){
            zm_log(zm, "bin32 frame: ");
            zm->state = STATE_PARSE_BIN32;
            zm->buffer[zm->buf_len] = enc_header.hex_pre[3];
            zm->buf_len++;
        }else{
            zm_log(zm, "unknow frame type!\r\n");
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

        frame_t frame;
        convHex2Plain(&hexframe, &frame);
        if (frame.crc != calcFrameCrc(&frame) || 0 != handleFrame(zm, &frame)){
            zm_log(zm, "crc error!\r\n");
            zm->state = STATE_EXIT;
            return ZR_ERROR;
        }
    }
    return result;
}

ZmodemResult stateParseBin(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    return ZR_ERROR;
}

ZmodemResult stateParseBin32(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    frame32_t frame32;
    ZmodemResult result = decodeStruct(&frame32, sizeof(frame32_t), 
            zm->buffer, &zm->buf_len, sizeof(zm->buffer), 
            str, decodeIndex, len);
    if (ZR_DONE == result){
        unsigned long crc = calcFrameCrc32(&frame32);
        if ( crc != frame32.crc || 0 != handleFrame32(zm, &frame32)){
            zm_log(zm, "crc32 error!\r\n");
            zm->state = STATE_EXIT;
            return ZR_ERROR;
        }
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
        zm->state = STATE_CHK_ENC;
    }
    
    return result;
}

ZmodemResult stateParseFileName(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    uint64_t i = *decodeIndex;
    for (; (i < len) && (str[i] != 0); i++){
        zm->buffer[zm->buf_len++] = str[i];
        if (zm->buf_len > sizeof(zm->buffer)){
            *decodeIndex = i;
            zm_log(zm, "file name is too long!\r\n");
            zm->state = STATE_IDLE;
            return ZR_ERROR;
        }
    }
    *decodeIndex = i + 1;
    if (str[i] == 0){
        zm->buffer[zm->buf_len] = 0;
        strcpy(zm->filename, zm->buffer);
        memset(zm->buffer, 0, sizeof(zm->buffer));
        zm->buf_len = 0;
        zm_log(zm, "start to handle file:%s.\r\n", zm->filename);
        zm->state = STATE_PARSE_FILE_SIZE;
        return ZR_DONE;
    }
    
    return ZR_PARTLY;
}

ZmodemResult stateParseFileSize(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    uint64_t i = *decodeIndex;
    for (; (i < len) && (str[i] != ' '); i++){
        zm->buffer[zm->buf_len++] = str[i];
        if (zm->buf_len > sizeof(zm->buffer)){
            *decodeIndex = i;
            zm_log(zm, "file size is too long!\r\n");
            zm->state = STATE_IDLE;
            return ZR_ERROR;
        }
    }
    *decodeIndex = i + 1;
    if (str[i] == ' '){
        zm->buffer[zm->buf_len] = 0;
        zm->filesize = atoi(zm->buffer);
        
        memset(zm->buffer, 0, sizeof(zm->buffer));
        zm->buf_len = 0;
        zm_log(zm, "file size:%d.\r\n", zm->filesize);
        zm->state = STATE_PARSE_FILE_MTIME;
        return ZR_DONE;
    }
    
    return ZR_PARTLY;
}


ZmodemResult stateParseFileMtime(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    uint64_t i = *decodeIndex;
    for (; (i < len) && (str[i] != ' '); i++){
        zm->buffer[zm->buf_len++] = str[i];
        if (zm->buf_len > sizeof(zm->buffer)){
            *decodeIndex = i;
            zm_log(zm, "file mtime is too long!\r\n");
            zm->state = STATE_IDLE;
            return ZR_ERROR;
        }
    }
    *decodeIndex = i + 1;
    if (str[i] == ' '){
        zm->buffer[zm->buf_len] = 0;
        sscanf(zm->buffer, "%lo", &zm->mtime);;
        
        memset(zm->buffer, 0, sizeof(zm->buffer));
        zm->buf_len = 0;
        zm_log(zm, "file mtime:%d.\r\n", zm->mtime);
        zm->state = STATE_PARSE_FILE_MODE;
        return ZR_DONE;
    }
    
    return ZR_PARTLY;
}

ZmodemResult stateParseFileMode(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    uint64_t i = *decodeIndex;
    for (; (i < len) && (str[i] != ' '); i++){
        zm->buffer[zm->buf_len++] = str[i];
        if (zm->buf_len > sizeof(zm->buffer)){
            *decodeIndex = i;
            zm_log(zm, "file mode is too long!\r\n");
            zm->state = STATE_IDLE;
            return ZR_ERROR;
        }
    }
    *decodeIndex = i + 3;
    if (str[i] == ' '){
        zm->buffer[zm->buf_len] = 0;
        sscanf(zm->buffer, "%o", &zm->mode);;
        
        memset(zm->buffer, 0, sizeof(zm->buffer));
        zm->buf_len = 0;
        zm_log(zm, "file mode:%o.\r\n", zm->mode);
        zm->state = STATE_PARSE_FILE_NFILELEFT;
        return ZR_DONE;
    }
    
    return ZR_PARTLY;
}


ZmodemResult stateParseFileNFileLeft(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    uint64_t i = *decodeIndex;
    for (; (i < len) && (str[i] != ' '); i++){
        zm->buffer[zm->buf_len++] = str[i];
        if (zm->buf_len > sizeof(zm->buffer)){
            *decodeIndex = i;
            zm_log(zm, "file count is too long!\r\n");
            zm->state = STATE_IDLE;
            return ZR_ERROR;
        }
    }
    *decodeIndex = i + 1;
    if (str[i] == ' '){
        zm->buffer[zm->buf_len] = 0;
        sscanf(zm->buffer, "%d", &zm->nfilesleft);;
        
        memset(zm->buffer, 0, sizeof(zm->buffer));
        zm->buf_len = 0;
        zm_log(zm, "file count:%d.\r\n", zm->nfilesleft);
        zm->state = STATE_PARSE_FILE_TOTALSIZELEFT;
        return ZR_DONE;
    }
    
    return ZR_PARTLY;
}

ZmodemResult stateParseFileTotalSizeLeft(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    uint64_t i = *decodeIndex;
    for (; (i < len) && (str[i] != 0); i++){
        zm->buffer[zm->buf_len++] = str[i];
        if (zm->buf_len > sizeof(zm->buffer)){
            *decodeIndex = i;
            zm_log(zm, "total size is too long!\r\n");
            zm->state = STATE_IDLE;
            return ZR_ERROR;
        }
    }
    *decodeIndex = i + 1;
    if (str[i] == 0){
        zm->buffer[zm->buf_len] = 0;
        sscanf(zm->buffer, "%d", &zm->totalfilesize);;
        
        memset(zm->buffer, 0, sizeof(zm->buffer));
        zm->buf_len = 0;
        zm_log(zm, "total size:%d.\r\n", zm->totalfilesize);
        zm->state = STATE_IDLE;
        return ZR_DONE;
    }
    
    return ZR_PARTLY;
}
ZmodemResult stateDump(zmodem_t *zm, const char* const str, const int len, uint64_t *decodeIndex)
{
    zm_log(zm, "\nrecv%d[", len);
    int i = *decodeIndex;
    for (; i < len; i++)
        zm_log(zm, "0x%x ", (unsigned)str[i]);
    zm_log(zm, "]\r\n");
    zm->state = STATE_IDLE;
    *decodeIndex = len;
    return ZR_DONE;
}

void initZmodem(zmodem_t *zm, 
    void* handle, int (*send) (void *handle, char *buf, int len),
    void* log_handle, int (*log)(void *log_handle, int is_stderr, const char *data, int len))
{
    zm->state = STATE_IDLE;
    zm->handle = handle;
    zm->send = send;
    zm->log_handle = log_handle;
    zm->log = log;

    memset(zm->buffer, 0, sizeof(zm->buffer));
    zm->buf_len = 0;

    memset(zm->filename, 0, sizeof(zm->filename));
    zm->filesize = 0;
    zm->mtime = 0;
    zm->mode = 0;
    zm->nfilesleft = 0;
    zm->totalfilesize = 0;
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

        case STATE_PARSE_BIN:
            result = stateParseBin(zm, str, len, &decodeIndex);
            break;

        case STATE_PARSE_BIN32:
            result = stateParseBin32(zm, str, len, &decodeIndex);
            break;

        case STATE_PARSE_LINESEEDXON:
            result = stateParseLineseedXon(zm, str, len, &decodeIndex);
            break;  

        case STATE_PARSE_FILE_NAME:
            result = stateParseFileName(zm, str, len, &decodeIndex);
            break;  

        case STATE_PARSE_FILE_SIZE:
            result = stateParseFileSize(zm, str, len, &decodeIndex);
            break;              

        case STATE_PARSE_FILE_MTIME:
            result = stateParseFileMtime(zm, str, len, &decodeIndex);
            break;  

        case STATE_PARSE_FILE_MODE:
            result = stateParseFileMode(zm, str, len, &decodeIndex);
            break;  

        case STATE_PARSE_FILE_NFILELEFT:
            result = stateParseFileNFileLeft(zm, str, len, &decodeIndex);
            break;  

        case STATE_PARSE_FILE_TOTALSIZELEFT:
            result = stateParseFileTotalSizeLeft(zm, str, len, &decodeIndex);
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


