// The number text of a double: Grisu2 and the layout of `to_chars` of "JSON for Modern C++" 3.11.3 (nlohmann/detail/conversions/to_chars.hpp),
// ported for `double` only. The control answers, replay files, room messages and ArtGen files hold these bytes, and `std::to_chars` writes
// another text for some doubles, so the routine lives on after the library was removed.
//
// Grisu2: Copyright (c) 2009 Florian Loitsch <https://florian.loitsch.com/>
// JSON for Modern C++: Copyright (c) 2013-2023 Niels Lohmann <https://nlohmann.me>
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// The algorithm: Loitsch, "Printing Floating-Point Numbers Quickly and Accurately with Integers", PLDI 2010.

#include "JsonDoubleText.h"

#include <bit>
#include <cmath>

namespace BECore {

    namespace {

        constexpr int kMantissaBits = 53;
        constexpr int kExponentBias = 1023 + kMantissaBits - 1;
        constexpr int kMinBinaryExponent = 1 - kExponentBias;
        constexpr uint64_t kHiddenBit = uint64_t{1} << (kMantissaBits - 1);
        [[maybe_unused]] constexpr int kMaxDigits = 17;
        /// Fixed notation from `0.0001` up to this many digits before the point; outside it the text is `d.ddde+XX`
        constexpr int kMinDecimalExponent = -4;
        constexpr int kMaxDecimalExponent = 15;

        /// The window the product of a value and its cached power of ten has to fall in (Definition 3.2 of the paper)
        constexpr int kAlpha = -60;
        [[maybe_unused]] constexpr int kGamma = -32;

        constexpr int kHighBitShift = 63;
        constexpr uint64_t kLow32 = 0xFFFFFFFFu;
        constexpr int kHalfBits = 32;

        /// f * 2^e
        struct DiyFp {
            uint64_t f = 0;
            int e = 0;
        };

        /// x - y, the same exponent and x.f >= y.f
        DiyFp Sub(DiyFp x, DiyFp y) {
            ASSERT(x.e == y.e);
            ASSERT(x.f >= y.f);
            return {x.f - y.f, x.e};
        }

        /// x * y with the upper 64 bits of the product, rounded (ties up)
        DiyFp Mul(DiyFp x, DiyFp y) {
            const uint64_t uLow = x.f & kLow32;
            const uint64_t uHigh = x.f >> kHalfBits;
            const uint64_t vLow = y.f & kLow32;
            const uint64_t vHigh = y.f >> kHalfBits;

            const uint64_t p0 = uLow * vLow;
            const uint64_t p1 = uLow * vHigh;
            const uint64_t p2 = uHigh * vLow;
            const uint64_t p3 = uHigh * vHigh;

            const uint64_t p0High = p0 >> kHalfBits;
            const uint64_t p1Low = p1 & kLow32;
            const uint64_t p1High = p1 >> kHalfBits;
            const uint64_t p2Low = p2 & kLow32;
            const uint64_t p2High = p2 >> kHalfBits;

            uint64_t middle = p0High + p1Low + p2Low;
            // Only the highest bit of the low half of the product matters: it rounds the high half
            middle += uint64_t{1} << (kHalfBits - 1);
            const uint64_t high = p3 + p2High + p1High + (middle >> kHalfBits);
            return {high, x.e + y.e + 64};
        }

        /// Shifts the significand up until its top bit is set
        DiyFp Normalize(DiyFp x) {
            ASSERT(x.f != 0);
            while ((x.f >> kHighBitShift) == 0) {
                x.f <<= 1u;
                x.e--;
            }
            return x;
        }

        /// The same value with the exponent @p targetExponent; the bits shifted out must be zero
        DiyFp NormalizeTo(DiyFp x, int targetExponent) {
            const int delta = x.e - targetExponent;
            ASSERT(delta >= 0);
            ASSERT(((x.f << delta) >> delta) == x.f);
            return {x.f << delta, targetExponent};
        }

        struct Boundaries {
            DiyFp w;
            DiyFp minus;
            DiyFp plus;
        };

