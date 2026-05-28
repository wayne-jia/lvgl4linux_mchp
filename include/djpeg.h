#ifndef _DJPEG_H
#define _DJPEG_H

int djpeg_rgb (char *buf, int len, char *p_rgb, int *p_width, int *p_height);
int djpeg_yuv (char *buf, int len, char *p_yuv, int *p_width, int *p_height);

#endif /* _DJPEG_H */
