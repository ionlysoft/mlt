#include "ImageWebp.h"
#include "webp/encode.h"
#include "webp/mux.h"
#include <qcolor.h>
#include <qimage.h>
#include <qdebug.h>
#include <qpainter.h>
#include <qvariant.h>

enum {
  METADATA_ICC  = (1 << 0),
  METADATA_XMP  = (1 << 1),
  METADATA_ALL  = METADATA_ICC | METADATA_XMP
};

static const int riffHeaderSize = 12; // RIFF_HEADER_SIZE from webp/format_constants.h

QWebpHandler::QWebpHandler() :
    m_quality(75),
    m_scanState(ScanNotScanned),
    m_features(),
    m_loop(0),
    m_frameCount(0),
    m_demuxer(NULL),
    m_composited(NULL)
{
    memset(&m_iter, 0, sizeof(m_iter));
}

QWebpHandler::~QWebpHandler()
{
    WebPDemuxReleaseIterator(&m_iter);
    WebPDemuxDelete(m_demuxer);
    delete m_composited;
}

bool QWebpHandler::canRead() const
{
    if (m_scanState == ScanNotScanned && !canRead(device()))
        return false;

    if (m_scanState != ScanError) {
        setFormat(QByteArrayLiteral("webp"));

        if (m_features.has_animation && m_iter.frame_num >= m_frameCount)
            return false;

        return true;
    }
    return false;
}

bool QWebpHandler::canRead(QIODevice *device)
{
    if (!device) {
        qWarning("QWebpHandler::canRead() called with no device");
        return false;
    }

    QByteArray header = device->peek(riffHeaderSize);
    return header.startsWith("RIFF") && header.endsWith("WEBP");
}

bool QWebpHandler::ensureScanned() const
{
    if (m_scanState != ScanNotScanned)
        return m_scanState == ScanSuccess;

    m_scanState = ScanError;

    if (device()->isSequential()) {
        qWarning() << "Sequential devices are not supported";
        return false;
    }

    qint64 oldPos = device()->pos();
    device()->seek(0);

    QWebpHandler *that = const_cast<QWebpHandler *>(this);
    QByteArray header = device()->peek(sizeof(WebPBitstreamFeatures));
    if (WebPGetFeatures((const uint8_t*)header.constData(), header.size(), &(that->m_features)) == VP8_STATUS_OK) {
        if (m_features.has_animation) {
            // For animation, we have to read and scan whole file to determine loop count and images count
            device()->seek(oldPos);

            if (that->ensureDemuxer()) {
                that->m_loop = WebPDemuxGetI(m_demuxer, WEBP_FF_LOOP_COUNT);
                that->m_frameCount = WebPDemuxGetI(m_demuxer, WEBP_FF_FRAME_COUNT);
                that->m_bgColor = QColor::fromRgba(QRgb(WebPDemuxGetI(m_demuxer, WEBP_FF_BACKGROUND_COLOR)));

                that->m_composited = new QImage(that->m_features.width, that->m_features.height, QImage::Format_ARGB32);
                if (that->m_features.has_alpha)
                    that->m_composited->fill(Qt::transparent);

                // We do not reset device position since we have read in all data
                m_scanState = ScanSuccess;
                return true;
            }
        } else {
            m_scanState = ScanSuccess;
        }
    }

    device()->seek(oldPos);

    return m_scanState == ScanSuccess;
}

bool QWebpHandler::ensureDemuxer()
{
    if (m_demuxer)
        return true;

    m_rawData = device()->readAll();
    m_webpData.bytes = reinterpret_cast<const uint8_t *>(m_rawData.constData());
    m_webpData.size = m_rawData.size();

    m_demuxer = WebPDemux(&m_webpData);
    if (m_demuxer == NULL)
        return false;

    return true;
}

