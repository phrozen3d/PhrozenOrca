//----------------------------------------------------------------------------
// Anti-Grain Geometry - Version 2.4
// Copyright (C) 2002-2005 Maxim Shemanarev (http://www.antigrain.com)
//
// Stack Blur Algorithm invented by Mario Klingemann
// mario@quasimondo.com
// http://incubator.quasimondo.com/processing/fast_blur_deluxe.php
//
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//----------------------------------------------------------------------------

#ifndef AGG_BLUR_INCLUDED
#define AGG_BLUR_INCLUDED

#include <vector>
#include "agg_basics.h"

namespace agg
{
    // stack_blur_gray8 — separable 1-D stack blur applied horizontally then
    // vertically.  Works with any grayscale pixel format whose pixel_type
    // exposes a c[0] uint8 member and pix_value_ptr(x,y,len)/stride() API
    // (e.g. pixfmt_gray8 backed by a rendering_buffer).
    //
    // For radius r the kernel is a triangle of width 2r+1 with weights
    // 1,2,...,r+1,...,2,1 whose sum is (r+1)^2, so the output is
    //   out = sum / (r+1)^2
    // applied independently per row (rx) and per column (ry).
    //
    // Complexity: O(W*H) regardless of radius — the classic O(r) stack trick.

    template<class Img>
    void stack_blur_gray8(Img& img, unsigned rx, unsigned ry)
    {
        typedef typename Img::pixel_type pixel_type;

        const unsigned w = img.width();
        const unsigned h = img.height();
        if (w == 0 || h == 0) return;
        const unsigned wm = w - 1;
        const unsigned hm = h - 1;

        std::vector<unsigned> stack;

        // ── horizontal pass ──────────────────────────────────────────────
        if (rx > 0)
        {
            if (rx > 254) rx = 254;
            const unsigned div      = rx * 2 + 1;
            const unsigned divisor  = (rx + 1) * (rx + 1);
            stack.assign(div, 0u);

            for (unsigned y = 0; y < h; ++y)
            {
                unsigned sum = 0, sum_in = 0, sum_out = 0;

                // Load initial stack: indices [0..r] mirror left edge,
                // indices [r+1..2r] contain pixels [1..r].
                pixel_type* p0 = img.pix_value_ptr(0, y, 1);
                unsigned v0 = p0->c[0];
                for (unsigned i = 0; i <= rx; ++i)
                {
                    stack[i]  = v0;
                    sum      += v0 * (i + 1);
                    sum_out  += v0;
                }
                for (unsigned i = 1; i <= rx; ++i)
                {
                    unsigned xp = i <= wm ? i : wm;
                    unsigned v  = img.pix_value_ptr(xp, y, 1)->c[0];
                    stack[i + rx] = v;
                    sum    += v * (rx + 1 - i);
                    sum_in += v;
                }

                unsigned   stack_ptr = rx;
                unsigned   xp        = rx <= wm ? rx : wm;
                pixel_type* src_ptr  = img.pix_value_ptr(xp, y, 1);
                pixel_type* dst_ptr  = img.pix_value_ptr(0,  y, 1);

                for (unsigned x = 0; x < w; ++x)
                {
                    dst_ptr->c[0] = static_cast<int8u>(sum / divisor);
                    dst_ptr = dst_ptr->next();

                    sum -= sum_out;

                    unsigned stack_start = stack_ptr + div - rx;
                    if (stack_start >= div) stack_start -= div;
                    sum_out -= stack[stack_start];

                    if (xp < wm) { src_ptr = src_ptr->next(); ++xp; }

                    unsigned v_new       = src_ptr->c[0];
                    stack[stack_start]   = v_new;
                    sum_in              += v_new;
                    sum                 += sum_in;

                    if (++stack_ptr >= div) stack_ptr = 0;
                    sum_out += stack[stack_ptr];
                    sum_in  -= stack[stack_ptr];
                }
            }
        }

        // ── vertical pass ────────────────────────────────────────────────
        if (ry > 0)
        {
            if (ry > 254) ry = 254;
            const unsigned div      = ry * 2 + 1;
            const unsigned divisor  = (ry + 1) * (ry + 1);
            stack.assign(div, 0u);

            const int stride = img.stride();

            for (unsigned x = 0; x < w; ++x)
            {
                unsigned sum = 0, sum_in = 0, sum_out = 0;

                pixel_type* p0 = img.pix_value_ptr(x, 0, 1);
                unsigned v0 = p0->c[0];
                for (unsigned i = 0; i <= ry; ++i)
                {
                    stack[i]  = v0;
                    sum      += v0 * (i + 1);
                    sum_out  += v0;
                }
                for (unsigned i = 1; i <= ry; ++i)
                {
                    unsigned yp = i <= hm ? i : hm;
                    unsigned v  = img.pix_value_ptr(x, yp, 1)->c[0];
                    stack[i + ry] = v;
                    sum    += v * (ry + 1 - i);
                    sum_in += v;
                }

                unsigned   stack_ptr = ry;
                unsigned   yp        = ry <= hm ? ry : hm;
                pixel_type* src_ptr  = img.pix_value_ptr(x, yp, 1);
                pixel_type* dst_ptr  = img.pix_value_ptr(x, 0,  1);

                for (unsigned y = 0; y < h; ++y)
                {
                    dst_ptr->c[0] = static_cast<int8u>(sum / divisor);
                    dst_ptr = reinterpret_cast<pixel_type*>(
                                  reinterpret_cast<int8u*>(dst_ptr) + stride);

                    sum -= sum_out;

                    unsigned stack_start = stack_ptr + div - ry;
                    if (stack_start >= div) stack_start -= div;
                    sum_out -= stack[stack_start];

                    if (yp < hm)
                    {
                        src_ptr = reinterpret_cast<pixel_type*>(
                                      reinterpret_cast<int8u*>(src_ptr) + stride);
                        ++yp;
                    }

                    unsigned v_new       = src_ptr->c[0];
                    stack[stack_start]   = v_new;
                    sum_in              += v_new;
                    sum                 += sum_in;

                    if (++stack_ptr >= div) stack_ptr = 0;
                    sum_out += stack[stack_ptr];
                    sum_in  -= stack[stack_ptr];
                }
            }
        }
    }

} // namespace agg

#endif // AGG_BLUR_INCLUDED
