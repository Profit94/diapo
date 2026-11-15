#include "imageio.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QUrl>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QtEndian>
#include <cmath>
#include <functional>

#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include <libheif/heif.h>
#include <libraw/libraw.h>

namespace imageio {

namespace {

const QStringList &rawExtensions() {
    static const QStringList raw{"cr2", "cr3", "crw", "nef", "nrw", "arw", "srf", "sr2", "dng", "raf", "orf",
                                 "rw2", "pef", "srw", "x3f", "3fr", "erf", "kdc", "dcr", "mrw", "mef", "mos",
                                 "iiq", "rwl", "raw"};
    return raw;
}

// A lambda, so the catalog test sees only real calls with a text in them.
const auto tr = [](const char *source) { return QCoreApplication::translate("imageio", source); };

QString suffix(const QString &path) { return QFileInfo(path).suffix().toLower(); }

enum class Kind { Qt, Heif, Jxl, Raw };

// What reads this file: its contents decide, then its name.
Kind kindOf(const QString &path) {
    if (isRaw(path))
        return Kind::Raw;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray head = file.read(12);
        if (head.mid(4, 4) == "ftyp")
            return Kind::Heif;
        if (head.startsWith("\xff\x0a") || head.startsWith(QByteArray("\x00\x00\x00\x0cJXL ", 8)))
            return Kind::Jxl;
    }
    const QString ext = suffix(path);
    if (ext == "heic" || ext == "heif" || ext == "hif" || ext == "avif")
        return Kind::Heif;
    return ext == "jxl" ? Kind::Jxl : Kind::Qt;
}

QImage loadQt(QIODevice *device, int maxEdge, QString *why, bool *turned = nullptr) {
    QImageReader reader(device);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        *why = reader.errorString();
        return {};
    }
    const QSize size = reader.size();
    if (reader.format().startsWith("svg")) {
        // A drawing has no pixels of its own, so it is drawn at the size asked for.
        const int edge = maxEdge > 0 ? maxEdge : 2048;
        if (size.isValid())
            reader.setScaledSize(size.scaled(edge, edge, Qt::KeepAspectRatio));
    } else if (maxEdge > 0 && size.isValid() && qMax(size.width(), size.height()) > maxEdge) {
        reader.setScaledSize(size.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio));
    }
    if (turned)
        *turned = reader.transformation() != QImageIOHandler::TransformationNone;
    QImage image = reader.read();
    if (image.isNull())
        *why = reader.errorString();
    return image;
}

struct HeifFile {
    heif_context *context = heif_context_alloc();
    heif_image_handle *handle = nullptr;
    bool open(const QString &path, QString *why) {
        heif_error e = heif_context_read_from_file(context, QFile::encodeName(path).constData(), nullptr);
        if (e.code == heif_error_Ok)
            e = heif_context_get_primary_image_handle(context, &handle);
        if (e.code != heif_error_Ok && why)
            *why = QString::fromUtf8(e.message);
        return e.code == heif_error_Ok;
    }
    ~HeifFile() {
        if (handle)
            heif_image_handle_release(handle);
        heif_context_free(context);
    }
};

// FFmpeg's HEVC decoder is about a third faster than libde265 on large
// photos; libheif picks libde265 first, so FFmpeg is asked for by name when
// it is there, and the default is tried if it can't read the file.
const char *fastHevcDecoder() {
    static const char *chosen = [] {
        const heif_decoder_descriptor *found[16];
        const int count = heif_get_decoder_descriptors(heif_compression_HEVC, found, 16);
        for (int i = 0; i < count; ++i)
            if (qstrcmp(heif_decoder_descriptor_get_id_name(found[i]), "ffmpeg") == 0)
                return "ffmpeg";
        return static_cast<const char *>(nullptr);
    }();
    return chosen;
}

