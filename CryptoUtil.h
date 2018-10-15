/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_CRYPTO_UTIL_H__
#define __TZHTTPD_CRYPTO_UTIL_H__

#include <xtra_rhel6.h>

#include <cmath>
#include <iomanip>
#include <istream>
#include <sstream>

#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include <cryptopp/gzip.h>

// 类静态函数可以直接将函数定义丢在头文件中

namespace tzhttpd {

// struct相比于namespace，可以避免产生多个实体

struct CryptoUtil {


static std::string base64_encode(const std::string &ascii) {

    std::string base64;

    BIO *bio, *b64;
    BUF_MEM *bptr = BUF_MEM_new();

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new(BIO_s_mem());
    BIO_push(b64, bio);
    BIO_set_mem_buf(b64, bptr, BIO_CLOSE);

    // Write directly to base64-buffer to avoid copy
    auto base64_length = static_cast<std::size_t>(round(4 * ceil(static_cast<double>(ascii.size()) / 3.0)));
    base64.resize(base64_length);
    bptr->length = 0;
    bptr->max = base64_length + 1;
    bptr->data = &base64[0];

    if(BIO_write(b64, &ascii[0], static_cast<int>(ascii.size())) <= 0 || BIO_flush(b64) <= 0)
        base64.clear();

    // To keep &base64[0] through BIO_free_all(b64)
    bptr->length = 0;
    bptr->max = 0;
    bptr->data = (char*)NULL;

    BIO_free_all(b64);

    return base64;
}

static std::string base64_decode(const std::string &base64) noexcept {

    std::string ascii;

    // Resize ascii, however, the size is a up to two bytes too large.
    ascii.resize((6 * base64.size()) / 8);
    BIO *b64, *bio;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    // TODO: Remove in 2020
#if OPENSSL_VERSION_NUMBER <= 0x1000115fL
    bio = BIO_new_mem_buf((char *)&base64[0], static_cast<int>(base64.size()));
#else
    bio = BIO_new_mem_buf(&base64[0], static_cast<int>(base64.size()));
#endif
    bio = BIO_push(b64, bio);

    auto decoded_length = BIO_read(bio, &ascii[0], static_cast<int>(ascii.size()));
    if(decoded_length > 0)
        ascii.resize(static_cast<std::size_t>(decoded_length));
    else
        ascii.clear();

    BIO_free_all(b64);

    return ascii;
}

/// Return hex string from bytes in input string.
/// 比如签名的二进制结果进行HEX字符串表达显示
static std::string to_hex_string(const std::string &input) noexcept {
    std::stringstream hex_stream;
    hex_stream << std::hex << std::internal << std::setfill('0');
    for(size_t i=0; i<input.size(); ++i)
        hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(input[i]));