bool QWebpHandler::read(QImage *image)
{
    if (!ensureScanned() || device()->isSequential() || !ensureDemuxer())
        return false;

    if (m_iter.frame_num == 0) {
        // Go to first frame
        if (!WebPDemuxGetFrame(m_demuxer, 1, &m_iter))
            return false;
    } else {
        // Go to next frame
        if (!WebPDemuxNextFrame(&m_iter))
            return false;
    }

    WebPBitstreamFeatures features;
    VP8StatusCode status = WebPGetFeatures(m_iter.fragment.bytes, m_iter.fragment.size, &features);
    if (status != VP8_STATUS_OK)
        return false;

    QImage::Format format = m_features.has_alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32;
    QImage frame(m_iter.width, m_iter.height, format);
    uint8_t *output = frame.bits();
    size_t output_size = frame.sizeInBytes();
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    if (!WebPDecodeBGRAInto(
        reinterpret_cast<const uint8_t*>(m_iter.fragment.bytes), m_iter.fragment.size,
        output, output_size, frame.bytesPerLine()))
#else
    if (!WebPDecodeARGBInto(
        reinterpret_cast<const uint8_t*>(m_iter.fragment.bytes), m_iter.fragment.size,
        output, output_size, frame.bytesPerLine()))
#endif
        return false;

    if (!m_features.has_animation) {
        // Single image
        *image = frame;
    } else {
        // Animation
        QPainter painter(m_composited);
        if (m_features.has_alpha && (m_iter.dispose_method == WEBP_MUX_DISPOSE_BACKGROUND ||
                m_iter.blend_method == WEBP_MUX_NO_BLEND))
            m_composited->fill(Qt::transparent);
        painter.drawImage(currentImageRect(), frame);

        *image = *m_composited;
    }

    return true;
}

static int pictureWriter(const quint8 *data, size_t data_size, const WebPPicture *const pic)
{
    QIODevice *io = reinterpret_cast<QIODevice*>(pic->custom_ptr);

    return data_size ? ((quint64)(io->write((const char*)data, data_size)) == data_size) : 1;
}

bool QWebpHandler::write(const QImage &image)
{
    if (image.isNull()) {
        qWarning() << "source image is null.";
        return false;
    }

    QImage srcImage = image;
    bool alpha = srcImage.hasAlphaChannel();
    QImage::Format newFormat = alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    if (srcImage.format() != newFormat)
        srcImage = srcImage.convertToFormat(newFormat);

    WebPPicture picture;
    WebPConfig config;

    if (!WebPPictureInit(&picture) || !WebPConfigInit(&config)) {
        qWarning() << "failed to init webp picture and config";
        return false;
    }

    picture.width = srcImage.width();
    picture.height = srcImage.height();
    picture.use_argb = 1;
    bool failed = false;
    if (alpha)
        failed = !WebPPictureImportRGBA(&picture, srcImage.bits(), srcImage.bytesPerLine());
    else
        failed = !WebPPictureImportRGB(&picture, srcImage.bits(), srcImage.bytesPerLine());

    if (failed) {
        qWarning() << "failed to import image data to webp picture.";
        WebPPictureFree(&picture);
        return false;
    }

    int reqQuality = m_quality < 0 ? 75 : qMin(m_quality, 100);
    if (reqQuality < 100) {
        config.lossless = 0;
        config.quality = reqQuality;
    } else {
        config.lossless = 1;
        config.quality = 70;  // For lossless, specifies compression effort; 70 is libwebp default
    }
    config.alpha_quality = config.quality;
    picture.writer = pictureWriter;
    picture.custom_ptr = device();

    if (!WebPEncode(&config, &picture)) {
        qWarning() << "failed to encode webp picture, error code: " << picture.error_code;
        WebPPictureFree(&picture);
        return false;
    }

    WebPPictureFree(&picture);

    return true;
}

QVariant QWebpHandler::option(ImageOption option) const
{
    if (!supportsOption(option) || !ensureScanned())
        return QVariant();

    switch (option) {
    case Quality:
        return m_quality;
    case Size:
        return QSize(m_features.width, m_features.height);
    case Animation:
        return m_features.has_animation;
    case BackgroundColor:
        return m_bgColor;
    default:
        return QVariant();
    }
}

void QWebpHandler::setOption(ImageOption option, const QVariant &value)
{
    switch (option) {
    case Quality:
        m_quality = value.toInt();
        return;
    default:
        break;
    }
    QImageIOHandler::setOption(option, value);
}

bool QWebpHandler::supportsOption(ImageOption option) const
{
    return option == Quality
        || option == Size
        || option == Animation
        || option == BackgroundColor;
}

QByteArray QWebpHandler::name() const
{
    return QByteArrayLiteral("webp");
}