QImage loadHeif(const QString &path, QString *why) {
    HeifFile file;
    if (!file.open(path, why))
        return {};
    // The photo's tiles decode in parallel, one per core, each on a single
    // codec thread: letting FFmpeg add its own threads per tile as well put
    // four times as many threads as cores to work and starved the window
    // while it was being built.
    heif_context_set_max_decoding_threads(file.context, qMax(2, QThread::idealThreadCount()));
    heif_image *decoded = nullptr;
    heif_error e{heif_error_Ok, heif_suberror_Unspecified, ""};
    // Without transparency there is no alpha to decode, store and carry: a
    // quarter less memory, and encoders don't spend time on an alpha plane.
    const bool alpha = heif_image_handle_has_alpha_channel(file.handle);
    const heif_chroma chroma = alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;
    // Only for HEVC: AVIF is AV1, which dav1d (the default) decodes best.
    QFile head(path);
    const bool av1 = head.open(QIODevice::ReadOnly) && head.read(12).mid(8, 3) == "avi";
    if (const char *fast = av1 ? nullptr : fastHevcDecoder()) {
        heif_decoding_options *options = heif_decoding_options_alloc();
        options->decoder_id = fast;
        options->num_codec_threads = 1;
        e = heif_decode_image(file.handle, &decoded, heif_colorspace_RGB, chroma, options);
        heif_decoding_options_free(options);
    }
    if (!decoded)
        e = heif_decode_image(file.handle, &decoded, heif_colorspace_RGB, chroma, nullptr);
    if (e.code != heif_error_Ok) {
        *why = QString::fromUtf8(e.message);
        return {};
    }
    int stride = 0;
    const uint8_t *pixels = heif_image_get_plane_readonly(decoded, heif_channel_interleaved, &stride);
    const int width = heif_image_get_width(decoded, heif_channel_interleaved);
    const int height = heif_image_get_height(decoded, heif_channel_interleaved);
    QImage image(width, height, alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (!image.isNull() && pixels)
        for (int y = 0; y < height; ++y)
            memcpy(image.scanLine(y), pixels + qsizetype(y) * stride, size_t(width) * (alpha ? 4 : 3));
    heif_image_release(decoded);
    return image;
}

struct JxlRunner {
    void *runner = JxlThreadParallelRunnerCreate(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());
    ~JxlRunner() { JxlThreadParallelRunnerDestroy(runner); }
};

// With `sizeOnly`, stops at the header and returns an empty image of that size.
QImage loadJxl(const QString &path, QString *why, QSize *sizeOnly = nullptr) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *why = file.errorString();
        return {};
    }
    const QByteArray data = file.readAll();
    JxlRunner runner;
    JxlDecoder *decoder = JxlDecoderCreate(nullptr);
    JxlDecoderSetParallelRunner(decoder, JxlThreadParallelRunner, runner.runner);
    JxlDecoderSubscribeEvents(decoder, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE);
    JxlDecoderSetInput(decoder, reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()));
    JxlDecoderCloseInput(decoder);
    const JxlPixelFormat format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    QImage image;
    bool done = false;
    while (!done) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(decoder);
        if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo info;
            JxlDecoderGetBasicInfo(decoder, &info);
            // Orientations five to eight lie on their side.
            const bool sideways = info.orientation >= JXL_ORIENT_TRANSPOSE;
            const QSize size = sideways ? QSize(info.ysize, info.xsize) : QSize(info.xsize, info.ysize);
            if (sizeOnly) {
                *sizeOnly = size;
                break;
            }
            image = QImage(size, QImage::Format_RGBA8888);
            if (image.isNull())
                break;
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            JxlDecoderSetImageOutBuffer(decoder, &format, image.bits(), size_t(image.sizeInBytes()));
        } else if (status == JXL_DEC_FULL_IMAGE || status == JXL_DEC_SUCCESS) {
            done = true;
        } else {
            image = {};
            break;
        }
    }
    JxlDecoderDestroy(decoder);
    if (image.isNull() && !sizeOnly)
        *why = tr("This JPEG XL file could not be read.");
    return image;
}

QImage turnedByRaw(const QImage &image, int flip) {
    QTransform turn;
    if (flip == 3)
        turn.rotate(180);
    else if (flip == 5)
        turn.rotate(270);
    else if (flip == 6)
        turn.rotate(90);
    else
        return image;
    return image.transformed(turn);
}