    return hex_stream.str();
}

static std::string md5(const std::string &input, std::size_t iterations = 1) noexcept {
    std::string hash;

    hash.resize(128 / 8);
    ::MD5(reinterpret_cast<const unsigned char *>(&input[0]), input.size(),
            reinterpret_cast<unsigned char *>(&hash[0]));

    for(std::size_t c = 1; c < iterations; ++c)
        ::MD5(reinterpret_cast<const unsigned char *>(&hash[0]), hash.size(),
                reinterpret_cast<unsigned char *>(&hash[0]));

    return hash;
}


static std::string sha1(const std::string &input, std::size_t iterations = 1) noexcept {
    std::string hash;

    hash.resize(160 / 8);
    ::SHA1(reinterpret_cast<const unsigned char *>(&input[0]), input.size(),
            reinterpret_cast<unsigned char *>(&hash[0]));

    for(std::size_t c = 1; c < iterations; ++c)
        ::SHA1(reinterpret_cast<const unsigned char *>(&hash[0]), hash.size(),
                reinterpret_cast<unsigned char *>(&hash[0]));

    return hash;
}


static std::string sha256(const std::string &input, std::size_t iterations = 1) noexcept {
    std::string hash;

    hash.resize(256 / 8);
    ::SHA256(reinterpret_cast<const unsigned char *>(&input[0]), input.size(),
             reinterpret_cast<unsigned char *>(&hash[0]));

    for(std::size_t c = 1; c < iterations; ++c)
        ::SHA256(reinterpret_cast<const unsigned char *>(&hash[0]), hash.size(),
                 reinterpret_cast<unsigned char *>(&hash[0]));

    return hash;
}


static std::string sha512(const std::string &input, std::size_t iterations = 1) noexcept {
    std::string hash;

    hash.resize(512 / 8);
    ::SHA512(reinterpret_cast<const unsigned char *>(&input[0]), input.size(),
            reinterpret_cast<unsigned char *>(&hash[0]));

    for(std::size_t c = 1; c < iterations; ++c)
        ::SHA512(reinterpret_cast<const unsigned char *>(&hash[0]), hash.size(),
                 reinterpret_cast<unsigned char *>(&hash[0]));

    return hash;
}

static std::string char_to_hex(char c) noexcept {

    std::string result;
    char first, second;

    first =  static_cast<char>((c & 0xF0) / 16);
    first += static_cast<char>(first > 9 ? 'A' - 10 : '0');
    second =  c & 0x0F;
    second += static_cast<char>(second > 9 ? 'A' - 10 : '0');

    result.append(1, first); result.append(1, second);
    return result;
}


static char hex_to_char(char first, char second) noexcept {
    int digit;

    digit = (first >= 'A' ? ((first & 0xDF) - 'A') + 10 : (first - '0'));
    digit *= 16;
    digit += (second >= 'A' ? ((second & 0xDF) - 'A') + 10 : (second - '0'));
    return static_cast<char>(digit);
}

static std::string url_encode(const std::string& src) noexcept {

    std::string result;
    for(std::string::const_iterator iter = src.begin(); iter != src.end(); ++iter) {
        switch(*iter) {
            case ' ':
                result.append(1, '+');
                break;

            // alnum
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
            case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
            case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
            case 'V': case 'W': case 'X': case 'Y': case 'Z':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
            case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
            case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
            case 'v': case 'w': case 'x': case 'y': case 'z':
            case '0': case '1': case '2': case '3': case '4': case '5': case '6':
            case '7': case '8': case '9':
            // mark
            case '-': case '_': case '.': case '!': case '~': case '*': case '\'':
            case '(': case ')':
                result.append(1, *iter);
                break;

            // escape
            default:
                result.append(1, '%');
                result.append(char_to_hex(*iter));
                break;
        }
    }

    return result;
}

static std::string url_decode(const std::string& src) noexcept {

    std::string result;
    char c;

    for(std::string::const_iterator iter = src.begin(); iter != src.end(); ++iter) {
        switch(*iter) {
            case '+':
                result.append(1, ' ');
                break;

            case '%':
                // Don't assume well-formed input
                if(std::distance(iter, src.end()) >= 2 &&
                   std::isxdigit(*(iter + 1)) &&
                   std::isxdigit(*(iter + 2))) {
                    c = *(++iter);
                    result.append(1, hex_to_char(c, *(++iter)));
                }
                // Just pass the % through untouched
                else {
                    result.append(1, '%');
                }
                break;

            default:
                result.append(1, *iter);
                break;
        }
    }

    return result;
}

static int Gzip(const std::string& src, std::string& store) {

    CryptoPP::Gzip zipper;

    zipper.Put((byte*)src.data(), src.size());
    zipper.MessageEnd();

    CryptoPP::word64 avail = zipper.MaxRetrievable();
    if(avail) {
        store.resize(avail);
        zipper.Get((byte*)&store[0], store.size());
        return 0;
    }

    return -1;
}



static int Gunzip(const std::string& src, std::string& store) {

    CryptoPP::Gunzip unzipper;

    unzipper.Put((byte*)src.data(), src.size());
    unzipper.MessageEnd();

    CryptoPP::word64 avail = unzipper.MaxRetrievable();
    if(avail) {
        store.resize(avail);
        unzipper.Get((byte*)&store[0], store.size());
        return 0;
    }

    return -1;
}

static int Deflator(const std::string& src, std::string& store) {

    CryptoPP::Deflator zipper;

    zipper.Put((byte*)src.data(), src.size());
    zipper.MessageEnd();

    CryptoPP::word64 avail = zipper.MaxRetrievable();
    if(avail) {
        store.resize(avail);
        zipper.Get((byte*)&store[0], store.size());
        return 0;
    }

    return -1;
}

static int Inflator(const std::string& src, std::string& store) {

    CryptoPP::Inflator unzipper;

    unzipper.Put((byte*)src.data(), src.size());
    unzipper.MessageEnd();

    CryptoPP::word64 avail = unzipper.MaxRetrievable();
    if(avail) {
        store.resize(avail);
        unzipper.Get((byte*)&store[0], store.size());
        return 0;
    }

    return -1;
}


};


} // end namespace tzhttpd

#endif // __TZHTTPD_CRYPTO_UTIL_H__
