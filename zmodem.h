#ifndef ZMODEM_H
#define ZMODEM_H

// Frame types
#define ZRQINIT    0
#define ZRINIT     1
#define ZSINIT     2
#define ZACK       3
#define ZFILE      4
#define ZSKIP      5
#define ZNAK       6
#define ZABORT     7
#define ZFIN       8
#define ZRPOS      9
#define ZDATA      10
#define ZEOF       11
#define ZFERR      12
#define ZCRC       13
#define ZCHALLENGE 14
#define ZCOMPL     15
#define ZCAN       16
#define ZFREECNT   17
#define ZCOMMAND   18
#define ZSTDERR    19

//state
typedef enum{
    STATE_IDLE = 0,
    STATE_ZRQINIT = 1,

    STATE_EXIT,
    STATE_DUMP
} ZmodemState;

typedef enum{
    ZR_ERROR  = -1,
    ZR_DONE   = 0,
    ZR_PARTLY = 1

} ZmodemResult;

struct zmodem_tag{
   ZmodemState state;
   void *handle;
   int (*send) (void *handle, char *buf, int len);
};
typedef struct zmodem_tag zmodem_t;

void initZmodem(zmodem_t *zm, void* handle, int (*send) (void *handle, char *buf, int len));
int processZmodem(zmodem_t *zm, const char* const str, const int len);
#endif /* ZMODEM_H */

