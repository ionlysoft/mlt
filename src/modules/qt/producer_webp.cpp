/*
 * producer_image.c -- a QT/QImage based producer for MLT
 * Copyright (C) 2006-2021 Meltytech, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "ImageWebp.h"
#include <QFile>

extern "C" {

#include <framework/mlt_producer.h>
#include <framework/mlt_frame.h>
#include <framework/mlt_profile.h>
#include <framework/mlt_log.h>
#include <framework/mlt_deque.h>
#include <framework/mlt_factory.h>
#include <framework/mlt_cache.h>
#include <framework/mlt_slices.h>
#include <framework/mlt_events.h>
//#include "qimage_wrapper.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
//#include <unistd.h>
#include <ctype.h>

}

#define HH(ms) ((ms / 3600))
#define MM(ms) ((ms %3600/60))
#define SS(ms) ((ms %60))


struct producer_qwebp_s
{
    struct mlt_producer_s parent;

    QFile* file;
    QWebpHandler* image_webp;

    int image_idx;
    int qimage_idx;
    uint8_t *current_image;
    uint8_t *current_alpha;
    int current_width;
    int current_height;
    int current_frames;   //当前图像，显示帧数
    int frames;         //已经显示帧数
    int count;          //图像数
    int alpha_size;
    mlt_cache_item image_cache;
    mlt_cache_item alpha_cache;
    mlt_cache_item qimage_cache;
    void *qimage;
    mlt_image_format format;
};
typedef struct producer_qwebp_s *producer_qwebp;

static int producer_get_frame( mlt_producer parent, mlt_frame_ptr frame, int index );
static void producer_close( mlt_producer parent );

static void qwebp_delete( void *data )
{
    QImage *image = (QImage*)data;
    delete image;
    image = NULL;
#if defined(USE_KDE4)
    delete instance;
    instance = 0L;
#endif
}


static void refresh_length( mlt_properties properties, producer_qwebp self )
{
    /*if ( self->count > mlt_properties_get_int( properties, "length" ) ||
	     mlt_properties_get_int( properties, "autolength" ) )
	{
		int ttl = mlt_properties_get_int( properties, "ttl" );
		mlt_position length = self->count * ttl;
		mlt_properties_set_position( properties, "length", length );
		mlt_properties_set_position( properties, "out", length - 1 );
    }*/
}


static void on_property_changed( mlt_service owner, mlt_producer producer, mlt_event_data event_data )
{
    //const char *name = mlt_event_data_to_string(event_data);
    //if ( name && !strcmp( name, "ttl" ) )
    //	refresh_length( MLT_PRODUCER_PROPERTIES(producer), producer->child );
}

static QString IntToTime(int nTime)
{
    int nPos=(int)(nTime/1000);
    return QString("%1:%2:%3.%4").arg(HH(nPos),2,10,QChar('0')).arg(MM(nPos),2,10,QChar('0')).arg(SS(nPos),2,10,QChar('0')).arg(nTime%1000,3,10,QChar('0'));
}


