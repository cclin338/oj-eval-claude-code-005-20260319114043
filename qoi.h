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

/**
 * @brief encode the raw pixel data of an image to qoi format.
 *
 * @param[in] width image width in pixels
 * @param[in] height image height in pixels
 * @param[in] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[in] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace = 0);

/**
 * @brief decode the qoi format of an image to raw pixel data
 *
 * @param[out] width image width in pixels
 * @param[out] height image height in pixels
 * @param[out] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[out] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace);


bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {

    // qoi-header part

    // write magic bytes "qoif"
    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    // write image width
    QoiWriteU32(width);
    // write image height
    QoiWriteU32(height);
    // write channel number
    QoiWriteU8(channels);
    // write color space specifier
    QoiWriteU8(colorspace);

    /* qoi-data part */
    int run = 0;
    int px_num = width * height;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r, g, b, a;
    a = 255u;
    uint8_t pre_r, pre_g, pre_b, pre_a;
    pre_r = 0u;
    pre_g = 0u;
    pre_b = 0u;
    pre_a = 255u;

    for (int i = 0; i < px_num; ++i) {
        r = QoiReadU8();
        g = QoiReadU8();
        b = QoiReadU8();
        if (channels == 4) a = QoiReadU8();

        // Check if current pixel matches previous pixel for run-length encoding
        if (r == pre_r && g == pre_g && b == pre_b && a == pre_a) {
            run++;
            if (run == 62 || i == px_num - 1) {
                // Write run length (0-61, encoded as 0xCO + (run-1))
                QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
                run = 0;
            }
            // Don't update history or previous pixel during run
            continue;
        }

        // If we have an accumulated run, write it before processing current pixel
        if (run > 0) {
            QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
            run = 0;
        }

        // Calculate hash index for current pixel
        int index = QoiColorHash(r, g, b, a);

        // Check if pixel exists in history (QOI_OP_INDEX)
        if (history[index][0] == r && history[index][1] == g &&
            history[index][2] == b && history[index][3] == a) {
            // Write index op (0x00 + index)
            QoiWriteU8(QOI_OP_INDEX_TAG | index);
        } else {
            // Update history with current pixel
            history[index][0] = r;
            history[index][1] = g;
            history[index][2] = b;
            history[index][3] = a;

            // Calculate differences from previous pixel
            int dr = (int)r - (int)pre_r;
            int dg = (int)g - (int)pre_g;
            int db = (int)b - (int)pre_b;
            int da = (int)a - (int)pre_a;

            // Apply wrap-around to get values in [-128, 127]
            if (dr < -128) dr += 256;
            else if (dr > 127) dr -= 256;
            if (dg < -128) dg += 256;
            else if (dg > 127) dg -= 256;
            if (db < -128) db += 256;
            else if (db > 127) db -= 256;
            if (da < -128) da += 256;
            else if (da > 127) da -= 256;

            // Try QOI_OP_DIFF: small differences (-2..1), only if alpha doesn't change
            if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1 && da == 0) {
                uint8_t diff = ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2);
                QoiWriteU8(QOI_OP_DIFF_TAG | diff);
            }
            // Try QOI_OP_LUMA: larger green difference (-32..31), red/blue differences (-8..7)
            else if (da == 0) {
                // dg is already wrapped to [-128, 127]
                int dg2 = dg + 32;  // Convert to 0..63
                int dr_dg = dr - dg;
                int db_dg = db - dg;
                // Apply wrap-around to dr_dg and db_dg
                if (dr_dg < -128) dr_dg += 256;
                else if (dr_dg > 127) dr_dg -= 256;
                if (db_dg < -128) db_dg += 256;
                else if (db_dg > 127) db_dg -= 256;

                if (dg2 >= 0 && dg2 <= 63 && dr_dg >= -8 && dr_dg <= 7 && db_dg >= -8 && db_dg <= 7) {
                    QoiWriteU8(QOI_OP_LUMA_TAG | dg2);
                    QoiWriteU8(((dr_dg + 8) << 4) | (db_dg + 8));
                } else {
                    // Use QOI_OP_RGB or QOI_OP_RGBA
                    if (channels == 3 || a == pre_a) {
                        // RGB op for RGB images or when alpha doesn't change
                        QoiWriteU8(QOI_OP_RGB_TAG);
                        QoiWriteU8(r);
                        QoiWriteU8(g);
                        QoiWriteU8(b);
                    } else {
                        // RGBA op when alpha changes in RGBA images
                        QoiWriteU8(QOI_OP_RGBA_TAG);
                        QoiWriteU8(r);
                        QoiWriteU8(g);
                        QoiWriteU8(b);
                        QoiWriteU8(a);
                    }
                }
            } else {
                // Alpha changed, use RGBA op
                QoiWriteU8(QOI_OP_RGBA_TAG);
                QoiWriteU8(r);
                QoiWriteU8(g);
                QoiWriteU8(b);
                QoiWriteU8(a);
            }
        }

        pre_r = r;
        pre_g = g;
        pre_b = b;
        pre_a = a;
    }

    // qoi-padding part
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        QoiWriteU8(QOI_PADDING[i]);
    }

    return true;
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {

    char c1 = QoiReadChar();
    char c2 = QoiReadChar();
    char c3 = QoiReadChar();
    char c4 = QoiReadChar();
    if (c1 != 'q' || c2 != 'o' || c3 != 'i' || c4 != 'f') {
        return false;
    }

    // read image width
    width = QoiReadU32();
    // read image height
    height = QoiReadU32();
    // read channel number
    channels = QoiReadU8();
    // read color space specifier
    colorspace = QoiReadU8();

    int px_num = width * height;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r, g, b, a;
    a = 255u;
    uint8_t pre_r, pre_g, pre_b, pre_a;
    pre_r = 0u;
    pre_g = 0u;
    pre_b = 0u;
    pre_a = 255u;

    for (int i = 0; i < px_num; ++i) {
        uint8_t byte1 = QoiReadU8();

        if (byte1 == QOI_OP_RGB_TAG) {
            // RGB op
            r = QoiReadU8();
            g = QoiReadU8();
            b = QoiReadU8();
            // Alpha remains previous alpha (255 for first pixel)
            a = pre_a;
        } else if (byte1 == QOI_OP_RGBA_TAG) {
            // RGBA op
            r = QoiReadU8();
            g = QoiReadU8();
            b = QoiReadU8();
            a = QoiReadU8();
        } else {
            // Check 2-bit tag
            uint8_t tag = byte1 & QOI_MASK_2;

            if (tag == QOI_OP_INDEX_TAG) {
                // Index op
                uint8_t index = byte1 & ~QOI_MASK_2;  // Lower 6 bits
                r = history[index][0];
                g = history[index][1];
                b = history[index][2];
                a = history[index][3];
            } else if (tag == QOI_OP_DIFF_TAG) {
                // Diff op
                uint8_t diff = byte1 & ~QOI_MASK_2;  // Lower 6 bits
                int dr = ((diff >> 4) & 0x03) - 2;
                int dg = ((diff >> 2) & 0x03) - 2;
                int db = (diff & 0x03) - 2;
                // Apply with wrap-around
                r = pre_r + dr;
                g = pre_g + dg;
                b = pre_b + db;
                // Alpha doesn't change
                a = pre_a;
            } else if (tag == QOI_OP_LUMA_TAG) {
                // Luma op
                uint8_t byte2 = QoiReadU8();
                int dg = (byte1 & ~QOI_MASK_2) - 32;  // Lower 6 bits - 32
                int dr_dg = ((byte2 >> 4) & 0x0F) - 8;
                int db_dg = (byte2 & 0x0F) - 8;
                int dr = dr_dg + dg;
                int db = db_dg + dg;
                // Apply with wrap-around
                r = pre_r + dr;
                g = pre_g + dg;
                b = pre_b + db;
                // Alpha doesn't change
                a = pre_a;
            } else if (tag == QOI_OP_RUN_TAG) {
                // Run op
                uint8_t run_length = (byte1 & ~QOI_MASK_2) + 1;  // Lower 6 bits + 1
                // Output the same pixel multiple times
                for (int j = 0; j < run_length; j++) {
                    QoiWriteU8(pre_r);
                    QoiWriteU8(pre_g);
                    QoiWriteU8(pre_b);
                    if (channels == 4) QoiWriteU8(pre_a);
                }
                // Update count
                i += run_length - 1;  // -1 because loop will increment
                // Don't update history or previous values - they remain the same
                continue;
            }
        }

        // Update history with current pixel
        int index = QoiColorHash(r, g, b, a);
        history[index][0] = r;
        history[index][1] = g;
        history[index][2] = b;
        history[index][3] = a;

        // Update previous pixel values
        pre_r = r;
        pre_g = g;
        pre_b = b;
        pre_a = a;

        QoiWriteU8(r);
        QoiWriteU8(g);
        QoiWriteU8(b);
        if (channels == 4) QoiWriteU8(a);
    }

    bool valid = true;
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        if (QoiReadU8() != QOI_PADDING[i]) valid = false;
    }

    return valid;
}

#endif // QOI_FORMAT_CODEC_QOI_H_