        /// The normalized value and the two midpoints to its neighbours, which share one exponent; @p value is finite and positive
        Boundaries ComputeBoundaries(double value) {
            ASSERT(std::isfinite(value));
            ASSERT(value > 0);

            const uint64_t bits = std::bit_cast<uint64_t>(value);
            const uint64_t biasedExponent = bits >> (kMantissaBits - 1);
            const uint64_t fraction = bits & (kHiddenBit - 1);

            const bool isDenormal = biasedExponent == 0;
            const DiyFp v = isDenormal ? DiyFp{fraction, kMinBinaryExponent} : DiyFp{fraction + kHiddenBit, static_cast<int>(biasedExponent) - kExponentBias};

            // A power of two has a closer neighbour below it
            const bool lowerBoundaryIsCloser = fraction == 0 && biasedExponent > 1;
            const DiyFp mPlus{2 * v.f + 1, v.e - 1};
            const DiyFp mMinus = lowerBoundaryIsCloser ? DiyFp{4 * v.f - 1, v.e - 2} : DiyFp{2 * v.f - 1, v.e - 1};

            const DiyFp wPlus = Normalize(mPlus);
            const DiyFp wMinus = NormalizeTo(mMinus, wPlus.e);
            return {Normalize(v), wMinus, wPlus};
        }

        /// c = f * 2^e, about 10^k
        struct CachedPower {
            uint64_t f;
            int e;
            int k;
        };

        constexpr int kCachedPowersMinDecimalExponent = -300;
        constexpr int kCachedPowersDecimalStep = 8;
        [[maybe_unused]] constexpr int kMaxBinaryExponentForPowers = 1500;
        // The factor of the estimate of log10(2) used to pick the power: 78913 / 2^18
        constexpr int kLog10Of2Numerator = 78913;
        constexpr int kLog10Of2Shift = 18;