int QWebpHandler::imageCount() const
{
    if (!ensureScanned())
        return 0;

    if (!m_features.has_animation)
        return 1;

    return m_frameCount;
}

int QWebpHandler::currentImageNumber() const
{
    if (!ensureScanned() || !m_features.has_animation)
        return 0;

    // Frame number in WebP starts from 1
    return m_iter.frame_num - 1;
}

QRect QWebpHandler::currentImageRect() const
{
    if (!ensureScanned())
        return QRect();

    return QRect(m_iter.x_offset, m_iter.y_offset, m_iter.width, m_iter.height);
}

int QWebpHandler::loopCount() const
{
    if (!ensureScanned() || !m_features.has_animation)
        return 0;

    // Loop count in WebP starts from 0
    return m_loop - 1;
}

int QWebpHandler::nextImageDelay() const
{
    if (!ensureScanned() || !m_features.has_animation)
        return 0;

    return m_iter.duration;
}

/*
bool CImageBase::LoadWebp(const TCHAR* filename)
{
    QFile file;
    file.setFileName(QSTRING_FROM_STRW(filename));
    file.open(QIODevice::ReadOnly);

    QWebpHandler webp;
    webp.setDevice(&file);

    //支持webp动画
    int nFrames = webp.imageCount();
    if(nFrames > 1)
    {
        ppFrames = new CImageBase*[nFrames];

        for(int i = 0; i < nFrames; i++)
        {
            ppFrames[i] = new CImageBase();
            QImage image;
            webp.read(&image);
            image.convertToFormat(QImage::Format_ARGB32);

            ppFrames[i]->LoadQImage(&image);

            if(i == 0)
            {
                Copy(*ppFrames[i]);
                info.nNumFrames = nFrames;
                SetRetreiveAllFrames(true);
            }
            ppFrames[i]->SetFrameDelay(webp.nextImageDelay()/10);
        }
        file.close();
    }else {
        QImage image;
        webp.read(&image);
        image.convertToFormat(QImage::Format_ARGB32);
        LoadQImage(&image);
        file.close();
    }

    return true;
}

static void ClearRectangle(WebPPicture* const picture,
                           int left, int top, int width, int height) {
  int i, j;
  const size_t stride = picture->argb_stride;
  uint32_t* dst = picture->argb + top * stride + left;
  for (j = 0; j < height; ++j, dst += stride) {
    for (i = 0; i < width; ++i) dst[i] = 0;
  }
}

bool CImageBase::SaveWebp(const TCHAR* filename)
{
    if(info.nNumFrames <= 1)
    {
        return SaveImage_FreeImage(this, filename, GetJpegQuality());
    }else {

        QFile file;
        bool bRes = true;

        WebPMuxError err = WEBP_MUX_OK;
        int frame_duration = 0;
        int frame_timestamp = 0;
        WebPPicture frame;
        WebPAnimEncoder* enc = NULL;
        WebPAnimEncoderOptions enc_options;
        WebPConfig config;

        WebPData webp_data;

        int keep_metadata = METADATA_XMP;  // ICC not output by default.
        WebPData icc_data;
        int stored_icc = 0;         // Whether we have already stored an ICC profile.
        WebPData xmp_data;
        int stored_xmp = 0;         // Whether we have already stored an XMP profile.
        int loop_count = 0;         // default: infinite
        WebPMux* mux = NULL;

        int default_kmin = 1;  // Whether to use default kmin value.
        int default_kmax = 1;

        if (!WebPConfigInit(&config) || !WebPAnimEncoderOptionsInit(&enc_options) ||
            !WebPPictureInit(&frame)) {
          fprintf(stderr, "Error! Version mismatch!\n");
          return false;
        }

        WebPDataInit(&webp_data);
        WebPDataInit(&icc_data);
        WebPDataInit(&xmp_data);


        config.lossless = 0;
        config.quality = GetJpegQualityF();
        //config.thread_level++;

        enc_options.allow_mixed = 0;
        enc_options.minimize_size = 0;
        enc_options.verbose = 0;

        default_kmax = 1;
        default_kmin = 1;
        // Appropriate default kmin, kmax values for lossy and lossless.
        if (default_kmin)
            enc_options.kmin = config.lossless ? 9 : 3;
        if (default_kmax)
            enc_options.kmax = config.lossless ? 17 : 5;

        int framecount = GetNumFrames();

        if (!WebPValidateConfig(&config)) {
          fprintf(stderr, "Error! Invalid configuration.\n");
          bRes = false;
          goto WEBPEnd;
        }

        frame.width = GetWidth();
        frame.height = GetHeight();
        frame.use_argb = 1;
        if (!WebPPictureAlloc(&frame))
        {
            bRes = false;
            goto WEBPEnd;
        }

        // Initialize encoder.
        enc = WebPAnimEncoderNew(frame.width, frame.height,
                                 &enc_options);
        if (enc == NULL)
        {
            bRes = false;
            goto WEBPEnd;
        }

        for(int i = 0; i < framecount; i++)
        {
            frame_duration = GetFrame(i)->GetFrameDelay()*10;

            //生成一帧数据
            WebPPicture sub_image;
            if (!WebPPictureView(&frame, 0, 0,frame.width, frame.height, &sub_image))
            {
                bRes = false;
                goto WEBPEnd;
            }
            GetFrame(i)->Flip();
            if (GetFrame(i)->GetBpp() <= 8)
                GetFrame(i)->IncreaseBpp(24);
            GetFrame(i)->Convert24To32();

            memcpy(sub_image.argb,GetFrame(i)->GetBits()
                   ,GetFrame(i)->GetEffWidth() * GetFrame(i)->GetHeight());

            WebPPictureFree(&sub_image);

            //webp编码
            if (!WebPAnimEncoderAdd(enc, &frame, frame_timestamp, &config))
            {
                bRes = false;
                goto WEBPEnd;
            }

            GetFrame(i)->Flip();

            if (frame_duration <= 10)
                frame_duration = 100;

            frame_timestamp += frame_duration;
            frame_duration = 0;

        };

        // Last NULL frame.
        if (!WebPAnimEncoderAdd(enc, NULL, frame_timestamp, NULL)) {
          fprintf(stderr, "Error flushing WebP muxer.\n");
          fprintf(stderr, "%s\n", WebPAnimEncoderGetError(enc));
        }

        if (!WebPAnimEncoderAssemble(enc, &webp_data)) {
          fprintf(stderr, "%s\n", WebPAnimEncoderGetError(enc));
          bRes = false;
          goto WEBPEnd;
        }

        //添加循环次数或icc或xmp
        if (stored_icc || stored_xmp) {
          // Re-mux to add loop count and/or metadata as needed.
          mux = WebPMuxCreate(&webp_data, 1);
          if (mux == NULL) {
            fprintf(stderr, "ERROR: Could not re-mux to add loop count/metadata.\n");
            goto WEBPEnd;
          }
          WebPDataClear(&webp_data);

          if (loop_count > 0) {  // Update loop count.
            WebPMuxAnimParams new_params;
            err = WebPMuxGetAnimationParams(mux, &new_params);
            if (err != WEBP_MUX_OK) {
                goto WEBPEnd;
            }
            new_params.loop_count = loop_count;
            err = WebPMuxSetAnimationParams(mux, &new_params);
            if (err != WEBP_MUX_OK) {
                goto WEBPEnd;
            }
          }

          if (stored_icc) {   // Add ICCP chunk.
            err = WebPMuxSetChunk(mux, "ICCP", &icc_data, 1);
            if (err != WEBP_MUX_OK) {
              goto WEBPEnd;
            }
          }

          if (stored_xmp) {   // Add XMP chunk.
            err = WebPMuxSetChunk(mux, "XMP ", &xmp_data, 1);
            if (err != WEBP_MUX_OK) {
              goto WEBPEnd;
            }
          }

          err = WebPMuxAssemble(mux, &webp_data);
          if (err != WEBP_MUX_OK) {
            goto WEBPEnd;
          }
        }

        //生成webp文件
        file.setFileName(QSTRING_FROM_STRW(filename));
        file.open(QIODevice::WriteOnly);
        file.write((const char*)(webp_data.bytes),webp_data.size);
        file.close();

WEBPEnd:
        WebPDataClear(&icc_data);
        WebPDataClear(&xmp_data);
        WebPMuxDelete(mux);
        WebPDataClear(&webp_data);
        WebPPictureFree(&frame);
        WebPAnimEncoderDelete(enc);
        return bRes;
    }
}
*/
