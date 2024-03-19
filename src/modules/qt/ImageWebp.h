#ifndef IMAGEWEBP_H
#define IMAGEWEBP_H

#include <QtGui/qcolor.h>
#include <QtGui/qimage.h>
#include <QtGui/qimageiohandler.h>
#include <QtCore/qbytearray.h>
#include <QtCore/qsize.h>

#include "webp/decode.h"
#include "webp/demux.h"

class QWebpHandler : public QImageIOHandler
{
public:
    QWebpHandler();
    ~QWebpHandler();

public:
    QByteArray name() const override;

    bool canRead() const override;
    bool read(QImage *image) override;

    static bool canRead(QIODevice *device);

    bool write(const QImage &image) override;
    QVariant option(ImageOption option) const override;
    void setOption(ImageOption option, const QVariant &value) override;
    bool supportsOption(ImageOption option) const override;

    int imageCount() const override;
    int currentImageNumber() const override;
    QRect currentImageRect() const override;
    int loopCount() const override;
    int nextImageDelay() const override;

private:
    bool ensureScanned() const;
    bool ensureDemuxer();

private:
    enum ScanState {
        ScanError = -1,
        ScanNotScanned = 0,
        ScanSuccess = 1,
    };

    int m_quality;
    mutable ScanState m_scanState;
    WebPBitstreamFeatures m_features;
    int m_loop;
    int m_frameCount;
    QColor m_bgColor;
    QByteArray m_rawData;
    WebPData m_webpData;
    WebPDemuxer *m_demuxer;
    WebPIterator m_iter;
    QImage *m_composited;   // For animation frames composition
};

#endif // IMAGEWEBP_H