// The camera's own full-size preview when the file carries one, which is what
// the camera showed and opens at once; otherwise the sensor data, developed.
QImage loadRaw(const QString &path, QString *why) {
    LibRaw raw;
    if (raw.open_file(QFile::encodeName(path).constData()) != LIBRAW_SUCCESS) {
        *why = tr("This RAW file could not be read.");
        return {};
    }
    QImage image;
    if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
        int error = 0;
        if (libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&error)) {
            if (thumb->type == LIBRAW_IMAGE_JPEG) {
                QByteArray bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(thumb->data),
                                                           qsizetype(thumb->data_size));
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::ReadOnly);
                QString ignored;
                bool turned = false;
                image = loadQt(&buffer, 0, &ignored, &turned);
                if (!image.isNull() && !turned)
                    image = turnedByRaw(image, raw.imgdata.sizes.flip);
            }
            LibRaw::dcraw_clear_mem(thumb);
        }
    }
    if (!image.isNull() && qMax(image.width(), image.height()) >= 1400)
        return image;

    raw.imgdata.params.use_camera_wb = 1;
    raw.imgdata.params.output_bps = 8;
    int error = 0;
    if (raw.unpack() == LIBRAW_SUCCESS && raw.dcraw_process() == LIBRAW_SUCCESS) {
        if (libraw_processed_image_t *developed = raw.dcraw_make_mem_image(&error)) {
            if (developed->type == LIBRAW_IMAGE_BITMAP && developed->colors == 3 && developed->bits == 8)
                image = QImage(developed->data, developed->width, developed->height, developed->width * 3,
                               QImage::Format_RGB888)
                            .copy();
            LibRaw::dcraw_clear_mem(developed);
        }
    }
    if (image.isNull())
        *why = tr("This RAW file could not be read.");
    return image;
}

// --- EXIF: a small TIFF reader, enough to find and fix a few tags ---

struct Tiff {
    QByteArray data;
    bool little = true;

    bool valid() const { return data.size() >= 8 && (data.startsWith("II*\0") || data.startsWith(QByteArray("MM\0*", 4))); }
    bool has(qsizetype at, qsizetype count) const { return at >= 0 && count >= 0 && at + count <= data.size(); }
    // Read a byte at a time: callers check the range with has() first, and
    // GCC cannot see that through a pointer into the array.
    uchar byte(qsizetype at) const { return uchar(data.at(at)); }
    quint16 u16(qsizetype at) const {
        return little ? quint16(byte(at) | byte(at + 1) << 8) : quint16(byte(at) << 8 | byte(at + 1));
    }
    quint32 u32(qsizetype at) const {
        return little ? quint32(u16(at)) | quint32(u16(at + 2)) << 16 : quint32(u16(at)) << 16 | u16(at + 2);
    }
    struct Entry {
        quint16 tag, type;
        quint32 count;
        qsizetype entry, value;
    };
    QList<Entry> directory(quint32 at) const {
        QList<Entry> entries;
        if (!has(at, 2))
            return entries;
        const int count = u16(at);
        static const int sizes[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
        for (int i = 0; i < count; ++i) {
            const qsizetype entry = qsizetype(at) + 2 + i * 12;
            if (!has(entry, 12))
                break;
            Entry e{u16(entry), u16(entry + 2), u32(entry + 4), entry, entry + 8};
            const qint64 bytes = qint64(e.type < 13 ? sizes[e.type] : 0) * e.count;
            if (bytes > 4)
                e.value = u32(entry + 8);
            if (bytes > 0 && has(e.value, qsizetype(bytes)))
                entries << e;
        }
        return entries;
    }
    QString ascii(const Entry &e) const {
        QByteArray bytes = data.mid(e.value, e.count);
        const qsizetype end = bytes.indexOf('\0');
        return QString::fromUtf8(end < 0 ? bytes : bytes.left(end)).trimmed();
    }
    double number(const Entry &e) const {
        if (e.type == 3)
            return u16(e.value);
        if (e.type == 4)
            return u32(e.value);
        if (e.type == 5 || e.type == 10) {
            const double denominator = u32(e.value + 4);
            return denominator != 0 ? u32(e.value) / denominator : 0;
        }
        return 0;
    }
};

Tiff tiffOf(const QByteArray &block) {
    Tiff tiff{block, block.startsWith("II")};
    return tiff;
}

QByteArray exifFromJpeg(const QByteArray &data) {
    qsizetype at = 2;
    while (at + 4 <= data.size() && uchar(data[at]) == 0xff) {
        const uchar marker = uchar(data[at + 1]);
        const int length = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData()) + at + 2);
        if (marker == 0xda || length < 2)
            break;
        if (marker == 0xe1 && data.mid(at + 4, 6) == QByteArray("Exif\0\0", 6))
            return data.mid(at + 10, length - 8);
        at += 2 + length;
    }
    return {};
}

