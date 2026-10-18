#include "driver/utf8_utils.h"

std::string sanitize_utf8_string(const std::string& str) {
    std::string clean_str;
    clean_str.reserve(str.size());

    size_t i = 0;
    size_t len = str.size();

    while (i < len) {
        unsigned char c = (unsigned char)str[i];

        if (c < 0x80) { // 1-byte ASCII (0x00 - 0x7F)
            if (c >= 0x20 || c == '\n' || c == '\t' || c == '\r') {
                clean_str.push_back((char)c);
            } else {
                clean_str.push_back(' ');
            }
            i += 1;
        } else if ((c & 0xE0) == 0xC0) { // 2-byte UTF-8
            if (i + 1 < len && ((unsigned char)str[i + 1] & 0xC0) == 0x80) {
                clean_str.append(str, i, 2);
                i += 2;
            } else {
                clean_str.push_back('?');
                i += 1;
            }
        } else if ((c & 0xF0) == 0xE0) { // 3-byte UTF-8 (CJK, Cyrillic, Arabic, Symbols)
            if (i + 2 < len &&
                ((unsigned char)str[i + 1] & 0xC0) == 0x80 &&
                ((unsigned char)str[i + 2] & 0xC0) == 0x80) {
                clean_str.append(str, i, 3);
                i += 3;
            } else {
                clean_str.push_back('?');
                i += 1;
            }
        } else if ((c & 0xF8) == 0xF0) { // 4-byte UTF-8 (Emojis, Symbols)
            if (i + 3 < len &&
                ((unsigned char)str[i + 1] & 0xC0) == 0x80 &&
                ((unsigned char)str[i + 2] & 0xC0) == 0x80 &&
                ((unsigned char)str[i + 3] & 0xC0) == 0x80) {
                clean_str.append(str, i, 4);
                i += 4;
            } else {
                clean_str.push_back('?');
                i += 1;
            }
        } else {
            clean_str.push_back('?');
            i += 1;
        }
    }

    return clean_str;
}
