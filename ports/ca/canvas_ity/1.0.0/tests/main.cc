// Draws on canvas_ity::canvas and reads the pixels back with get_image_data:
// exact filled rectangles (directly and as a translated path), an exact
// stroked line, an antialiased stroked circle with bounded coverage, and path
// hit-testing.
#define CANVAS_ITY_IMPLEMENTATION
#include <canvas_ity.hpp>

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

const int S = 64;

struct Pixels {
    std::vector<unsigned char> data;

    explicit Pixels(canvas_ity::canvas &c) : data(S * S * 4, 7) { c.get_image_data(&data[0], S, S, S * 4, 0, 0); }

    const unsigned char *at(int x, int y) const { return &data[(y * S + x) * 4]; }

    bool is(int x, int y, int r, int g, int b, int a) const
    {
        const unsigned char *p = at(x, y);
        return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
    }
};

// Only pixels with x0 <= x < x1 and y0 <= y < y1 carry the color, exactly.
bool exact_block(const Pixels &px, int x0, int y0, int x1, int y1, int r, int g, int b)
{
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (!(inside ? px.is(x, y, r, g, b, 255) : px.is(x, y, 0, 0, 0, 0))) {
                return false;
            }
        }
    }
    return true;
}

void test_fill()
{
    canvas_ity::canvas c(S, S);
    check(exact_block(Pixels(c), 0, 0, 0, 0, 0, 0, 0), "a new canvas is transparent black");
    c.set_color(canvas_ity::fill_style, 1.0f, 0.0f, 0.0f, 1.0f);
    c.fill_rectangle(8.0f, 8.0f, 16.0f, 16.0f);
    check(exact_block(Pixels(c), 8, 8, 24, 24, 255, 0, 0), "fill_rectangle covers exactly its pixels");
}

void test_fill_path()
{
    canvas_ity::canvas c(S, S);
    c.set_color(canvas_ity::fill_style, 1.0f, 0.0f, 0.0f, 1.0f);
    c.translate(8.0f, 8.0f);
    c.begin_path();
    c.rectangle(0.0f, 0.0f, 16.0f, 16.0f);
    c.fill();
    check(exact_block(Pixels(c), 8, 8, 24, 24, 255, 0, 0), "a translated rectangle path fills exactly its pixels");
}

void test_stroke_line()
{
    canvas_ity::canvas c(S, S);
    c.set_color(canvas_ity::stroke_style, 0.0f, 0.0f, 1.0f, 1.0f);
    c.set_line_width(4.0f);
    c.begin_path();
    c.move_to(8.0f, 40.0f);
    c.line_to(56.0f, 40.0f);
    c.stroke();
    // Butt caps: the stroke spans x 8..56 and y 38..42.
    check(exact_block(Pixels(c), 8, 38, 56, 42, 0, 0, 255), "a stroked line covers exactly its pixels");
}

void test_stroke_circle()
{
    canvas_ity::canvas c(S, S);
    c.set_color(canvas_ity::stroke_style, 0.0f, 1.0f, 0.0f, 1.0f);
    c.set_line_width(2.0f);
    c.begin_path();
    c.arc(32.0f, 32.0f, 16.0f, 0.0f, 6.2831853f);
    c.stroke();
    const Pixels px(c);
    double coverage = 0.0;
    bool green = true;
    bool partial = false;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const unsigned char *p = px.at(x, y);
            coverage += p[3] / 255.0;
            green = green && p[0] == 0 && p[2] == 0 && (p[3] == 0 || p[1] == 255);
            partial = partial || (p[3] > 0 && p[3] < 255);
        }
    }
    check(green, "the stroke is green wherever it is drawn");
    check(partial, "the circle's edges are antialiased");
    // Total alpha within 1% of the ring's area, 2 * pi * 16 * 2.
    const double ring = 2.0 * 3.14159265 * 16.0 * 2.0;
    check(coverage > ring * 0.99 && coverage < ring * 1.01, "the circle's coverage matches its ring");
    // (47, 31) lies wholly in the ring: its corners are 15 to 16.03 from the
    // circle's centre.
    check(px.is(47, 31, 0, 255, 0, 255) && px.is(31, 31, 0, 0, 0, 0) && px.is(32, 32, 0, 0, 0, 0) &&
              px.is(0, 0, 0, 0, 0, 0) && px.is(63, 63, 0, 0, 0, 0),
          "the ring is opaque on the circle and clear inside and outside");
}

void test_hit()
{
    canvas_ity::canvas c(S, S);
    c.begin_path();
    c.rectangle(10.0f, 10.0f, 20.0f, 20.0f);
    check(c.is_point_in_path(15.0f, 25.0f) && !c.is_point_in_path(5.0f, 5.0f) && !c.is_point_in_path(35.0f, 15.0f),
          "is_point_in_path");
}

} // namespace

int main()
{
    test_fill();
    test_fill_path();
    test_stroke_line();
    test_stroke_circle();
    test_hit();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("canvas_ity 1.0.0 ok");
    return 0;
}
