/*
 * filter_transpose.c -- transposeping filter
 * Copyright (C) 2009-2014 Meltytech, LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <framework/mlt_filter.h>
#include <framework/mlt_frame.h>
#include <framework/mlt_log.h>
#include <framework/mlt_profile.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <libyuv.h>
#include <libyuv/rotate_argb.h>

static int filter_get_image( mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{
    int error = 0;
	mlt_profile profile = mlt_frame_pop_service( frame );

	// Get the properties from the frame
	mlt_properties properties = MLT_FRAME_PROPERTIES( frame );
    int dir    = mlt_properties_get_int( properties, "dir" );

    if(dir == 1 || dir == 2)
    {

        // Correct Width/height if necessary
        if ( *width == 0 || *height == 0 )
        {
            *width  = profile->width;
            *height = profile->height;
        }

        mlt_image_format fmt = mlt_image_rgba;

        // Now get the image
        error = mlt_frame_get_image(frame, image, format, width, height, writable);

        int owidth  = *height;
        int oheight = *width;
        if(!(dir == 1 || dir == 2))
        {
            owidth = *width;
            oheight = *height;
        }
        owidth = owidth < 0 ? 0 : owidth;
        oheight = oheight < 0 ? 0 : oheight;

        if (error == 0 && *image != NULL && owidth > 0 && oheight > 0 )
        {
            int bpp;

            mlt_log_debug( NULL, "[filter transpose] %s %dx%d -> %dx%d\n", mlt_image_format_name(*format),
                           *width, *height, owidth, oheight);

            // Subsampled YUV is messy and less precise.
            if (*format != mlt_image_rgba && frame->convert_image)
            {
                frame->convert_image( frame, image, format, fmt);
            }

            // Create the output image
            int size = mlt_image_format_size(fmt, owidth, oheight, &bpp );
            uint8_t *output = mlt_pool_alloc( size );
            if ( output )
            {
                int strides[4];
                uint8_t* planes[4];
                mlt_image_format_planes(fmt, *width, *height, (void*)*image, planes, strides);

                int o_strides[4];
                uint8_t* o_planes[4];
                mlt_image_format_planes(fmt, owidth, oheight, (void*)output, o_planes, o_strides);

                if(dir == 1)
                {
                    ARGBRotate(planes[0],strides[0],o_planes[0],o_strides[0],*width,*height,kRotate90);
                }else if(dir == 2)
                {
                    ARGBRotate(planes[0],strides[0],o_planes[0],o_strides[0],*width,*height,kRotate270);
                }


                // Now update the frame
                mlt_frame_set_image( frame, output, size, mlt_pool_release );
                *image = output;


            }

            // We should resize the alpha too
            uint8_t *alpha = mlt_frame_get_alpha( frame );
            int alpha_size = 0;
            mlt_properties_get_data( properties, "alpha", &alpha_size );
            if ( alpha && alpha_size >= ( *width * *height ) )
            {
                uint8_t *newalpha = mlt_pool_alloc( owidth * oheight );
                if ( newalpha )
                {
                    if(dir == 1)
                    {
                        RotatePlane90(alpha,*width,newalpha,*height,*width,*height);
                    }else if(dir == 2)
                    {
                        RotatePlane270(alpha,*width,newalpha,*height,*width,*height);
                    }
                    mlt_frame_set_alpha( frame, newalpha, owidth * oheight, mlt_pool_release );
                }
            }
            *width = owidth;
            *height = oheight;
        }
    }else
    {
        //mlt_image_format fmt = mlt_image_rgb24a;
        // Now get the image
        error = mlt_frame_get_image(frame, image, format, width, height, writable);
    }

	return error;
}

/** Filter processing.
*/

static mlt_frame filter_process( mlt_filter filter, mlt_frame frame )
{
    if ( mlt_properties_get_int( MLT_FILTER_PROPERTIES( filter ), "active" ) )
    {
        mlt_frame_push_service( frame, filter );
        mlt_frame_push_get_image( frame, filter_get_image );
    }
    else
    {
        mlt_properties filter_props = MLT_FILTER_PROPERTIES( filter );
        mlt_properties frame_props = MLT_FRAME_PROPERTIES( frame );
        int dir   = mlt_properties_get_int( filter_props, "dir" );
        mlt_properties_set_int( frame_props, "dir", dir );

        int width  = mlt_properties_get_int( frame_props, "meta.media.width" );
        int height = mlt_properties_get_int( frame_props, "meta.media.height" );

        mlt_properties_set_int( frame_props, "transpose.original_width", width );
        mlt_properties_set_int( frame_props, "transpose.original_height", height );

        if(dir == 2 || dir == 1)
        {
            mlt_properties_set_int( frame_props, "meta.media.width", height );
            mlt_properties_set_int( frame_props, "meta.media.height", width );
        }
    }
	return frame;
}

/** Constructor for the filter.
*/

mlt_filter filter_transpose_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
	mlt_filter filter = calloc( 1, sizeof( struct mlt_filter_s ) );
	if ( mlt_filter_init( filter, filter ) == 0 )
	{
		filter->process = filter_process;

        if ( arg )
            mlt_properties_set_int( MLT_FILTER_PROPERTIES( filter ), "active", atoi( arg ) );
	}
	return filter;
}
