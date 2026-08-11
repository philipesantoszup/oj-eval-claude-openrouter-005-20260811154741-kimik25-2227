#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0;
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace = 0);
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace);

static inline void qoi_encode_pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                    uint8_t pr, uint8_t pg, uint8_t pb, uint8_t pa,
                                    uint8_t history[64][4]) {
    int idx = QoiColorHash(r, g, b, a);
    bool match = (history[idx][0] == r && history[idx][1] == g &&
                  history[idx][2] == b && history[idx][3] == a);

    history[idx][0] = r; history[idx][1] = g;
    history[idx][2] = b; history[idx][3] = a;

    if (match) {
        QoiWriteU8(QOI_OP_INDEX_TAG | idx);
    } else if (a == pa) {
        int dr = (int)r - (int)pr;
        int dg = (int)g - (int)pg;
        int db = (int)b - (int)pb;
        if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
            QoiWriteU8(QOI_OP_DIFF_TAG | ((dr+2)<<4) | ((dg+2)<<2) | (db+2));
        } else if (dg >= -32 && dg <= 31) {
            int dr_dg = dr - dg;
            int db_dg = db - dg;
            if (dr_dg >= -8 && dr_dg <= 7 && db_dg >= -8 && db_dg <= 7) {
                QoiWriteU8(QOI_OP_LUMA_TAG | (dg + 32));
                QoiWriteU8(((dr_dg + 8) << 4) | (db_dg + 8));
            } else {
                QoiWriteU8(QOI_OP_RGB_TAG);
                QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
            }
        } else {
            QoiWriteU8(QOI_OP_RGB_TAG);
            QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
        }
    } else {
        QoiWriteU8(QOI_OP_RGBA_TAG);
        QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b); QoiWriteU8(a);
    }
}

bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {
    QoiWriteChar('q'); QoiWriteChar('o'); QoiWriteChar('i'); QoiWriteChar('f');
    QoiWriteU32(width);
    QoiWriteU32(height);
    QoiWriteU8(channels);
    QoiWriteU8(colorspace);

    int px_num = width * height;
    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r, g, b, a;
    a = 255;
    uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
    int run = 0;

    for (int i = 0; i < px_num; ++i) {
        r = QoiReadU8(); g = QoiReadU8(); b = QoiReadU8();
        if (channels == 4) a = QoiReadU8();

        if (r == pr && g == pg && b == pb && a == pa) {
            run++;
            if (run == 62) {
                QoiWriteU8(QOI_OP_RUN_TAG | 61);
                run = 0;
            }
        } else {
            if (run > 0) {
                // Emit previous run
                QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
                run = 0;
            }
            // Encode current pixel
            qoi_encode_pixel(r, g, b, a, pr, pg, pb, pa, history);
        }

        pr = r; pg = g; pb = b; pa = a;
    }

    // Handle run at end
    if (run > 0) {
        QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
    }

    for (int i = 0; i < 8; ++i) QoiWriteU8(QOI_PADDING[i]);
    return true;
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {
    if (QoiReadChar() != 'q' || QoiReadChar() != 'o' ||
        QoiReadChar() != 'i' || QoiReadChar() != 'f') return false;

    width = QoiReadU32();
    height = QoiReadU32();
    channels = QoiReadU8();
    colorspace = QoiReadU8();

    int px_num = width * height;
    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r = 0, g = 0, b = 0, a = 255;
    int run = 0;

    for (int i = 0; i < px_num; ++i) {
        if (run > 0) {
            run--;
        } else {
            uint8_t b1 = QoiReadU8();
            if (b1 == QOI_OP_RGB_TAG) {
                r = QoiReadU8(); g = QoiReadU8(); b = QoiReadU8();
            } else if (b1 == QOI_OP_RGBA_TAG) {
                r = QoiReadU8(); g = QoiReadU8(); b = QoiReadU8(); a = QoiReadU8();
            } else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX_TAG) {
                int idx = b1 & 0x3F;
                r = history[idx][0]; g = history[idx][1];
                b = history[idx][2]; a = history[idx][3];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF_TAG) {
                r += ((b1 >> 4) & 0x03) - 2;
                g += ((b1 >> 2) & 0x03) - 2;
                b += (b1 & 0x03) - 2;
            } else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA_TAG) {
                int dg = (b1 & 0x3F) - 32;
                uint8_t b2 = QoiReadU8();
                r += ((b2 >> 4) & 0x0F) - 8 + dg;
                g += dg;
                b += (b2 & 0x0F) - 8 + dg;
            } else if ((b1 & QOI_MASK_2) == QOI_OP_RUN_TAG) {
                run = b1 & 0x3F;
            }
        }

        int idx = QoiColorHash(r, g, b, a);
        history[idx][0] = r; history[idx][1] = g;
        history[idx][2] = b; history[idx][3] = a;

        QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
        if (channels == 4) QoiWriteU8(a);
    }

    bool valid = true;
    for (int i = 0; i < 8; ++i) if (QoiReadU8() != QOI_PADDING[i]) valid = false;
    return valid;
}

#endif