int refresh_qwebp(producer_qwebp self, mlt_frame frame, int enable_caching )
{
    // Obtain properties of frame and producer
    mlt_properties properties = MLT_FRAME_PROPERTIES( frame );
    mlt_producer producer = &self->parent;
    mlt_properties producer_props = MLT_PRODUCER_PROPERTIES(producer);

    // Check if user wants us to reload the image
    if (mlt_properties_get_int( producer_props, "force_reload" ) )
    {
        self->qimage = NULL;
        self->current_image = NULL;
        mlt_properties_set_int( producer_props, "force_reload", 0 );
    }

    // Get the original position of this frame
    mlt_position position = mlt_frame_original_position( frame );
    position += mlt_producer_get_in( producer );

    if(position > self->frames && self->image_idx < self->count + 1)
        self->qimage = NULL;

    if(self->image_idx >= self->count)
    {
        delete self->image_webp;

        self->file->seek(0);
        self->image_webp = new QWebpHandler();
        if(self->image_webp)
        {
            self->image_webp->setDevice(self->file);
            self->current_frames = 0;
            self->count += self->image_webp->imageCount();
        }
    }

    if (!self->qimage)
    {
        self->current_image = NULL;
        QImage* qimage = new QImage();
        self->image_webp->read(qimage);
        self->current_frames = self->image_webp->nextImageDelay() / 1000.0 * mlt_properties_get_int( MLT_PRODUCER_PROPERTIES( producer ), "fps" );
        self->frames += self->current_frames;
        qimage->convertToFormat(QImage::Format_ARGB32);
        self->qimage = qimage;
        self->image_idx++;

        if ( !qimage->isNull( ) )
        {
            if (enable_caching )
            {
                // Register qimage for destruction and reuse
                mlt_cache_item_close( self->qimage_cache );
                mlt_service_cache_put( MLT_PRODUCER_SERVICE( producer ), "qwebp.qimage", qimage, 0, ( mlt_destructor )qwebp_delete );
                self->qimage_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.qimage" );
            }
            else
            {
                // Ensure original image data will be deleted
                mlt_properties_set_data( producer_props, "qwebp.qimage", qimage, 0, ( mlt_destructor )qwebp_delete, NULL );
            }

            // Store the width/height of the qimage
            self->current_width = qimage->width( );
            self->current_height = qimage->height( );

            mlt_events_block( producer_props, NULL );
            mlt_properties_set_int( producer_props, "meta.media.width", self->current_width );
            mlt_properties_set_int( producer_props, "meta.media.height", self->current_height );
            mlt_events_unblock( producer_props, NULL );
        }
        else
        {
            delete qimage;
            self->qimage = NULL;
        }
    }

    mlt_properties_set_int( properties, "width", self->current_width );
    mlt_properties_set_int( properties, "height", self->current_height );

    return 0;
}

