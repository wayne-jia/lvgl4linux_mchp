#ifndef _DATASRC_H
#define _DATASRC_H

#include "jpeglib.h"

void jpeg_memory_src (j_decompress_ptr cinfo, char * inbuff, int inbuff_size);

#endif /* _DATASRC_H */
