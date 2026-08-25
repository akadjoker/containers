#pragma once

#include <cstdint>

#include "vector.hpp"

namespace ct
{

    // MaxRects atlas packer. Define CT_RECTPACKER_IMPLEMENTATION in exactly one
    // translation unit before including this header to emit its implementation.
    class RectPacker
    {
    public:
        struct Input
        {
            int32_t id = 0;
            int32_t width = 0;
            int32_t height = 0;
        };

        struct PlacedRect
        {
            int32_t id = 0;
            int32_t x = 0;
            int32_t y = 0;
            int32_t width = 0;
            int32_t height = 0;
        };

        struct Page
        {
            Vector<PlacedRect> placed;
            int32_t atlas_width = 0;
            int32_t atlas_height = 0;
        };

        // Packs inputs into as many power-of-two pages as necessary. Page
        // dimensions never exceed the supplied limits. padding is reserved on
        // the right and bottom of each input but omitted from output sizes.
        // Invalid or impossible inputs are ignored; rectangles are not rotated.
        static Vector<Page> pack_pages(Vector<Input> inputs, int32_t max_width,
                                       int32_t max_height, int32_t padding = 1);

    private:
        struct FreeRect
        {
            int32_t x;
            int32_t y;
            int32_t w;
            int32_t h;
        };

        struct RectItem
        {
            int32_t id;
            int32_t w;
            int32_t h;
        };

        struct PackResult
        {
            Page page;
            Vector<RectItem> remaining;
        };

        static int32_t next_power_of_two(int32_t value);
        static int32_t largest_power_of_two_at_most(int32_t value);
        static bool contains(const FreeRect &outer, const FreeRect &inner);
        static void prune_free_rects(Vector<FreeRect> &free_rects);
        static int find_best_free_rect(const Vector<FreeRect> &free_rects, int32_t width,
                                       int32_t height);
        static bool intersects(const FreeRect &free_rect, int32_t x, int32_t y,
                               int32_t width, int32_t height);
        static void split_free_rects(Vector<FreeRect> &free_rects, int32_t x, int32_t y,
                                     int32_t width, int32_t height);
        static PackResult try_pack(const Vector<RectItem> &items, int32_t width,
                                   int32_t height);
        static Page pack_one_page(Vector<RectItem> &remaining, int32_t max_width,
                                  int32_t max_height);
    };

} // namespace ct

#ifdef CT_RECTPACKER_IMPLEMENTATION

#include <limits>

#include "sort.hpp"

namespace ct
{

    int32_t RectPacker::next_power_of_two(int32_t value)
    {
        if (value <= 1)
            return 1;
        int32_t power = 1;
        while (power < value && power <= (std::numeric_limits<int32_t>::max)() / 2)
            power <<= 1;
        return power >= value ? power : 0;
    }

    int32_t RectPacker::largest_power_of_two_at_most(int32_t value)
    {
        if (value < 1)
            return 0;
        int32_t power = 1;
        while (power <= value / 2)
            power <<= 1;
        return power;
    }

    bool RectPacker::contains(const FreeRect &outer, const FreeRect &inner)
    {
        return inner.x >= outer.x && inner.y >= outer.y &&
               inner.x + inner.w <= outer.x + outer.w &&
               inner.y + inner.h <= outer.y + outer.h;
    }

    void RectPacker::prune_free_rects(Vector<FreeRect> &free_rects)
    {
        for (size_t i = 0; i < free_rects.size(); ++i)
        {
            bool removed = false;
            for (size_t j = 0; j < free_rects.size(); ++j)
            {
                if (i != j && contains(free_rects[j], free_rects[i]))
                {
                    free_rects.erase(free_rects.begin() + i);
                    removed = true;
                    break;
                }
            }
            if (removed)
                --i;
        }
    }