        // clang-format off
        constexpr Array<CachedPower, 79> kCachedPowers = {{
        { 0xAB70FE17C79AC6CA, -1060, -300 },
        { 0xFF77B1FCBEBCDC4F, -1034, -292 },
        { 0xBE5691EF416BD60C, -1007, -284 },
        { 0x8DD01FAD907FFC3C,  -980, -276 },
        { 0xD3515C2831559A83,  -954, -268 },
        { 0x9D71AC8FADA6C9B5,  -927, -260 },
        { 0xEA9C227723EE8BCB,  -901, -252 },
        { 0xAECC49914078536D,  -874, -244 },
        { 0x823C12795DB6CE57,  -847, -236 },
        { 0xC21094364DFB5637,  -821, -228 },
        { 0x9096EA6F3848984F,  -794, -220 },
        { 0xD77485CB25823AC7,  -768, -212 },
        { 0xA086CFCD97BF97F4,  -741, -204 },
        { 0xEF340A98172AACE5,  -715, -196 },
        { 0xB23867FB2A35B28E,  -688, -188 },
        { 0x84C8D4DFD2C63F3B,  -661, -180 },
        { 0xC5DD44271AD3CDBA,  -635, -172 },
        { 0x936B9FCEBB25C996,  -608, -164 },
        { 0xDBAC6C247D62A584,  -582, -156 },
        { 0xA3AB66580D5FDAF6,  -555, -148 },
        { 0xF3E2F893DEC3F126,  -529, -140 },
        { 0xB5B5ADA8AAFF80B8,  -502, -132 },
        { 0x87625F056C7C4A8B,  -475, -124 },
        { 0xC9BCFF6034C13053,  -449, -116 },
        { 0x964E858C91BA2655,  -422, -108 },
        { 0xDFF9772470297EBD,  -396, -100 },
        { 0xA6DFBD9FB8E5B88F,  -369,  -92 },
        { 0xF8A95FCF88747D94,  -343,  -84 },
        { 0xB94470938FA89BCF,  -316,  -76 },
        { 0x8A08F0F8BF0F156B,  -289,  -68 },
        { 0xCDB02555653131B6,  -263,  -60 },
        { 0x993FE2C6D07B7FAC,  -236,  -52 },
        { 0xE45C10C42A2B3B06,  -210,  -44 },
        { 0xAA242499697392D3,  -183,  -36 },
        { 0xFD87B5F28300CA0E,  -157,  -28 },
        { 0xBCE5086492111AEB,  -130,  -20 },
        { 0x8CBCCC096F5088CC,  -103,  -12 },
        { 0xD1B71758E219652C,   -77,   -4 },
        { 0x9C40000000000000,   -50,    4 },
        { 0xE8D4A51000000000,   -24,   12 },
        { 0xAD78EBC5AC620000,     3,   20 },
        { 0x813F3978F8940984,    30,   28 },
        { 0xC097CE7BC90715B3,    56,   36 },
        { 0x8F7E32CE7BEA5C70,    83,   44 },
        { 0xD5D238A4ABE98068,   109,   52 },
        { 0x9F4F2726179A2245,   136,   60 },
        { 0xED63A231D4C4FB27,   162,   68 },
        { 0xB0DE65388CC8ADA8,   189,   76 },
        { 0x83C7088E1AAB65DB,   216,   84 },
        { 0xC45D1DF942711D9A,   242,   92 },
        { 0x924D692CA61BE758,   269,  100 },
        { 0xDA01EE641A708DEA,   295,  108 },
        { 0xA26DA3999AEF774A,   322,  116 },
        { 0xF209787BB47D6B85,   348,  124 },
        { 0xB454E4A179DD1877,   375,  132 },
        { 0x865B86925B9BC5C2,   402,  140 },
        { 0xC83553C5C8965D3D,   428,  148 },
        { 0x952AB45CFA97A0B3,   455,  156 },
        { 0xDE469FBD99A05FE3,   481,  164 },
        { 0xA59BC234DB398C25,   508,  172 },
        { 0xF6C69A72A3989F5C,   534,  180 },
        { 0xB7DCBF5354E9BECE,   561,  188 },
        { 0x88FCF317F22241E2,   588,  196 },
        { 0xCC20CE9BD35C78A5,   614,  204 },
        { 0x98165AF37B2153DF,   641,  212 },
        { 0xE2A0B5DC971F303A,   667,  220 },
        { 0xA8D9D1535CE3B396,   694,  228 },
        { 0xFB9B7CD9A4A7443C,   720,  236 },
        { 0xBB764C4CA7A44410,   747,  244 },
        { 0x8BAB8EEFB6409C1A,   774,  252 },
        { 0xD01FEF10A657842C,   800,  260 },
        { 0x9B10A4E5E9913129,   827,  268 },
        { 0xE7109BFBA19C0C9D,   853,  276 },
        { 0xAC2820D9623BF429,   880,  284 },
        { 0x80444B5E7AA7CF85,   907,  292 },
        { 0xBF21E44003ACDD2D,   933,  300 },
        { 0x8E679C2F5E44FF8F,   960,  308 },
        { 0xD433179D9C8CB841,   986,  316 },
        { 0x9E19DB92B4E31BA9,  1013,  324 },
        }};
        // clang-format on

        /// The cached power of ten c with `kAlpha <= c.e + e + 64 <= kGamma`
        CachedPower GetCachedPowerForBinaryExponent(int e) {
            ASSERT(e >= -kMaxBinaryExponentForPowers);
            ASSERT(e <= kMaxBinaryExponentForPowers);

            const int f = kAlpha - e - 1;
            const int k = (f * kLog10Of2Numerator) / (1 << kLog10Of2Shift) + static_cast<int>(f > 0);
            const int index = (-kCachedPowersMinDecimalExponent + k + (kCachedPowersDecimalStep - 1)) / kCachedPowersDecimalStep;
            ASSERT(index >= 0);
            ASSERT(static_cast<size_t>(index) < kCachedPowers.size());

            const CachedPower cached = kCachedPowers[static_cast<size_t>(index)];
            ASSERT(kAlpha <= cached.e + e + 64);
            ASSERT(kGamma >= cached.e + e + 64);
            return cached;
        }

