/*************************************************************************/
/* sn9c102-webcam.c - Imlib2-based tool for sn9c102 webcams.             */
/* by Paul Duncan <pabs@pablotron.org>                                   */
/*                                                                       */
/* (sorta, this code is based on Christophe Lucas's horridly written     */
/* SDL-based sn9c102 viewer from the following URL:                      */
/* http://odie.mcom.fr/~clucas/articles/sn9c102.html)                    */
/*                                                                       */
/* To Compile:                                                           */
/*   cc -O2 -W -Wall -o sn9c102-webcam{,.c} -lImlib2                     */
/*                                                                       */
/* To Use:                                                               */
/*   Run with no arguments (e.g. ./sn9c102-webcam) or see the function   */
/*   print_usage_and_exit() for command-line parameters and examples.    */
/*                                                                       */
/*************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>

/* load v4l headers */
#include <linux/compiler.h>
#include <linux/videodev.h>

/* imlib2 headers */
#define X_DISPLAY_MISSING
#include <Imlib2.h>

typedef struct {
  unsigned char *rgb_buf, *rgba_buf;
  int w, h, counter;
  char *im_path; /* can contain sprintf-style %d, %02d, etc */
} CamImage;

static void im_init(CamImage *cam_im, unsigned char *rgb_buf, int w, int h, char *im_path);
static void im_save(CamImage *cam_im); 
static void bayer2rgb24(unsigned char *dst, unsigned char *src, unsigned long w, unsigned long h);
static void die(const char *msg);
static void print_usage_and_exit(const char *app);

int main(int argc, char **argv) {
  unsigned long w, h, f;
  unsigned char	*s, *d;
  unsigned int   delay = 20 ; /* 1/25 * 1000 */
  int	          video_dev, read_ret;

  struct v4l2_cropcap cropcap;
  struct v4l2_crop    crop;
  struct v4l2_format  format;

  CamImage im;
  char *im_path = NULL;

  /* check command-line arguments */
  if (argc < 3)
    print_usage_and_exit(argv[0]);

  /* read required command-line arguments */
  /* (fugly, but based on the original code) */
  im_path = argv[2];

  /* set reasonable defaults, then check optional parameters */
  w = 320; h = 240; delay = 20; f = 1;

  if (argc > 3)
    delay = atoi(argv[3]);
  if (argc > 4)
    w = atoi(argv[4]);
  if (argc > 5)
    h = atoi(argv[5]);
  if (argc > 6)
    f = atoi(argv[6]);

  /* print out debugging information */
  if (getenv("SN9C102_DEBUG"))
    fprintf(stderr, "DEBUG: dev = %s, im = %s, delay = %d, w = %ld, h = %ld, f = %ld\n",
            argv[1], im_path, delay, w, h, f);
  
  /* allocate bayer and rgb24 buffers */
  if ((s = (unsigned char*) malloc(w * h)) == NULL)
    die("couldn't allocate bayer buffer:");
  if ((d = (unsigned char*) malloc(w * h * 3)) == NULL)
    die("couldn't allocate RGB24 buffer:");

  /* initialize image save data */
  im_init(&im, d, w, h, im_path);

  /* open video device */
  video_dev = open(argv[1], O_RDONLY);
  if (video_dev < 0)
    die("couldn't open video4linux device:");

  /* init v4l device */
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(video_dev, VIDIOC_CROPCAP, &cropcap) == -1)
    die("ioctl() VIDEOC_CROPCAP failed");

  crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  crop.c.top = 0;
  crop.c.left = 0;
  crop.c.width = w * f;
  crop.c.height = h * f;

  if (ioctl(video_dev, VIDIOC_S_CROP, &crop) == -1)
    die("ioctl() VIDEOC_S_CROP failed");

  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = w;
  format.fmt.pix.height = h;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_SBGGR8;

  if (ioctl(video_dev, VIDIOC_S_FMT, &format) == -1)
    die("ioctl() VIDEOC_S_FMT failed");

  /* loop forever */
  for (;;) {
    /* read frame from v4l device */
    read_ret = read(video_dev, s, w * h);
    if (read_ret != (int) (w * h))
        fprintf(stderr, "WARNING: fragmented picture\n" );

    /* convert data to rgb24 format */
    bayer2rgb24(d, s, w, h);

    /* save current image */
    im_save(&im);

    /* delay until next frame */
    usleep(delay);
  }

  /* close v4l device */
  close(video_dev);

  /* return success */
  return EXIT_SUCCESS;
}

/*
 * im_init(): populate a CamImage structure.
 */
static void im_init(CamImage *id, unsigned char *rgb_buf, int w, int h, char *im_path) {
  /* clear image data structure */
  memset(id, 0, sizeof(CamImage));

  /* populate struct members */
  id->rgb_buf = rgb_buf;
  id->w = w; 
  id->h = h;
  id->im_path = im_path;

  /* allocate RGBA buffer */
  if ((id->rgba_buf = malloc(4 * w * h)) == NULL)
    die("couldn't allocate RGBA buffer");

  /* set buffer to all alpha                    */
  memset(id->rgba_buf, 0xff, (4 * w * h));
}

/*
 * im_save(): save cam image structure to a file.
 */
static void im_save(CamImage *id) {
  Imlib_Image im;
  int i, j;
  char path_buf[BUFSIZ];
  unsigned char *src, *dst;;

  /* copy pixel data from rgb buf to rgba buf */
  for (i = 0; i < id->h; i++)
    for (j = 0; j < id->w; j++) { 
      dst = id->rgba_buf + 4 * (i * id->w + j);
      src = id->rgb_buf + 3 * (i * id->w + j);
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
      dst[3] = 0xff;
    }

  /* create, save, and free image */
  im = imlib_create_image_using_data(id->w, id->h, (DATA32*) id->rgba_buf);
  imlib_context_set_image(im);

  /* build output file name */
  snprintf(path_buf, sizeof(path_buf), id->im_path, id->counter++);

  imlib_save_image(path_buf);
  
  /* free image data */
  imlib_free_image_and_decache();
}

