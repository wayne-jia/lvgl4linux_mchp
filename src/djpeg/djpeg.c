#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include "jpeglib.h"
#include "datasrc.h"

#define MAX_WIDTH	1024
#define LINES		16

static JSAMPLE buf_rgb[MAX_WIDTH * 3];
static JSAMPROW row_rgb[1] = { buf_rgb };
static JSAMPARRAY scanarray_rgb = { row_rgb };

static JSAMPLE buf_y[LINES][MAX_WIDTH];
static JSAMPLE buf_u[LINES / 2][MAX_WIDTH / 2];
static JSAMPLE buf_v[LINES / 2][MAX_WIDTH / 2];
static JSAMPROW row_y[LINES] = {
	buf_y[0],  buf_y[1],  buf_y[2],  buf_y[3],
	buf_y[4],  buf_y[5],  buf_y[6],  buf_y[7],
	buf_y[8],  buf_y[9],  buf_y[10], buf_y[11],
	buf_y[12], buf_y[13], buf_y[14], buf_y[15]
};
static JSAMPROW row_u[LINES / 2] = {
	buf_u[0], buf_u[1], buf_u[2], buf_u[3],
	buf_u[4], buf_u[5], buf_u[6], buf_u[7]
};
static JSAMPROW row_v[LINES / 2] = {
	buf_v[0], buf_v[1], buf_v[2], buf_v[3],
	buf_v[4], buf_v[5], buf_v[6], buf_v[7]
};
static JSAMPARRAY scanarray_yuv[3] = {
	row_y, row_u, row_v
};

struct my_error_mgr {
	struct jpeg_error_mgr pub;    /* "public" fields */

	jmp_buf setjmp_buffer;    /* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
	/* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	my_error_ptr myerr = (my_error_ptr) cinfo->err;

	/* Always display the message. */
	/* We could postpone this until after returning, if we chose. */
	(*cinfo->err->output_message) (cinfo);

	/* Return control to the setjmp point */
	longjmp(myerr->setjmp_buffer, 1);
}

int djpeg_rgb (char *buf, int len, char *p_rgb, int *p_width, int *p_height)
{
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	int ret;

	if ((!buf) || (len <= 0) || (!p_rgb) || (!p_width) || (!p_height))
		return -1;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return jerr.pub.msg_code;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_memory_src(&cinfo, buf, len);
	(void)jpeg_read_header(&cinfo, TRUE);

	(void)jpeg_start_decompress(&cinfo);
	/* Check the size of output buffer */
	if ((*p_width < cinfo.output_width) ||
		(*p_height < cinfo.output_height) ||
		(MAX_WIDTH < cinfo.output_width)) {
		ret = -1;
		goto OUT; 
	}
	*p_width  = cinfo.output_width;
	*p_height = cinfo.output_height;

	while (cinfo.output_scanline < cinfo.output_height) {
		(void)jpeg_read_scanlines(&cinfo, scanarray_rgb, 1);

		memcpy(p_rgb, buf_rgb, cinfo.output_width * 3);
        p_rgb += cinfo.output_width * 3;
	}

	ret = 0;
OUT:
	(void)jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	return ret;
}

int djpeg_yuv (char *buf, int len, char *p_yuv, int *p_width, int *p_height)
{
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	int lines, mcu_lines;
	int i, ret;
	char *p_y, *p_u, *p_v;

	if ((!buf) || (len <= 0) || (!p_yuv) || (!p_width) || (!p_height))
		return -1;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return jerr.pub.msg_code;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_memory_src(&cinfo, buf, len);
	(void)jpeg_read_header(&cinfo, TRUE);

	/* Adjust default decompression parameters by re-parsing the options */
	cinfo.raw_data_out = TRUE;
	cinfo.out_color_space = JCS_YCbCr;
	cinfo.two_pass_quantize = FALSE;
	cinfo.dither_mode = JDITHER_NONE;
	cinfo.dct_method = JDCT_FASTEST;
	cinfo.do_fancy_upsampling = FALSE;
	cinfo.do_block_smoothing = FALSE;

	(void)jpeg_start_decompress(&cinfo);
	/* Check the size of output buffer */
	if ((*p_width < cinfo.output_width) ||
		(*p_height < cinfo.output_height) ||
		(MAX_WIDTH < cinfo.output_width)) {
		ret = -1;
		goto OUT; 
	}
	*p_width  = cinfo.output_width;
	*p_height = cinfo.output_height;

	mcu_lines = cinfo.max_v_samp_factor * cinfo.min_DCT_h_scaled_size;
	if (mcu_lines > LINES) {
		ret = -1;
		goto OUT;
	}
	lines = mcu_lines;

	p_y = p_yuv;
	p_u = p_y + (cinfo.output_width * cinfo.output_height);
	p_v = p_u + (cinfo.output_width * cinfo.output_height / 4);

	while (cinfo.output_scanline < cinfo.output_height) {
		if ((cinfo.output_height - cinfo.output_scanline) < mcu_lines)
			lines = cinfo.output_height - cinfo.output_scanline;

		ret = jpeg_read_raw_data(&cinfo, scanarray_yuv, mcu_lines);
		if (ret < lines) {
			ret = -1;
			goto OUT;
		}

		for (i=0; i<lines; i++) {
			memcpy(p_y, buf_y[i], cinfo.output_width);
			p_y += cinfo.output_width;
		}
		for (i=0; i<lines/2; i++) {
			memcpy(p_u, buf_u[i], cinfo.output_width/2);
			memcpy(p_v, buf_v[i], cinfo.output_width/2);
			p_u += cinfo.output_width/2;
			p_v += cinfo.output_width/2;
		}
	}

	ret = 0;
OUT:
	(void)jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	return ret;
}