void refresh_webp(producer_qwebp self, mlt_frame frame, mlt_image_format format, int width, int height, int enable_caching )
{
    // Obtain properties of frame and producer
    mlt_properties properties = MLT_FRAME_PROPERTIES( frame );
    mlt_producer producer = &self->parent;

    // Get index and qimage
    int image_idx = refresh_qwebp( self, frame, enable_caching);

    // optimization for subsequent iterations on single picture
    if (!enable_caching || width != self->current_width || height != self->current_height )
        self->current_image = NULL;

    // If we have a qimage and need a new scaled image
    if ( self->qimage && ( !self->current_image || ( format != mlt_image_none && format != mlt_image_movit && format != self->format ) ) )
    {
        QString interps = mlt_properties_get( properties, "rescale.interp" );
        bool interp = ( interps != "nearest" ) && ( interps != "none" );
        QImage *qimage = static_cast<QImage*>( self->qimage );
        int has_alpha = qimage->hasAlphaChannel();
        QImage::Format qimageFormat = has_alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32;

        // Note - the original qimage is already safe and ready for destruction
        if ( enable_caching && qimage->format() != qimageFormat )
        {
            QImage temp = qimage->convertToFormat( qimageFormat );
            qimage = new QImage( temp );
            self->qimage = qimage;
            mlt_cache_item_close( self->qimage_cache );
            mlt_service_cache_put( MLT_PRODUCER_SERVICE( producer ), "qwebp.qimage", qimage, 0, ( mlt_destructor )qwebp_delete );
            self->qimage_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.qimage" );
        }
        QImage scaled = interp? qimage->scaled( QSize( width, height ), Qt::IgnoreAspectRatio, Qt::SmoothTransformation ) :
            qimage->scaled( QSize(width, height) );

        // Store width and height
        self->current_width = width;
        self->current_height = height;

        // Allocate/define image
        self->current_alpha = NULL;
        self->alpha_size = 0;

        // Convert scaled image to target format (it might be premultiplied after scaling).
        scaled = scaled.convertToFormat( qimageFormat );

        // Copy the image
        int image_size;
#if QT_VERSION >= 0x050200
        if ( has_alpha )
        {
            self->format = mlt_image_rgba;
            scaled = scaled.convertToFormat( QImage::Format_RGBA8888 );
            image_size = mlt_image_format_size(self->format, width, height, NULL);
            self->current_image = ( uint8_t * )mlt_pool_alloc( image_size );
            memcpy( self->current_image, scaled.constBits(), scaled.sizeInBytes());
        }
        else
        {
            self->format = mlt_image_rgb;
            scaled = scaled.convertToFormat( QImage::Format_RGB888 );
            image_size = mlt_image_format_size(self->format, width, height, NULL);
            self->current_image = ( uint8_t * )mlt_pool_alloc( image_size );
            for (int y = 0; y < height; y++) {
                QRgb *values = reinterpret_cast<QRgb *>(scaled.scanLine(y));
                memcpy( &self->current_image[3 * y * width], values, 3 * width);
            }
        }
#else
        self->format = has_alpha? mlt_image_rgba : mlt_image_rgb;
        image_size = mlt_image_format_size( self->format, self->current_width, self->current_height, NULL );
        self->current_image = ( uint8_t * )mlt_pool_alloc( image_size );
        int y = self->current_height + 1;
        uint8_t *dst = self->current_image;
        if (has_alpha) {
            while ( --y )
            {
                QRgb *src = (QRgb*) scaled.scanLine( self->current_height - y );
                int x = self->current_width + 1;
                while ( --x )
                {
                    *dst++ = qRed(*src);
                    *dst++ = qGreen(*src);
                    *dst++ = qBlue(*src);
                    *dst++ = qAlpha(*src);
                    ++src;
                }
            }
        } else {
            while ( --y )
            {
                QRgb *src = (QRgb*) scaled.scanLine( self->current_height - y );
                int x = self->current_width + 1;
                while ( --x )
                {
                    *dst++ = qRed(*src);
                    *dst++ = qGreen(*src);
                    *dst++ = qBlue(*src);
                    ++src;
                }
            }
        }
#endif

        // Convert image to requested format
        if ( format != mlt_image_none && format != mlt_image_movit && format != self->format && enable_caching )
        {
            uint8_t *buffer = NULL;

            // First, set the image so it can be converted when we get it
            mlt_frame_replace_image( frame, self->current_image, self->format, width, height );
            mlt_frame_set_image( frame, self->current_image, image_size, mlt_pool_release );

            // get_image will do the format conversion
            mlt_frame_get_image( frame, &buffer, &format, &width, &height, 0 );

            // cache copies of the image and alpha buffers
            if ( buffer )
            {
                self->current_width = width;
                self->current_height = height;
                self->format = format;
                image_size = mlt_image_format_size( format, width, height, NULL );
                self->current_image = (uint8_t*) mlt_pool_alloc( image_size );
                memcpy( self->current_image, buffer, image_size );
            }
            if ( ( buffer = (uint8_t*) mlt_properties_get_data( properties, "alpha", &self->alpha_size ) ) )
            {
                if ( !self->alpha_size )
                    self->alpha_size = self->current_width * self->current_height;
                self->current_alpha = (uint8_t*) mlt_pool_alloc( self->alpha_size );
                memcpy( self->current_alpha, buffer, self->alpha_size );
            }
        }

        if ( enable_caching )
        {
            // Update the cache
            mlt_cache_item_close( self->image_cache );
            mlt_service_cache_put( MLT_PRODUCER_SERVICE( producer ), "qwebp.image", self->current_image, image_size, mlt_pool_release );
            self->image_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.image" );
            mlt_cache_item_close( self->alpha_cache );
            self->alpha_cache = NULL;
            if ( self->current_alpha )
            {
                mlt_service_cache_put( MLT_PRODUCER_SERVICE( producer ), "qwebp.alpha", self->current_alpha, self->alpha_size, mlt_pool_release );
                self->alpha_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.alpha" );
            }
        }
    }

    // Set width/height of frame
    mlt_properties_set_int( properties, "width", self->current_width );
    mlt_properties_set_int( properties, "height", self->current_height );
}

