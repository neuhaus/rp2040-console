#ifndef NH313_TAMP_CODEC_H
#define NH313_TAMP_CODEC_H
#include <stddef.h>
void nh_codec_begin(int);
void nh_codec_write(int, const void *, size_t);
void nh_codec_flush(int);
void nh_codec_end(int);
void nh_codec_reset(void);
int nh_codec_read(int, void *, size_t);
int nh_codec_valid(int);
#endif