    int RectPacker::find_best_free_rect(const Vector<FreeRect> &free_rects, int32_t width,
                                        int32_t height)
    {
        const int32_t maximum = (std::numeric_limits<int32_t>::max)();
        int best = -1;
        int32_t best_long_side = maximum;
        int32_t best_short_side = maximum;
        int64_t best_area = (std::numeric_limits<int64_t>::max)();

        for (size_t i = 0; i < free_rects.size(); ++i)
        {
            const FreeRect &free_rect = free_rects[i];
            if (free_rect.w < width || free_rect.h < height)
                continue;

            const int32_t horizontal = free_rect.w - width;
            const int32_t vertical = free_rect.h - height;
            const int32_t long_side = horizontal > vertical ? horizontal : vertical;
            const int32_t short_side = horizontal < vertical ? horizontal : vertical;
            const int64_t area = static_cast<int64_t>(free_rect.w) * free_rect.h;
            if (long_side < best_long_side ||
                (long_side == best_long_side && short_side < best_short_side) ||
                (long_side == best_long_side && short_side == best_short_side &&
                 area < best_area))
            {
                best_long_side = long_side;
                best_short_side = short_side;
                best_area = area;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    bool RectPacker::intersects(const FreeRect &free_rect, int32_t x, int32_t y,
                                int32_t width, int32_t height)
    {
        return x < free_rect.x + free_rect.w && x + width > free_rect.x &&
               y < free_rect.y + free_rect.h && y + height > free_rect.y;
    }

    void RectPacker::split_free_rects(Vector<FreeRect> &free_rects, int32_t x, int32_t y,
                                      int32_t width, int32_t height)
    {
        for (size_t i = 0; i < free_rects.size();)
        {
            const FreeRect free_rect = free_rects[i];
            if (!intersects(free_rect, x, y, width, height))
            {
                ++i;
                continue;
            }

            free_rects.erase(free_rects.begin() + i);
            const int32_t right = x + width;
            const int32_t bottom = y + height;
            const int32_t free_right = free_rect.x + free_rect.w;
            const int32_t free_bottom = free_rect.y + free_rect.h;
            if (y > free_rect.y)
                free_rects.push_back({free_rect.x, free_rect.y, free_rect.w, y - free_rect.y});
            if (bottom < free_bottom)
                free_rects.push_back({free_rect.x, bottom, free_rect.w, free_bottom - bottom});
            if (x > free_rect.x)
                free_rects.push_back({free_rect.x, free_rect.y, x - free_rect.x, free_rect.h});
            if (right < free_right)
                free_rects.push_back({right, free_rect.y, free_right - right, free_rect.h});
        }
        prune_free_rects(free_rects);
    }

    RectPacker::PackResult RectPacker::try_pack(const Vector<RectItem> &items, int32_t width,
                                                int32_t height)
    {
        PackResult result;
        result.page.atlas_width = width;
        result.page.atlas_height = height;
        Vector<FreeRect> free_rects;
        free_rects.push_back({0, 0, width, height});

        for (const RectItem &item : items)
        {
            const int index = find_best_free_rect(free_rects, item.w, item.h);
            if (index < 0)
            {
                result.remaining.push_back(item);
                continue;
            }

            const int32_t x = free_rects[static_cast<size_t>(index)].x;
            const int32_t y = free_rects[static_cast<size_t>(index)].y;
            split_free_rects(free_rects, x, y, item.w, item.h);
            result.page.placed.push_back({item.id, x, y, item.w, item.h});
        }
        return result;
    }

    RectPacker::Page RectPacker::pack_one_page(Vector<RectItem> &remaining, int32_t max_width,
                                               int32_t max_height)
    {
        PackResult maximum = try_pack(remaining, max_width, max_height);
        if (maximum.page.placed.empty())
            return static_cast<Page &&>(maximum.page);

        Vector<RectItem> placed_items;
        placed_items.reserve(remaining.size() - maximum.remaining.size());
        Vector<RectItem> unplaced = maximum.remaining;
        for (const RectItem &item : remaining)
        {
            size_t match = 0;
            while (match < unplaced.size() &&
                   (unplaced[match].id != item.id || unplaced[match].w != item.w ||
                    unplaced[match].h != item.h))
                ++match;
            if (match == unplaced.size())
                placed_items.push_back(item);
            else
                unplaced.erase(unplaced.begin() + match);
        }

        int32_t minimum_width = placed_items[0].w;
        int32_t minimum_height = placed_items[0].h;
        for (const RectItem &item : placed_items)
        {
            minimum_width = minimum_width > item.w ? minimum_width : item.w;
            minimum_height = minimum_height > item.h ? minimum_height : item.h;
        }

        const int32_t first_width = next_power_of_two(minimum_width);
        const int32_t first_height = next_power_of_two(minimum_height);
        Page best = maximum.page;
        int64_t best_area = static_cast<int64_t>(max_width) * max_height;
        for (int32_t width = first_width;; width *= 2)
        {
            for (int32_t height = first_height;; height *= 2)
            {
                const int64_t area = static_cast<int64_t>(width) * height;
                const int32_t longest_side = width > height ? width : height;
                const int32_t best_longest_side = best.atlas_width > best.atlas_height
                                                      ? best.atlas_width
                                                      : best.atlas_height;
                if (area <= best_area)
                {
                    PackResult candidate = try_pack(placed_items, width, height);
                    if (candidate.remaining.empty() &&
                        (area < best_area ||
                         (area == best_area && longest_side < best_longest_side)))
                    {
                        best = static_cast<Page &&>(candidate.page);
                        best_area = area;
                    }
                }
                if (height == max_height)
                    break;
            }
            if (width == max_width)
                break;
        }

        remaining = static_cast<Vector<RectItem> &&>(maximum.remaining);
        return best;
    }

    Vector<RectPacker::Page> RectPacker::pack_pages(Vector<Input> inputs, int32_t max_width,
                                                     int32_t max_height, int32_t padding)
    {
        const int32_t cap_width = largest_power_of_two_at_most(max_width);
        const int32_t cap_height = largest_power_of_two_at_most(max_height);
        if (cap_width == 0 || cap_height == 0)
            return {};

        padding = padding > 0 ? padding : 0;
        Vector<RectItem> items;
        items.reserve(inputs.size());
        for (const Input &input : inputs)
        {
            const int64_t padded_width = static_cast<int64_t>(input.width) + padding;
            const int64_t padded_height = static_cast<int64_t>(input.height) + padding;
            if (input.width <= 0 || input.height <= 0 || padded_width > cap_width ||
                padded_height > cap_height)
                continue;
            items.push_back({input.id, static_cast<int32_t>(padded_width),
                             static_cast<int32_t>(padded_height)});
        }

        ct::sort(items.begin(), items.end(), [](const RectItem &a, const RectItem &b)
        {
            const int64_t area_a = static_cast<int64_t>(a.w) * a.h;
            const int64_t area_b = static_cast<int64_t>(b.w) * b.h;
            if (area_a != area_b)
                return area_a > area_b;
            const int32_t max_a = a.w > a.h ? a.w : a.h;
            const int32_t max_b = b.w > b.h ? b.w : b.h;
            if (max_a != max_b)
                return max_a > max_b;
            return a.w > b.w;
        });

        Vector<Page> pages;
        while (!items.empty())
        {
            Page page = pack_one_page(items, cap_width, cap_height);
            if (page.placed.empty())
                break;
            for (PlacedRect &placed : page.placed)
            {
                placed.width -= padding;
                placed.height -= padding;
            }
            pages.push_back(static_cast<Page &&>(page));
        }
        return pages;
    }

} // namespace ct

#endif // CT_RECTPACKER_IMPLEMENTATION