extern "C" {

mlt_producer producer_qwebp_init(mlt_profile profile, mlt_service_type type, const char *id, char *filename )
{
    producer_qwebp self = (producer_qwebp)calloc( 1, sizeof(struct producer_qwebp_s));
    if ( self != NULL && mlt_producer_init(&self->parent, self) == 0 )
    {
        mlt_producer producer = &self->parent;

        // Get the properties interface
        mlt_properties properties = MLT_PRODUCER_PROPERTIES(&self->parent);

        // Callback registration
        producer->get_frame = producer_get_frame;
        producer->close = ( mlt_destructor )producer_close;

        // Set the default properties
        mlt_properties_set( properties, "resource", filename );
        mlt_properties_set_int( properties, "fps", profile->frame_rate_num / profile->frame_rate_den);
        mlt_properties_set_int( properties, "aspect_ratio", 1 );
        mlt_properties_set_int( properties, "progressive", 1 );
        mlt_properties_set_int( properties, "seekable", 1 );

        self->file = new QFile();
        self->file->setFileName(filename);
        if(self->file->open(QIODevice::ReadOnly))
        {
            self->image_webp = new QWebpHandler();
            if(self->image_webp)
            {
                self->image_webp->setDevice(self->file);
                self->current_frames = 0;
                self->frames = 0;
                self->image_idx = 0;
                self->count = self->image_webp->imageCount();
            }
        }

        mlt_frame frame = mlt_frame_init( MLT_PRODUCER_SERVICE( producer ) );
        if ( frame )
        {
            mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );
            mlt_properties_set_data( frame_properties, "producer_qwebp", self, 0, NULL, NULL );
            mlt_frame_set_position( frame, mlt_producer_position( producer ) );
            int enable_caching = 1;
            refresh_qwebp( self, frame, enable_caching );
            if ( enable_caching )
            {
                mlt_cache_item_close( self->qimage_cache );
            }
            mlt_frame_close( frame );
        }

        if ( self->current_width == 0 )
        {
            producer_close( producer );
            producer = NULL;
        }
        else
        {
            mlt_events_listen( properties, self, "property-changed", (mlt_listener) on_property_changed );
        }
        return producer;
    }
    free( self );
    return NULL;
}

}

static int producer_get_image( mlt_frame frame, uint8_t **buffer, mlt_image_format *format, int *width, int *height, int writable )
{
	int error = 0;
	
	// Obtain properties of frame and producer
	mlt_properties properties = MLT_FRAME_PROPERTIES( frame );
    producer_qwebp self = (producer_qwebp)mlt_properties_get_data( properties, "producer_qwebp", NULL );
	mlt_producer producer = &self->parent;

	// Use the width and height suggested by the rescale filter because we can do our own scaling.
	if ( mlt_properties_get_int( properties, "rescale_width" ) > 0 )
		*width = mlt_properties_get_int( properties, "rescale_width" );
	if ( mlt_properties_get_int( properties, "rescale_height" ) > 0 )
		*height = mlt_properties_get_int( properties, "rescale_height" );
	mlt_service_lock( MLT_PRODUCER_SERVICE( &self->parent ) );
    int enable_caching = (mlt_properties_get_int( MLT_PRODUCER_PROPERTIES( producer ), "fps" ) > 1 );

	// Refresh the image
	if ( enable_caching )
	{
        self->qimage_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.qimage" );
		self->qimage = mlt_cache_item_data( self->qimage_cache, NULL );
        self->image_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.image" );
        self->current_image = (uint8_t *)mlt_cache_item_data( self->image_cache, NULL );
        self->alpha_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.alpha" );
        self->current_alpha = (uint8_t *)mlt_cache_item_data( self->alpha_cache, &self->alpha_size );
	}
    refresh_webp( self, frame, *format, *width, *height, enable_caching );

	// Get width and height (may have changed during the refresh)
	*width = mlt_properties_get_int( properties, "width" );
	*height = mlt_properties_get_int( properties, "height" );
	*format = self->format;

	// NB: Cloning is necessary with this producer (due to processing of images ahead of use)
	// The fault is not in the design of mlt, but in the implementation of the qimage producer...
	if ( self->current_image )
	{
		int image_size = mlt_image_format_size( self->format, self->current_width, self->current_height, NULL );
		if ( enable_caching )
		{
			// Clone the image and the alpha
            uint8_t *image_copy = (uint8_t *)mlt_pool_alloc( image_size );
			memcpy( image_copy, self->current_image, image_size );
			// Now update properties so we free the copy after
			mlt_frame_set_image( frame, image_copy, image_size, mlt_pool_release );
			// We're going to pass the copy on
			*buffer = image_copy;
			mlt_log_debug( MLT_PRODUCER_SERVICE( &self->parent ), "%dx%d (%s)\n",
				self->current_width, self->current_height, mlt_image_format_name( *format ) );
			// Clone the alpha channel
			if ( self->current_alpha )
			{
				if ( !self->alpha_size )
					self->alpha_size = self->current_width * self->current_height;
                uint8_t * alpha_copy = (uint8_t *)mlt_pool_alloc( self->alpha_size );
				memcpy( alpha_copy, self->current_alpha, self->alpha_size );
				mlt_frame_set_alpha( frame, alpha_copy, self->alpha_size, mlt_pool_release );
			}
		}
		else
		{
			// For image sequences with ttl = 1 we recreate self->current_image on each frame, no need to clone
			mlt_frame_set_image( frame, self->current_image, image_size, mlt_pool_release );
			*buffer = self->current_image;
			if ( self->current_alpha )
			{
				if ( !self->alpha_size )
					self->alpha_size = self->current_width * self->current_height;
				mlt_frame_set_alpha( frame, self->current_alpha, self->alpha_size, mlt_pool_release );
			}
		}
	}
	else
	{
		error = 1;
	}

	if ( enable_caching )
	{
		// Release references and locks
		mlt_cache_item_close( self->qimage_cache );
		mlt_cache_item_close( self->image_cache );
		mlt_cache_item_close( self->alpha_cache );
	}
	mlt_service_unlock( MLT_PRODUCER_SERVICE( &self->parent ) );

	return error;
}

