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
#define STATE_IDLE 0

struct zmodem_t{
   int state;
   void *handle;
   int (*send) (void *handle, char *buf, int len);
};
typedef struct zmodem_t zmodem;

void initZmodem(zmodem *zm, void* handle, int (*send) (void *handle, char *buf, int len));
int processZmodem(zmodem *zm, const char* const str, const int len);
#endif /* ZMODEM_H */