/*
 * BAYER2RGB24 ROUTINE TAKEN FROM:
 *
 * Sonix SN9C101 based webcam basic I/F routines
 * Copyright (C) 2004 Takafumi Mizuno <taka-qce@ls-a.jp>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
static void bayer2rgb24(unsigned char *dst, unsigned char *src, unsigned long width, unsigned long height)
{
    unsigned long i;
    unsigned char *rawpt, *scanpt;
    unsigned long size;

    rawpt = src;
    scanpt = dst;
    size = width*height;

    for ( i = 0; i < size; i++ ) {
	if ( (i/width) % 2 == 0 ) {
	    if ( (i % 2) == 0 ) {
		/* B */
		if ( (i > width) && ((i % width) > 0) ) {
		    *scanpt++ = (*(rawpt-width-1)+*(rawpt-width+1)+
				 *(rawpt+width-1)+*(rawpt+width+1))/4;	/* R */
		    *scanpt++ = (*(rawpt-1)+*(rawpt+1)+
				 *(rawpt+width)+*(rawpt-width))/4;	/* G */
		    *scanpt++ = *rawpt;					/* B */
		} else {
		    /* first line or left column */
		    *scanpt++ = *(rawpt+width+1);		/* R */
		    *scanpt++ = (*(rawpt+1)+*(rawpt+width))/2;	/* G */
		    *scanpt++ = *rawpt;				/* B */
		}
	    } else {
		/* (B)G */
		if ( (i > width) && ((i % width) < (width-1)) ) {
		    *scanpt++ = (*(rawpt+width)+*(rawpt-width))/2;	/* R */
		    *scanpt++ = *rawpt;					/* G */
		    *scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;		/* B */
		} else {
		    /* first line or right column */
		    *scanpt++ = *(rawpt+width);	/* R */
		    *scanpt++ = *rawpt;		/* G */
		    *scanpt++ = *(rawpt-1);	/* B */
		}
	    }
	} else {
	    if ( (i % 2) == 0 ) {
		/* G(R) */
		if ( (i < (width*(height-1))) && ((i % width) > 0) ) {
		    *scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;		/* R */
		    *scanpt++ = *rawpt;					/* G */
		    *scanpt++ = (*(rawpt+width)+*(rawpt-width))/2;	/* B */
		} else {
		    /* bottom line or left column */
		    *scanpt++ = *(rawpt+1);		/* R */
		    *scanpt++ = *rawpt;			/* G */
		    *scanpt++ = *(rawpt-width);		/* B */
		}
	    } else {
		/* R */
		if ( i < (width*(height-1)) && ((i % width) < (width-1)) ) {
		    *scanpt++ = *rawpt;					/* R */
		    *scanpt++ = (*(rawpt-1)+*(rawpt+1)+
				 *(rawpt-width)+*(rawpt+width))/4;	/* G */
		    *scanpt++ = (*(rawpt-width-1)+*(rawpt-width+1)+
				 *(rawpt+width-1)+*(rawpt+width+1))/4;	/* B */
		} else {
		    /* bottom line or right column */
		    *scanpt++ = *rawpt;				/* R */
		    *scanpt++ = (*(rawpt-1)+*(rawpt-width))/2;	/* G */
		    *scanpt++ = *(rawpt-width-1);		/* B */
		}
	    }
	}
	rawpt++;
    }

}

/*
 * die(): exit with error.
 */
static void die(const char *msg) {
  static char *unknown_msg = "unknown error";
  char *errno_msg;
  int len;
  len = strlen(msg) + 1;

  /* print error message, and possibly errno output */
  if (msg[len - 2] == ':' && ((errno_msg = strerror(errno)) || (errno_msg = unknown_msg)))
    fprintf(stderr, "ERROR: %s %s", msg, errno_msg);
  else
    fprintf(stderr, "ERROR: %s", msg);

  /* print trailing newline */
  if (msg[len - 2] != '\n')
    fprintf(stderr, "\n");

  /* exit out of program */
  exit(EXIT_FAILURE);
}

static void print_usage_and_exit(const char *app) {
  printf(
    "Usage: %s <video_dev> <image_path> [delay] [width] [height] [scalefact]\n"
    "\n"
    "Parameters:\n"
    "  video_dev:  Path to video4linux device (e.g. \"/dev/video0\", required).\n"
    "   img_path:  Path of output image (e.g. \"cam.jpg\", required).\n"
    "      delay:  Delay between captures, in milliseconds. (defaults to 20).\n"
    "      width:  Width of image, in pixels. (defaults to 320).\n"
    "     height:  Height of image, in pixels. (defaults to 240).\n"
    "  scalefact:  Downscale factor. (defaults to 1).\n"
    "\n"
    "Examples:\n"
    "  # save to \"cam.jpg\" from V4L device \"/dev/video0\".\n"
    "  %s /dev/video0 cam.jpg\n"
    "\n"
    "  # save to \"cam-XXX.jpg\" from \"/dev/video0\" with a 1 second delay.\n"
    "  %s /dev/video0 cam-%%03d.jpg 1000\n"
    "\n"
    "Bugs:\n"
    "  Probably lots.  I hacked this up in an evening from the original source.\n"
    "\n"
    "About the Author:\n"
    "  Paul Duncan <pabs@pablotron.org>\n"
    "  http://pablotron.org/\n",
    app, app, app);
  exit(EXIT_FAILURE);
}