static int producer_get_frame( mlt_producer producer, mlt_frame_ptr frame, int index )
{
	// Get the real structure for this producer
    producer_qwebp self = (producer_qwebp)producer->child;

	// Fetch the producers properties
	mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES( producer );

    //if ( self->filenames == NULL && mlt_properties_get( producer_properties, "resource" ) != NULL )
    //	load_filenames( self, producer_properties );

	// Generate a frame
	*frame = mlt_frame_init( MLT_PRODUCER_SERVICE( producer ) );

    if ( *frame != NULL)
	{
		// Obtain properties of frame and producer
		mlt_properties properties = MLT_FRAME_PROPERTIES( *frame );

		// Set the producer on the frame properties
        mlt_properties_set_data( properties, "producer_qwebp", self, 0, NULL, NULL );

		// Update timecode on the frame we're creating
		mlt_frame_set_position( *frame, mlt_producer_position( producer ) );

		// Refresh the image
        if (mlt_properties_get_int( producer_properties, "fps" ) > 1 )
		{
            self->qimage_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qwebp.qimage" );
			self->qimage = mlt_cache_item_data( self->qimage_cache, NULL );
            refresh_qwebp( self, *frame, 1 );
			mlt_cache_item_close( self->qimage_cache );
		}

		// Set producer-specific frame properties
		mlt_properties_set_int( properties, "progressive", mlt_properties_get_int( producer_properties, "progressive" ) );
		double force_ratio = mlt_properties_get_double( producer_properties, "force_aspect_ratio" );
		if ( force_ratio > 0.0 )
			mlt_properties_set_double( properties, "aspect_ratio", force_ratio );
		else
			mlt_properties_set_double( properties, "aspect_ratio", mlt_properties_get_double( producer_properties, "aspect_ratio" ) );

		// Push the get_image method
		mlt_frame_push_get_image( *frame, producer_get_image );
	}

	// Calculate the next timecode
	mlt_producer_prepare_next( producer );

	return 0;
}


static void producer_close( mlt_producer parent )
{
    producer_qwebp self = (producer_qwebp)parent->child;
	parent->close = NULL;
	mlt_service_cache_purge( MLT_PRODUCER_SERVICE(parent) );
	mlt_producer_close( parent );

    self->file->close();
    delete self->image_webp;
    delete self->file;

	free( self );
}