        /// For n != 0 the k with 10^(k-1) <= n < 10^k and `pow10` = 10^(k-1); for 0, 1 and 1
        int FindLargestPow10(uint32_t n, uint32_t& pow10) {
            constexpr uint32_t kBillion = 1000000000;
            pow10 = kBillion;
            int digits = 10;
            while (digits > 1 && n < pow10) {
                pow10 /= 10;
                --digits;
            }
            return digits;
        }

        /// Moves the last digit down while that gets closer to the exact value and stays inside the interval
        void Grisu2Round(char* buffer, int length, uint64_t dist, uint64_t delta, uint64_t rest, uint64_t tenK) {
            ASSERT(length >= 1);
            ASSERT(dist <= delta);
            ASSERT(rest <= delta);
            ASSERT(tenK > 0);

            while (rest < dist && delta - rest >= tenK && (rest + tenK < dist || dist - rest > rest + tenK - dist)) {
                ASSERT(buffer[length - 1] != '0');
                buffer[length - 1]--;
                rest += tenK;
            }
        }

        /// The digits of V = buffer * 10^decimalExponent with `mMinus <= V <= mPlus`; both are normalized and share an exponent in the window
        void Grisu2DigitGen(char* buffer, int& length, int& decimalExponent, DiyFp mMinus, DiyFp w, DiyFp mPlus) {
            ASSERT(mPlus.e >= kAlpha);
            ASSERT(mPlus.e <= kGamma);

            uint64_t delta = Sub(mPlus, mMinus).f;
            uint64_t dist = Sub(mPlus, w).f;

            const DiyFp one{uint64_t{1} << -mPlus.e, mPlus.e};
            // -e >= 32, so the integral part fits 32 bits
            auto p1 = static_cast<uint32_t>(mPlus.f >> -one.e);
            uint64_t p2 = mPlus.f & (one.f - 1);
            ASSERT(p1 > 0);

            uint32_t pow10{};
            const int k = FindLargestPow10(p1, pow10);

            int n = k;
            while (n > 0) {
                const uint32_t d = p1 / pow10;
                const uint32_t r = p1 % pow10;
                ASSERT(d <= 9);
                buffer[length++] = static_cast<char>('0' + d);
                p1 = r;
                n--;

                const uint64_t rest = (uint64_t{p1} << -one.e) + p2;
                if (rest <= delta) {
                    decimalExponent += n;
                    const uint64_t tenN = uint64_t{pow10} << -one.e;
                    Grisu2Round(buffer, length, dist, delta, rest, tenN);
                    return;
                }
                pow10 /= 10;
            }

            ASSERT(p2 > delta);
            int m = 0;
            for (;;) {
                ASSERT(p2 <= (std::numeric_limits<uint64_t>::max)() / 10);
                p2 *= 10;
                const uint64_t d = p2 >> -one.e;
                const uint64_t r = p2 & (one.f - 1);
                ASSERT(d <= 9);
                buffer[length++] = static_cast<char>('0' + d);
                p2 = r;
                m++;
                delta *= 10;
                dist *= 10;
                if (p2 <= delta) {
                    break;
                }
            }

            decimalExponent -= m;
            Grisu2Round(buffer, length, dist, delta, p2, one.f);
        }

        /// value = buffer * 10^decimalExponent, `length` digits written; @p value is finite and positive
        void Grisu2(char* buffer, int& length, int& decimalExponent, double value) {
            const Boundaries b = ComputeBoundaries(value);
            ASSERT(b.plus.e == b.minus.e);
            ASSERT(b.plus.e == b.w.e);

            const CachedPower cached = GetCachedPowerForBinaryExponent(b.plus.e);
            const DiyFp cMinusK{cached.f, cached.e};

            const DiyFp w = Mul(b.w, cMinusK);
            const DiyFp wMinus = Mul(b.minus, cMinusK);
            const DiyFp wPlus = Mul(b.plus, cMinusK);

            // The products are off by one unit; the interval shrinks to stay inside the true one
            const DiyFp mMinus{wMinus.f + 1, wMinus.e};
            const DiyFp mPlus{wPlus.f - 1, wPlus.e};

            decimalExponent = -cached.k;
            Grisu2DigitGen(buffer, length, decimalExponent, mMinus, w, mPlus);
        }

