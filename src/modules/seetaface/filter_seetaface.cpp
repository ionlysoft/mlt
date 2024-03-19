#include <framework/mlt.h>
#include <framework/mlt_log.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <seeta/FaceDetector.h>
#include <seeta/CFaceInfo.h>
#include <seeta/CStruct.h>
#include <seeta/Struct.h>

#include <QPainter>
#include <QImage>
#include <QtGlobal>

static void crop( uint8_t *src, uint8_t *dest, int bpp, int width, int height, int left, int right, int top, int bottom )
{
    int src_stride = ( width  ) * bpp;
    int dest_stride = ( width - left - right ) * bpp;
    int y      = height - top - bottom + 1;
    src += top * src_stride + left * bpp;

    while ( --y )
    {
        memcpy( dest, src, dest_stride );
        dest += dest_stride;
        src += src_stride;
    }
}

static void Blur( uint8_t *image, int bpp, int width, int height, int left, int right, int top, int bottom)
{
    int radius = 16;
    int tab[] = { 14, 10, 8, 6, 5, 5, 4, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2 };
    int alpha = 2;//(radius < 1) ? 16 : (radius > 17) ? 1 : tab[radius - 1];

    QImage img = QImage(image,width,height,QImage::Format_RGB888);

    int r1 = top;
    int r2 = height - bottom;
    int c1 = left;
    int c2 = width - right;

    int bpl = img.bytesPerLine();
    int rgba[4];
    unsigned char* p;

    int i1 = 0;
    int i2 = 2;

    //if (alphaOnly)
    //	i1 = i2 = (QSysInfo::ByteOrder == QSysInfo::BigEndian ? 0 : 3);

    for (int col = c1; col <= c2; col++) {
        p = img.scanLine(r1) + col * 3;
        for (int i = i1; i <= i2; i++)
            rgba[i] = p[i] << 4;

        p += bpl;
        for (int j = r1; j < r2; j++, p += bpl)
            for (int i = i1; i <= i2; i++)
                p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 32) >> 4;
    }

    for (int row = r1; row <= r2; row++) {
        p = img.scanLine(row) + c1 * 3;
        for (int i = i1; i <= i2; i++)
            rgba[i] = p[i] << 4;

        p += 3;
        for (int j = c1; j < c2; j++, p += 3)
            for (int i = i1; i <= i2; i++)
                p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 32) >> 4;
    }

    for (int col = c1; col <= c2; col++) {
        p = img.scanLine(r2) + col * 3;
        for (int i = i1; i <= i2; i++)
            rgba[i] = p[i] << 4;

        p -= bpl;
        for (int j = r1; j < r2; j++, p -= bpl)
            for (int i = i1; i <= i2; i++)
                p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 32) >> 4;
    }

    for (int row = r1; row <= r2; row++) {
        p = img.scanLine(row) + c2 * 3;
        for (int i = i1; i <= i2; i++)
            rgba[i] = p[i] << 4;

        p -= 3;
        for (int j = c1; j < c2; j++, p -= 3)
            for (int i = i1; i <= i2; i++)
                p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 32) >> 4;
    }
}

static void drawface( uint8_t *image, char* filename, int width, int height, int left, int right, int top, int bottom)
{
    QImage imgface(filename);
    QImage img = QImage(image,width,height,QImage::Format_RGB888);
    QPainter painter(&img);
    painter.drawImage(left,top,imgface.scaled(width-left-right,height-top-bottom, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    painter.end();
}

/** Do it :-).
*/
static int filter_get_image( mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{
    mlt_filter filter = (mlt_filter)mlt_frame_pop_service(frame);

    mlt_properties properties = MLT_FILTER_PROPERTIES(filter);
    char* file    = mlt_properties_get( properties, "logo" );
    int nX = mlt_properties_get_int(properties, "x");
    int nY = mlt_properties_get_int(properties, "y");
    int nWidth = mlt_properties_get_int(properties, "width");
    int nHeight = mlt_properties_get_int(properties, "height");
    int nType = mlt_properties_get_int(properties, "type");
    int nCanvasWidth = mlt_properties_get_int(properties, "canvaswidth");
    int nCanvasHeight = mlt_properties_get_int(properties, "canvasheight");

    *format = mlt_image_rgb;
	int error = mlt_frame_get_image( frame, image, format, width, height, 0 );

	if ( error == 0 )
	{
        float fRW = (float)(*width) / (float)nCanvasWidth;
        float fRH = (float)(*height) / (float)nCanvasHeight;
        //float fRW = 1.0;
        //float fRH = 1.0;

        seeta::ModelSetting::Device device = seeta::ModelSetting::CPU;

        int id = 0;
        seeta::ModelSetting FD_model( "./opts/fd_2_00.dat", device, id );
        seeta::FaceDetector FD(FD_model);
        FD.set(seeta::FaceDetector::PROPERTY_MIN_FACE_SIZE, 80);

        if(nWidth != 0 && nHeight != 0 && *height > 300)
        {
            QImage imgArea = QImage(nWidth*fRW,nHeight*fRH,QImage::Format_RGB888);

            crop(*image,imgArea.bits(),3,*width,*height,nX*fRW,*width - (nX + nWidth)*fRW,nY*fRH,*height - (nY + nHeight)*fRH);

            seeta::ImageData imgdate;
            imgdate.data= imgArea.bits();
            imgdate.width = nWidth * fRW;
            imgdate.height = nHeight* fRH;
            imgdate.channels = 3;

            seeta::ImageData face(0, 0, 0);
            auto faces = FD.detect(imgdate);


            for(int i = 0; i < faces.size ; i++)
            {
                SeetaRect rc  = faces.data[i].pos;

                int left = nX + rc.x;
                int top = nY + rc.y;
                int right = *width - nX  - rc.width - rc.x;
                int bottom = *height - nY - rc.height -rc.y;

                if(nType == 0)
                    Blur(*image,3,*width,*height,left,right,top,bottom);
                else
                    drawface(*image,file,*width,*height,left,right,top,bottom);
            }

        }else
        {

            seeta::ImageData imgdate;
            imgdate.data= *image;
            imgdate.height = *height;
            imgdate.width = *width;
            imgdate.channels = 3;

            seeta::ImageData face(0, 0, 0);
            auto faces = FD.detect(imgdate);

            for(int i = 0; i < faces.size ; i++)
            {
                SeetaRect rc  = faces.data[i].pos;

                int left = rc.x;
                int top = rc.y;
                int right = *width - rc.width - rc.x;
                int bottom = *height - rc.height -rc.y;

                if(nType == 0)
                    Blur(*image,3,*width,*height,left,right,top,bottom);
                else
                    drawface(*image,file,*width,*height,left,right,top,bottom);
            }
        }
	}

	return error;
}

/** Filter processing.
*/

static mlt_frame filter_process( mlt_filter filter, mlt_frame frame )
{
	// Push the frame filter
	mlt_frame_push_service( frame, filter );
	mlt_frame_push_get_image( frame, filter_get_image );

    /*
    mlt_properties filter_props = MLT_FILTER_PROPERTIES( filter );
    mlt_properties frame_props = MLT_FRAME_PROPERTIES( frame );
    char file   = mlt_properties_get_int( filter_props, "dir" );
    mlt_properties_set_int( frame_props, "dir", file );
    */

	return frame;
}

/** Constructor for the filter.
*/

extern "C" {

mlt_filter filter_seetaface_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
    mlt_filter filter = mlt_filter_new( );
    if ( filter != NULL )
    {
        filter->process = filter_process;
    }
    return filter;
}

}