// A PNG's eXIf chunk, or a WebP's EXIF one.
QByteArray exifFromChunks(const QByteArray &data, bool png) {
    qsizetype at = png ? 8 : 12;
    while (at + 8 <= data.size()) {
        const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
        const quint32 length = png ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p + 4);
        const QByteArray name = png ? data.mid(at + 4, 4) : data.mid(at, 4);
        if (name == (png ? "eXIf" : "EXIF")) {
            QByteArray block = data.mid(at + 8, length);
            return block.startsWith(QByteArray("Exif\0\0", 6)) ? block.mid(6) : block;
        }
        at += png ? 12 + qsizetype(length) : 8 + qsizetype(length) + (length & 1);
    }
    return {};
}

QByteArray exifFromHeif(const QString &path) {
    HeifFile file;
    if (!file.open(path, nullptr))
        return {};
    heif_item_id id = 0;
    if (heif_image_handle_get_list_of_metadata_block_IDs(file.handle, "Exif", &id, 1) != 1)
        return {};
    QByteArray block(qsizetype(heif_image_handle_get_metadata_size(file.handle, id)), '\0');
    if (block.size() < 8 || heif_image_handle_get_metadata(file.handle, id, block.data()).code != heif_error_Ok)
        return {};
    // Four bytes say how far in the TIFF header starts.
    const quint32 skip = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(block.constData()));
    return block.mid(4 + qsizetype(skip));
}