        /// Appends `+XX`, `-XX` or the three-digit form of @p e (|e| < 1000) and returns the end
        char* AppendExponent(char* buffer, int e) {
            ASSERT(e > -1000);
            ASSERT(e < 1000);

            if (e < 0) {
                e = -e;
                *buffer++ = '-';
            } else {
                *buffer++ = '+';
            }

            auto k = static_cast<uint32_t>(e);
            if (k < 10) {
                *buffer++ = '0';
                *buffer++ = static_cast<char>('0' + k);
            } else if (k < 100) {
                *buffer++ = static_cast<char>('0' + k / 10);
                k %= 10;
                *buffer++ = static_cast<char>('0' + k);
            } else {
                *buffer++ = static_cast<char>('0' + k / 100);
                k %= 100;
                *buffer++ = static_cast<char>('0' + k / 10);
                k %= 10;
                *buffer++ = static_cast<char>('0' + k);
            }
            return buffer;
        }

        /// Turns the `length` digits and `decimalExponent` into text: fixed notation for a value in [10^minExp, 10^maxExp), else `d.ddde+XX`
        char* FormatBuffer(char* buffer, int length, int decimalExponent, int minExp, int maxExp) {
            ASSERT(minExp < 0);
            ASSERT(maxExp > 0);

            const int k = length;
            const int n = length + decimalExponent;

            // digits[000].0
            if (k <= n && n <= maxExp) {
                std::memset(buffer + k, '0', static_cast<size_t>(n) - static_cast<size_t>(k));
                buffer[n + 0] = '.';
                buffer[n + 1] = '0';
                return buffer + (static_cast<size_t>(n) + 2);
            }

            // dig.its
            if (0 < n && n <= maxExp) {
                ASSERT(k > n);
                std::memmove(buffer + (static_cast<size_t>(n) + 1), buffer + n, static_cast<size_t>(k) - static_cast<size_t>(n));
                buffer[n] = '.';
                return buffer + (static_cast<size_t>(k) + 1U);
            }

            // 0.[000]digits
            if (minExp < n && n <= 0) {
                std::memmove(buffer + (2 + static_cast<size_t>(-n)), buffer, static_cast<size_t>(k));
                buffer[0] = '0';
                buffer[1] = '.';
                std::memset(buffer + 2, '0', static_cast<size_t>(-n));
                return buffer + (2U + static_cast<size_t>(-n) + static_cast<size_t>(k));
            }

            // d[.igits]e+XX
            if (k == 1) {
                buffer += 1;
            } else {
                std::memmove(buffer + 2, buffer + 1, static_cast<size_t>(k) - 1);
                buffer[1] = '.';
                buffer += 1 + static_cast<size_t>(k);
            }
            *buffer++ = 'e';
            return AppendExponent(buffer, n - 1);
        }

    }  // namespace

    size_t WriteDoubleText(char* buffer, double value) {
        ASSERT(std::isfinite(value));

        char* first = buffer;
        if (std::signbit(value)) {
            value = -value;
            *first++ = '-';
        }

        if (value == 0.0) {
            *first++ = '0';
            *first++ = '.';
            *first++ = '0';
            return static_cast<size_t>(first - buffer);
        }

        int length = 0;
        int decimalExponent = 0;
        Grisu2(first, length, decimalExponent, value);
        ASSERT(length <= kMaxDigits);

        char* end = FormatBuffer(first, length, decimalExponent, kMinDecimalExponent, kMaxDecimalExponent);
        const size_t written = static_cast<size_t>(end - buffer);
        ASSERT(written <= kMaxDoubleTextBytes);
        return written;
    }

}  // namespace BECore
