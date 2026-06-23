#include "libslic3r/Format/PhrozenPRZRle.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

// Remaining encoder implementations land in later task groups:
//   - task 5 : prz_encode_layer_banded FALSE/vertical family (descending sched.)
//   - task 7 : TLS tile buffer + ragged-band ROI view

namespace Slic3r {

namespace {

// ---------------------------------------------------------------------------
// PRZ-RLE byte-format constants — moved verbatim from the legacy inline encoder
// in SLAPrintSteps.cpp (the single source of truth for the on-disk byte format).
//   black run : base 0x00      white run : base 0xc0      gray run : base 0x40
//   high nibble of the lead byte encodes the continuation-byte count (0..3);
//   low nibble is count & 0x0f; the remaining (count >> 4) follows as `bid`
//   big-endian bytes. Gray runs additionally carry the raw color byte.
// ---------------------------------------------------------------------------
constexpr uchar RLE_BLACK          = 0x00;
constexpr uchar RLE_WHITE          = 0xc0;
constexpr uchar RLE_GRAY           = 0x40;
constexpr uchar RLE_BYTE_NUMBER[4] = {0x00, 0x10, 0x20, 0x30};
constexpr int   RLE_CONT_BOUND[4]  = {1 << 4, 1 << 12, 1 << 20, 1 << 28};
constexpr int   RLE_BOUND_0        = 0x0f;

// Emit one (color, count) run into `out` at `pos`, accumulating `sum`.
// Byte-for-byte identical to the legacy flush_run lambda in SLAPrintSteps.cpp.
// `count` is taken by value: the low nibble goes into the lead byte, then it is
// shifted right by 4 and its `bid` bytes are appended (high-to-low order).
inline void prz_flush_run(std::vector<char> &out, std::size_t &pos, int &sum,
                          uchar color, int count)
{
    const char *c = reinterpret_cast<const char *>(&count);
    if (color == 0x00 || color == 0xff) {
        uchar base = (color == 0x00) ? RLE_BLACK : RLE_WHITE;
        for (int bid = 0; bid < 4; ++bid) {
            if (count < RLE_CONT_BOUND[bid]) {
                uchar b0 = base + RLE_BYTE_NUMBER[bid] + (count & RLE_BOUND_0);
                count >>= 4;
                sum += static_cast<int>(b0);
                out[pos++] = static_cast<char>(b0);
                for (int k = bid; k >= 1; --k) {
                    out[pos++] = c[k - 1];
                    sum += static_cast<int>(static_cast<uchar>(c[k - 1]));
                }
                break;
            }
        }
    } else {
        for (int bid = 0; bid < 4; ++bid) {
            if (count < RLE_CONT_BOUND[bid]) {
                uchar b0 = RLE_GRAY + RLE_BYTE_NUMBER[bid] + (count & RLE_BOUND_0);
                count >>= 4;
                sum += static_cast<int>(b0);
                out[pos++] = static_cast<char>(b0);
                out[pos++] = static_cast<char>(color);
                sum += static_cast<int>(color);
                for (int k = bid; k >= 1; --k) {
                    out[pos++] = c[k - 1];
                    sum += static_cast<int>(static_cast<uchar>(c[k - 1]));
                }
                break;
            }
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// PrzRleEncoder — stateful run-length encoder whose run state (current color,
// run count, checksum accumulator, write position) is carried ACROSS feed()
// calls. This is what lets the banded encoder (task 4-5) push one rotated+
// flipped tile at a time while producing a byte stream identical to a single
// full-frame linear pass:
//
//   begin();                         // writes the 0x55 layer head
//   feed(tile0.data, tile0.size);    // run state persists between feeds...
//   feed(tile1.data, tile1.size);    // ...so a run may span tile boundaries
//   ...
//   total_bytes = finish();          // flushes the trailing run + checksum
//
// `out` is caller-owned and reused (zero-malloc steady state: once grown large
// enough, feed()/finish() stop resizing).
// ---------------------------------------------------------------------------
struct PrzRleEncoder {
    std::vector<char> &out;
    std::size_t        pos     = 0;
    int                sum     = 0;
    uchar              cur     = 0;
    int                count   = 0;
    bool               started = false;

    explicit PrzRleEncoder(std::vector<char> &o) : out(o) {}

    void begin()
    {
        pos = 0; sum = 0; count = 0; started = false;
        if (out.size() < 1)
            out.resize(16);
        out[pos++] = static_cast<char>(0x55); // PRZ layer head (once per frame)
    }

    // Consume n pixels, carrying the run across this and prior/later feeds.
    void feed(const uchar *d, int n)
    {
        if (n <= 0)
            return;
        // Worst case 2 bytes/pixel (alternating gray) + slack for a flushed
        // carried run + checksum. Never shrinks → steady-state zero realloc.
        const std::size_t need = pos + static_cast<std::size_t>(n) * 2 + 16;
        if (out.size() < need)
            out.resize(need);

        int i = 0;
        if (!started) {
            cur     = d[0];
            count   = 1;
            started = true;
            i       = 1;
        }
        for (; i < n; ++i) {
            uchar px = d[i];
            if (px == cur) {
                ++count;
            } else {
                prz_flush_run(out, pos, sum, cur, count);
                cur   = px;
                count = 1;
            }
        }
    }

    // Flush the final run and append the 1-byte checksum. Returns total bytes.
    std::size_t finish()
    {
        if (started)
            prz_flush_run(out, pos, sum, cur, count);
        if (out.size() < pos + 1)
            out.resize(pos + 1);
        out[pos++] = static_cast<char>(static_cast<uchar>((~sum) & 0xff));
        return pos;
    }
};

// Band/tiled fused rotate + per-tile flip + PRZ-RLE encode.
//
// ============================ CRITICAL CONTRACT ============================
// DO NOT "simplify" the two rules below. Both are load-bearing for correctness;
// breaking either produces output that still RLE-decodes and passes every
// length/checksum check, yet prints a MIRRORED / segment-scrambled layer on
// hardware — the most expensive possible failure (caught only on a physical
// print, never by "does it slice?").
//
// (1) SYMMETRIC FLIP RULE. BOTH printer families MUST apply the per-tile flip;
//     the ONLY permitted difference is the OUTER band scheduling direction:
//        FALSE/vertical  -> descending band order + per-tile cv::flip code 0
//        TRUE /horizontal -> ascending  band order + per-tile cv::flip code 1
//     The vertical case is the trap: a global vertical flip decomposes as
//        (reverse band order) ∘ (reverse rows within each tile),
//     and for band height K>1 BOTH halves are required. Reversing only the band
//     ORDER (dropping the per-tile code-0 flip) yields a BLOCK PERMUTATION — a
//     silent mirror. This is enforced by the sandbox K-invariance sweep: the
//     byte stream MUST be independent of K, which is impossible if either half
//     is missing. (See design.md D3 and the prz-band-tiled-rle-fusion grilling.)
//
// (2) CROSS-BAND RUN-STATE CONTINUITY. The PrzRleEncoder run state
//     (cur/count/sum/pos) is carried across feed() calls WITHOUT reset, so a run
//     may span tile and band boundaries exactly as in a single full-frame linear
//     pass. The 0x55 head is written once (begin) and the checksum once (finish),
//     NEVER per band. The ragged last band uses min(K, N-c0) and takes the SAME
//     path (no special case) — in descending order it is processed FIRST.
//
// Output is byte-identical to the legacy full-frame rotate->flip->linear-RLE
// path; CACHE_VERSION is therefore unchanged.
// ==========================================================================
//
// Geometry (portrait: M=rows=display_pixels_x, N=cols=display_pixels_y):
//   ROTATE_90_CW maps dst row i == portrait COLUMN i. A band of K consecutive
//   dst rows is therefore a vertical M x k slab of the portrait. Rotating that
//   slab yields a contiguous k x M tile == dst rows [c0, c0+k) in ascending
//   order, which is then flipped and fed to the run-state-carrying encoder.
//
// Symmetric flip rule (design.md D3) — BOTH families call the per-tile flip;
// the ONLY difference is the outer band scheduling direction:
//   TRUE  (horizontal): ascending band order  + per-tile cv::flip code 1.
//          A horizontal flip is within-row, so local == global. Trivially banded.
//   FALSE (vertical)  : descending band order + per-tile cv::flip code 0.
//          A vertical flip is across-row: global = (reverse band order) ∘
//          (reverse rows within each tile). With K>1 the outer reversal ALONE
//          gives a block-permutation (a silent mirror that still RLE-decodes);
//          the per-tile code-0 flip supplies the missing intra-tile reversal.
//          In descending order the ragged last band (k<K) is processed FIRST,
//          landing at the top of dst' — handled by the same loop, no special case.
std::size_t prz_encode_layer_banded(const cv::Mat     &portrait,
                                    bool               final_x_mirror,
                                    int                band_cols,
                                    std::vector<char> &out)
{
    const int M = portrait.rows; // display_pixels_x
    const int N = portrait.cols; // display_pixels_y == number of dst rows
    const int K = std::max(1, band_cols);

    // Single bool drives the only family divergence: schedule direction.
    const bool descending = !final_x_mirror;          // FALSE/vertical: reverse
    const int  flip_code   = final_x_mirror ? 1 : 0;  // both families flip locally
    const int  n_bands     = (N + K - 1) / K;

    PrzRleEncoder enc(out);
    enc.begin();

    // TLS tile pool, reused across every layer on this thread (zero-malloc
    // steady state). It is exactly K rows x M cols; cols MUST equal M so that a
    // "top k rows" ROI stays CONTINUOUS — that lets the full-band (k==K) and the
    // ragged-band (k<K) cases share one allocation with no realloc, and lets
    // feed() scan k*M bytes linearly. Re-created only if M changes or K grows.
    thread_local cv::Mat tile_pool;
    if (tile_pool.type() != CV_8UC1 || tile_pool.cols != M || tile_pool.rows < K)
        tile_pool.create(K, M, CV_8UC1);

    for (int b = 0; b < n_bands; ++b) {
        const int band = descending ? (n_bands - 1 - b) : b; // scheduling index
        const int c0   = band * K;
        const int k    = std::min(K, N - c0); // dynamic ragged-band bound

        // Vertical M x k slab = K consecutive portrait columns = K dst rows.
        cv::Mat slab = portrait(cv::Range::all(), cv::Range(c0, c0 + k));

        // Rotate into the top k rows of the pool: a k x M continuous ROI. cv::
        // rotate's internal transpose sees a matching-size dst → no realloc.
        cv::Mat tile = tile_pool(cv::Rect(0, 0, M, k));
        cv::rotate(slab, tile, cv::ROTATE_90_CLOCKWISE); // -> k x M, in pool
        cv::flip(tile, tile, flip_code);                 // per-tile (both families)

        enc.feed(tile.data, k * M); // run state carries across band boundaries
    }

    return enc.finish();
}

} // namespace Slic3r