quint32 crc32(const QByteArray &bytes) {
    static quint32 table[256];
    static const bool built = [] {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        return true;
    }();
    Q_UNUSED(built);
    quint32 crc = 0xffffffffu;
    for (const char byte : bytes)
        crc = table[(crc ^ uchar(byte)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

QByteArray be32(quint32 value) {
    QByteArray bytes(4, '\0');
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

QByteArray le32(quint32 value) {
    QByteArray bytes(4, '\0');
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

QByteArray jpegWithExif(QByteArray data, const QByteArray &exif) {
    if (exif.size() + 8 > 0xffff || !data.startsWith("\xff\xd8"))
        return data;
    QByteArray segment("\xff\xe1", 2);
    segment += char((exif.size() + 8) >> 8);
    segment += char((exif.size() + 8) & 0xff);
    segment += QByteArray("Exif\0\0", 6) + exif;
    return data.insert(2, segment);
}

QByteArray pngWithExif(QByteArray data, const QByteArray &exif) {
    // After the header chunk, which is always first and 25 bytes long.
    if (data.size() < 33)
        return data;
    const QByteArray body = "eXIf" + exif;
    return data.insert(33, be32(quint32(exif.size())) + body + be32(crc32(body)));
}

// A WebP needs the extended header (VP8X) before it may carry EXIF.
QByteArray webpWithExif(QByteArray data, const QByteArray &exif, const QSize &size, bool alpha) {
    if (data.size() < 20 || !data.startsWith("RIFF") || data.mid(8, 4) != "WEBP")
        return data;
    if (data.mid(12, 4) == "VP8X") {
        data[20] = char(data[20] | 0x08);
    } else {
        QByteArray header("VP8X");
        header += le32(10);
        header += char(0x08 | (alpha ? 0x10 : 0));
        header += QByteArray(3, '\0');
        const quint32 w = quint32(size.width() - 1), h = quint32(size.height() - 1);
        header += le32(w).left(3) + le32(h).left(3);
        data.insert(12, header);
    }
    data += "EXIF" + le32(quint32(exif.size())) + exif;
    if (exif.size() & 1)
        data += '\0';
    data.replace(4, 4, le32(quint32(data.size() - 8)));
    return data;
}

QByteArray encodeAvif(const QImage &source, int quality, const QByteArray &exif, QString *error) {
    const bool alpha = source.hasAlphaChannel();
    const QImage image = source.convertToFormat(alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    const int channels = alpha ? 4 : 3;
    heif_context *context = heif_context_alloc();
    heif_encoder *encoder = nullptr;
    heif_image *frame = nullptr;
    heif_image_handle *handle = nullptr;
    QByteArray out;
    heif_error e = heif_context_get_encoder_for_format(context, heif_compression_AV1, &encoder);
    if (e.code == heif_error_Ok) {
        heif_encoder_set_lossy_quality(encoder, quality);
        // Faster than the encoder's default, at a few percent in size.
        heif_encoder_set_parameter_integer(encoder, "speed", 7);
        e = heif_image_create(image.width(), image.height(), heif_colorspace_RGB,
                              alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB, &frame);
    }
    if (e.code == heif_error_Ok)
        e = heif_image_add_plane(frame, heif_channel_interleaved, image.width(), image.height(), 8);
    if (e.code == heif_error_Ok) {
        int stride = 0;
        uint8_t *pixels = heif_image_get_plane(frame, heif_channel_interleaved, &stride);
        for (int y = 0; y < image.height(); ++y)
            memcpy(pixels + qsizetype(y) * stride, image.constScanLine(y), size_t(image.width()) * channels);
        e = heif_context_encode_image(context, frame, encoder, nullptr, &handle);
    }
    if (e.code == heif_error_Ok && !exif.isEmpty())
        heif_context_add_exif_metadata(context, handle, exif.constData(), int(exif.size()));
    if (e.code == heif_error_Ok) {
        heif_writer writer;
        writer.writer_api_version = 1;
        writer.write = [](heif_context *, const void *data, size_t size, void *userdata) {
            static_cast<QByteArray *>(userdata)->append(static_cast<const char *>(data), qsizetype(size));
            return heif_error{heif_error_Ok, heif_suberror_Unspecified, ""};
        };
        e = heif_context_write(context, &writer, &out);
    }
    if (e.code != heif_error_Ok) {
        out.clear();
        if (error)
            *error = tr("AVIF could not be written: %1").arg(QString::fromUtf8(e.message));
    }
    if (handle)
        heif_image_handle_release(handle);
    if (frame)
        heif_image_release(frame);
    if (encoder)
        heif_encoder_release(encoder);
    heif_context_free(context);
    return out;
}

QByteArray encodeJxl(const QImage &source, int quality, const QByteArray &exif, QString *error) {
    const bool alpha = source.hasAlphaChannel();
    const QImage image = source.convertToFormat(alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    JxlRunner runner;
    JxlEncoder *encoder = JxlEncoderCreate(nullptr);
    JxlEncoderSetParallelRunner(encoder, JxlThreadParallelRunner, runner.runner);
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = uint32_t(image.width());
    info.ysize = uint32_t(image.height());
    info.bits_per_sample = 8;
    info.num_color_channels = 3;
    info.uses_original_profile = JXL_FALSE;
    if (alpha) {
        info.num_extra_channels = 1;
        info.alpha_bits = 8;
    }
    bool ok = JxlEncoderSetBasicInfo(encoder, &info) == JXL_ENC_SUCCESS;
    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    ok = ok && JxlEncoderSetColorEncoding(encoder, &color) == JXL_ENC_SUCCESS;
    if (ok && !exif.isEmpty()) {
        // The box holds the same four-byte offset HEIF uses, then the TIFF block.
        const QByteArray box = QByteArray(4, '\0') + exif;
        JxlEncoderUseBoxes(encoder);
        JxlEncoderAddBox(encoder, "Exif", reinterpret_cast<const uint8_t *>(box.constData()), size_t(box.size()),
                         JXL_FALSE);
        JxlEncoderCloseBoxes(encoder);
    }
    JxlEncoderFrameSettings *settings = JxlEncoderFrameSettingsCreate(encoder, nullptr);
    JxlEncoderSetFrameDistance(settings, JxlEncoderDistanceFromQuality(float(quality)));
    JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, 5);
    // Rows of a QImage start on four-byte boundaries.
    const JxlPixelFormat format{uint32_t(alpha ? 4 : 3), JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 4};
    ok = ok && JxlEncoderAddImageFrame(settings, &format, image.constBits(), size_t(image.sizeInBytes()))
                   == JXL_ENC_SUCCESS;
    JxlEncoderCloseInput(encoder);
    QByteArray out(1 << 16, '\0');
    size_t written = 0;
    while (ok) {
        uint8_t *next = reinterpret_cast<uint8_t *>(out.data()) + written;
        size_t available = size_t(out.size()) - written;
        const JxlEncoderStatus status = JxlEncoderProcessOutput(encoder, &next, &available);
        written = size_t(out.size()) - available;
        if (status == JXL_ENC_NEED_MORE_OUTPUT)
            out.resize(out.size() * 2);
        else {
            ok = status == JXL_ENC_SUCCESS;
            break;
        }
    }
    JxlEncoderDestroy(encoder);
    if (!ok) {
        if (error)
            *error = tr("JPEG XL could not be written.");
        return {};
    }
    out.truncate(qsizetype(written));
    return out;
}

} // namespace

const QStringList &extensions() {
    static const QStringList all = [] {
        QSet<QString> set{"jpe", "jfif", "heic", "heif", "hif", "avif", "jxl"};
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            set << QString::fromLatin1(format).toLower();
        set.remove("pdf");
        for (const QString &raw : rawExtensions())
            set << raw;
        QStringList list(set.begin(), set.end());
        list.sort();
        return list;
    }();
    return all;
}

bool isPhoto(const QString &path) { return extensions().contains(suffix(path)); }

bool isRaw(const QString &path) { return rawExtensions().contains(suffix(path)); }

bool decodesWhole(const QString &path) { return kindOf(path) != Kind::Qt; }

// The freedesktop.org thumbnail cache: ~/.cache/thumbnails/<size>/<md5 of
// the file's URI>.png, largest first, and only one made from the file as it
// is now (its Thumb::MTime matches).
QImage cachedThumbnail(const QString &path) {
    const QFileInfo info(path);
    const QByteArray uri = QUrl::fromLocalFile(info.absoluteFilePath()).toEncoded();
    const QString name = QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex() + ".png";
    const QString cache = qEnvironmentVariable("XDG_CACHE_HOME", QDir::homePath() + "/.cache") + "/thumbnails/";
    const QString modified = QString::number(info.lastModified().toSecsSinceEpoch());
    for (const char *size : {"xx-large", "x-large", "large", "normal"}) {
        QImageReader reader(cache + size + "/" + name);
        if (!reader.canRead())
            continue;
        const QString mtime = reader.text("Thumb::MTime");
        if (!mtime.isEmpty() && mtime != modified)
            continue;
        const QImage image = reader.read();
        if (!image.isNull())
            return image;
    }
    return {};
}

QImage thumbnail(const QString &path) {
    if (kindOf(path) != Kind::Heif)
        return cachedThumbnail(path);
    HeifFile file;
    if (!file.open(path, nullptr))
        return {};
    heif_item_id id = 0;
    if (heif_image_handle_get_list_of_thumbnail_IDs(file.handle, &id, 1) != 1)
        return cachedThumbnail(path);
    heif_image_handle *small = nullptr;
    if (heif_image_handle_get_thumbnail(file.handle, id, &small).code != heif_error_Ok)
        return {};
    heif_image *decoded = nullptr;
    QImage image;
    if (heif_decode_image(small, &decoded, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr).code == heif_error_Ok) {
        int stride = 0;
        const uint8_t *pixels = heif_image_get_plane_readonly(decoded, heif_channel_interleaved, &stride);
        const int width = heif_image_get_width(decoded, heif_channel_interleaved);
        const int height = heif_image_get_height(decoded, heif_channel_interleaved);
        if (pixels && width > 0 && height > 0)
            image = QImage(pixels, width, height, stride, QImage::Format_RGBA8888).copy();
        heif_image_release(decoded);
    }
    heif_image_handle_release(small);
    return image;
}

bool isAnimated(const QString &path) {
    const QString ext = suffix(path);
    if (ext != "gif" && ext != "webp")
        return false;
    QImageReader reader(path);
    return reader.supportsAnimation() && reader.imageCount() > 1;
}

QImage load(const QString &path, int maxEdge, QString *error) {
    // Qt refuses pictures over 256 MB decoded; a large panorama is more.
    QImageReader::setAllocationLimit(2048);
    QString why;
    QImage image;
    if (!QFileInfo::exists(path)) {
        why = tr("The file is no longer there.");
    } else {
        switch (kindOf(path)) {
        case Kind::Raw:
            image = loadRaw(path, &why);
            break;
        case Kind::Heif:
            image = loadHeif(path, &why);
            break;
        case Kind::Jxl:
            image = loadJxl(path, &why);
            break;
        case Kind::Qt: {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly))
                image = loadQt(&file, maxEdge, &why);
            else
                why = file.errorString();
            break;
        }
        }
    }
    if (image.isNull()) {
        if (error)
            *error = why.isEmpty() ? tr("This file could not be read.") : why;
        return {};
    }
    if (maxEdge > 0 && qMax(image.width(), image.height()) > maxEdge)
        image = image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

QSize sizeOf(const QString &path) {
    QString ignored;
    switch (kindOf(path)) {
    case Kind::Heif: {
        HeifFile file;
        if (!file.open(path, nullptr))
            return {};
        return {heif_image_handle_get_width(file.handle), heif_image_handle_get_height(file.handle)};
    }
    case Kind::Jxl: {
        QSize size;
        loadJxl(path, &ignored, &size);
        return size;
    }
    case Kind::Raw: {
        LibRaw raw;
        if (raw.open_file(QFile::encodeName(path).constData()) != LIBRAW_SUCCESS)
            return {};
        const auto &thumb = raw.imgdata.thumbnail;
        const bool preview = qMax(thumb.twidth, thumb.theight) >= 1400;
        const QSize size = preview ? QSize(thumb.twidth, thumb.theight)
                                   : QSize(raw.imgdata.sizes.iwidth, raw.imgdata.sizes.iheight);
        const int flip = raw.imgdata.sizes.flip;
        return (flip == 5 || flip == 6) && size.width() > size.height() ? size.transposed() : size;
    }
    case Kind::Qt:
        break;
    }
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    return reader.transformation() & QImageIOHandler::TransformationRotate90 ? size.transposed() : size;
}

QString formatId(Format format) {
    switch (format) {
    case Format::Jpeg: return "jpeg";
    case Format::Png: return "png";
    case Format::Webp: return "webp";
    case Format::Avif: return "avif";
    case Format::Jxl: return "jxl";
    }
    return {};
}

QString formatName(Format format) {
    switch (format) {
    case Format::Jpeg: return "JPEG";
    case Format::Png: return "PNG";
    case Format::Webp: return "WebP";
    case Format::Avif: return "AVIF";
    case Format::Jxl: return "JPEG XL";
    }
    return {};
}

QString formatExtension(Format format) { return format == Format::Jpeg ? "jpg" : formatId(format); }

bool parseFormat(const QString &id, Format *format) {
    for (const Format f : {Format::Jpeg, Format::Png, Format::Webp, Format::Avif, Format::Jxl})
        if (formatId(f) == id.toLower() || formatExtension(f) == id.toLower()) {
            *format = f;
            return true;
        }
    return false;
}

bool formatOfPath(const QString &path, Format *format) {
    const QString ext = suffix(path);
    return parseFormat(ext == "jpe" || ext == "jfif" ? "jpeg" : ext, format);
}

QByteArray encode(const QImage &image, Format format, int quality, const QByteArray &exif, QString *error) {
    if (image.isNull()) {
        if (error)
            *error = tr("There is no picture to write.");
        return {};
    }
    if (format == Format::Avif)
        return encodeAvif(image, quality, exif, error);
    if (format == Format::Jxl)
        return encodeJxl(image, quality, exif, error);

    QImage source = image;
    if (format == Format::Jpeg && image.hasAlphaChannel()) {
        // JPEG has no transparency; see-through parts become white, as paper.
        source = QImage(image.size(), QImage::Format_RGB888);
        source.fill(Qt::white);
        QPainter painter(&source);
        painter.drawImage(0, 0, image);
    }
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, formatId(format).toLatin1());
    if (format == Format::Png) {
        writer.setQuality(15);
    } else {
        writer.setQuality(qBound(1, quality, 100));
        writer.setOptimizedWrite(true);
    }
    if (!writer.write(source)) {
        if (error)
            *error = writer.errorString();
        return {};
    }
    if (exif.isEmpty())
        return data;
    if (format == Format::Jpeg)
        return jpegWithExif(data, exif);
    if (format == Format::Png)
        return pngWithExif(data, exif);
    return webpWithExif(data, exif, source.size(), source.hasAlphaChannel());
}

QImage decode(const QByteArray &data) {
    QImage image = QImage::fromData(data);
    if (!image.isNull())
        return image;
    // AVIF and JPEG XL go through their own readers, which take files.
    QFile file(QDir::tempPath() + "/diapo-" + QString::number(QCoreApplication::applicationPid()) + "-"
               + QString::number(quintptr(&data), 16));
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(data);
    file.close();
    image = load(file.fileName());
    file.remove();
    return image;
}

QByteArray exifOf(const QString &path) {
    QByteArray block;
    const Kind kind = kindOf(path);
    if (kind == Kind::Heif) {
        block = exifFromHeif(path);
    } else if (kind == Kind::Qt) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray head = file.peek(12);
        if (head.startsWith("\xff\xd8"))
            block = exifFromJpeg(file.read(1 << 20));
        else if (head.startsWith("\x89PNG"))
            block = exifFromChunks(file.readAll(), true);
        else if (head.startsWith("RIFF") && head.mid(8, 4) == "WEBP")
            block = exifFromChunks(file.readAll(), false);
    }
    Tiff tiff = tiffOf(block);
    if (!tiff.valid())
        return {};
    // The pixels are upright already, so the tag must say so too.
    for (const Tiff::Entry &entry : tiff.directory(tiff.u32(4)))
        if (entry.tag == 0x0112 && entry.type == 3) {
            const quint16 upright = 1;
            if (tiff.little)
                qToLittleEndian(upright, reinterpret_cast<uchar *>(block.data()) + entry.value);
            else
                qToBigEndian(upright, reinterpret_cast<uchar *>(block.data()) + entry.value);
        }
    return block;
}

QList<QPair<QString, QString>> cameraFacts(const QString &path) {
    QString make, model, lens, taken;
    double exposure = 0, aperture = 0, iso = 0, focal = 0;
    if (isRaw(path)) {
        LibRaw raw;
        if (raw.open_file(QFile::encodeName(path).constData()) != LIBRAW_SUCCESS)
            return {};
        make = QString::fromUtf8(raw.imgdata.idata.make).trimmed();
        model = QString::fromUtf8(raw.imgdata.idata.model).trimmed();
        lens = QString::fromUtf8(raw.imgdata.lens.Lens).trimmed();
        exposure = raw.imgdata.other.shutter;
        aperture = raw.imgdata.other.aperture;
        iso = raw.imgdata.other.iso_speed;
        focal = raw.imgdata.other.focal_len;
        if (raw.imgdata.other.timestamp > 0)
            taken = QDateTime::fromSecsSinceEpoch(raw.imgdata.other.timestamp).toString(Qt::ISODate);
    } else {
        const Tiff tiff = tiffOf(exifOf(path));
        if (!tiff.valid())
            return {};
        quint32 details = 0;
        for (const Tiff::Entry &entry : tiff.directory(tiff.u32(4))) {
            if (entry.tag == 0x010f)
                make = tiff.ascii(entry);
            else if (entry.tag == 0x0110)
                model = tiff.ascii(entry);
            else if (entry.tag == 0x8769)
                details = tiff.u32(entry.value);
        }
        for (const Tiff::Entry &entry : tiff.directory(details)) {
            if (entry.tag == 0x829a)
                exposure = tiff.number(entry);
            else if (entry.tag == 0x829d)
                aperture = tiff.number(entry);
            else if (entry.tag == 0x8827)
                iso = tiff.number(entry);
            else if (entry.tag == 0x920a)
                focal = tiff.number(entry);
            else if (entry.tag == 0xa434)
                lens = tiff.ascii(entry);
            else if (entry.tag == 0x9003)
                taken = QDateTime::fromString(tiff.ascii(entry), "yyyy:MM:dd HH:mm:ss").toString(Qt::ISODate);
        }
    }

    QList<QPair<QString, QString>> facts;
    const QString camera = model.startsWith(make, Qt::CaseInsensitive) ? model : (make + " " + model).trimmed();
    if (!camera.isEmpty())
        facts << qMakePair(QString("camera"), camera);
    if (!lens.isEmpty())
        facts << qMakePair(QString("lens"), lens);
    if (!taken.isEmpty())
        facts << qMakePair(QString("taken"), taken);
    if (exposure > 0)
        facts << qMakePair(QString("exposure"), exposure < 1 ? QString("1/%1 s").arg(qRound(1 / exposure))
                                                             : QString("%1 s").arg(exposure, 0, 'g', 3));
    if (aperture > 0)
        facts << qMakePair(QString("aperture"), QString("f/%1").arg(aperture, 0, 'g', 2));
    if (iso > 0)
        facts << qMakePair(QString("iso"), QString("ISO %1").arg(qRound(iso)));
    if (focal > 0)
        facts << qMakePair(QString("focal"), QString("%1 mm").arg(qRound(focal)));
    return facts;
}

bool write(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    // A folder that can't hold a temporary file fails the save rather than
    // having the target truncated and rewritten in place.
    file.setDirectWriteFallback(false);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
        return true;
    if (error)
        *error = file.errorString();
    return false;
}

QString uniquePath(const QString &folder, const QString &stem, const QString &extension) {
    QString path = QDir(folder).filePath(stem + "." + extension);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QDir(folder).filePath(QString("%1-%2.%3").arg(stem).arg(n).arg(extension));
    return path;
}

} // namespace imageio
