/*
 * ipptransform utility for converting PDF and JPEG files to raster data or HP PCL.
 *
 * Copyright 2016 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * This file is subject to the Apple OS-Developed Software exception.
 */

#include <cups/cups.h>

#ifdef __APPLE__
#  include <ApplicationServices/ApplicationServices.h>
#endif /* __APPLE__ */


/*
 * Local types...
 */

typedef struct xform_raster_s
{
  cups_raster_t		*ras;		/* Raster stream */
  cups_page_header2_t	header;		/* Page header */
  unsigned char		*band_buffer;	/* Band buffer */
  unsigned		band_height;	/* Band height */
} xform_raster_t;

typedef ssize_t (*xform_write_cb_t)(void *, const void *, size_t);


/*
 * Local functions...
 */

static int	load_env_options(cups_option_t **options);
static void	pcl_end_job(xform_write_cb_t cb, void *ctx);
static void	pcl_end_page(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	pcl_start_job(xform_write_cb_t cb, void *ctx);
static void	pcl_start_page(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	pcl_write_line(unsigned char *line, xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	raster_end_job(xform_write_cb_t cb, void *ctx);
static void	raster_end_page(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	raster_start_job(xform_write_cb_t cb, void *ctx);
static void	raster_start_page(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	raster_write_line(unsigned char *line, xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	usage(int status) __attribute__((noreturn));
static int	xform_jpeg(const char *filename, int num_options, cups_option_t *options, xform_write_cb_t cb, void *ctx);
static int	xform_pdf(const char *filename, int num_options, cups_option_t *options, xform_write_cb_t cb, void *ctx);


/*
 * 'main()' - Main entry for transform utility.
 */

int
main(int  argc,
     char *argv[])
{
  return (0);
}
