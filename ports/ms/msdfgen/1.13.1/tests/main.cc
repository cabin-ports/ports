// Builds a 4x4 square from linear edges, colors its edges and generates SDF
// and MSDF bitmaps whose pixels are checked against distances worked out by
// hand; then parses SVG paths from a string and from a file (tinyxml2), saves
// PNGs (libpng) and checks their header, and opens FreeType.  C++11 (C++14
// with MSVC), as the probe compiles it.
#include <msdfgen-ext.h>
#include <msdfgen.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace msdfgen;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "msdfgen test failed: %s\n", what);
        ++failures;
    }
}

bool approx(double a, double b) {
    return std::fabs(a - b) < 1e-6;
}

bool at(Point2 p, double x, double y) {
    return p.x == x && p.y == y;
}

// Clockwise with Y up, as font outlines wind: distances are positive inside.
Shape square() {
    Shape shape;
    Contour &contour = shape.addContour();
    contour.addEdge(EdgeHolder(Point2(0, 0), Point2(0, 4)));
    contour.addEdge(EdgeHolder(Point2(0, 4), Point2(4, 4)));
    contour.addEdge(EdgeHolder(Point2(4, 4), Point2(4, 0)));
    contour.addEdge(EdgeHolder(Point2(4, 0), Point2(0, 0)));
    return shape;
}

// An 8x8 bitmap whose pixel (x, y) samples the point (x - 1.5, y - 1.5), so
// pixels 2 to 5 lie inside the square.  Range(2) maps a distance d to
// (d + 1) / 2, which is above 0.5 exactly inside.
const Projection projection(Vector2(1), Vector2(2));

bool inside(int x, int y) {
    return x >= 2 && x <= 5 && y >= 2 && y <= 5;
}

void test_shape() {
    Shape shape = square();
    check(shape.validate() && shape.edgeCount() == 4, "validate and edgeCount");
    check(shape.contours[0].winding() == 1, "a clockwise contour winds +1");
    Shape::Bounds bounds = shape.getBounds();
    check(bounds.l == 0 && bounds.b == 0 && bounds.r == 4 && bounds.t == 4, "getBounds");

    Shape ccw;
    Contour &contour = ccw.addContour();
    contour.addEdge(EdgeHolder(Point2(0, 0), Point2(4, 0)));
    contour.addEdge(EdgeHolder(Point2(4, 0), Point2(4, 4)));
    contour.addEdge(EdgeHolder(Point2(4, 4), Point2(0, 4)));
    contour.addEdge(EdgeHolder(Point2(0, 4), Point2(0, 0)));
    check(ccw.contours[0].winding() == -1, "a counter-clockwise contour winds -1");
    ccw.orientContours();
    const std::vector<EdgeHolder> &edges = ccw.contours[0].edges;
    check(ccw.contours[0].winding() == 1 && at(edges[0]->point(0), 0, 0) &&
              at(edges[0]->point(1), 0, 4) && at(edges[3]->point(0), 4, 0),
          "orientContours reverses the contour");

    Shape open;
    Contour &gap = open.addContour();
    gap.addEdge(EdgeHolder(Point2(0, 0), Point2(1, 0)));
    gap.addEdge(EdgeHolder(Point2(2, 0), Point2(0, 0)));
    check(!open.validate(), "validate refuses an open contour");
}

void test_coloring() {
    Shape shape = square();
    shape.normalize();
    edgeColoringSimple(shape, 3.0);
    // Four corners, seed 0: the first color is the switch from cyan, and the
    // last edge avoids sharing two channels with the first.
    const std::vector<EdgeHolder> &edges = shape.contours[0].edges;
    check(edges[0]->color == MAGENTA && edges[1]->color == YELLOW && edges[2]->color == CYAN &&
              edges[3]->color == YELLOW,
          "edgeColoringSimple colors");
}

void test_sdf() {
    Shape shape = square();
    shape.normalize();
    Bitmap<float, 1> sdf(8, 8);
    generateSDF(sdf, shape, projection, Range(2));
    check(approx(*sdf(2, 3), 0.75), "SDF 0.5 inside the left edge");
    check(approx(*sdf(3, 3), 1.25), "SDF 1.5 inside");
    check(approx(*sdf(0, 3), -0.25), "SDF 1.5 outside the left edge");
    const double corner = (1 - 1.5 * std::sqrt(2.0)) / 2;
    check(approx(*sdf(0, 0), corner) && approx(*sdf(7, 7), corner), "SDF beyond the corners");
    bool signs = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            signs = signs && (*sdf(x, y) > .5f) == inside(x, y);
    check(signs, "SDF sign inside and outside");
}

void test_msdf() {
    Shape shape = square();
    shape.normalize();
    edgeColoringSimple(shape, 3.0);

    // Without error correction each channel is the pseudo-distance to the
    // nearest edge of that channel: red and blue reach the left edge, green
    // only the top, right and bottom ones, extending the bottom edge's line
    // past its end outside.
    Bitmap<float, 3> raw(8, 8);
    generateMSDF(raw, shape, projection, Range(2),
                 MSDFGeneratorConfig(true, ErrorCorrectionConfig(ErrorCorrectionConfig::DISABLED)));
    const float *in = raw(2, 3);
    check(approx(in[0], 0.75) && approx(in[1], 1.25) && approx(in[2], 0.75), "MSDF channels inside");
    const float *out = raw(0, 3);
    check(approx(out[0], -0.25) && approx(out[1], 1.25) && approx(out[2], -0.25), "MSDF channels outside");

    Bitmap<float, 3> msdf(8, 8);
    generateMSDF(msdf, shape, projection, Range(2));
    bool signs = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const float *p = msdf(x, y);
            signs = signs && (median(p[0], p[1], p[2]) > .5f) == inside(x, y);
        }
    check(signs, "MSDF median sign inside and outside");
}

void test_svg_path() {
    Shape shape;
    check(buildShapeFromSvgPath(shape, "M0 0 L0 4 Q4 4 4 0 Z"), "buildShapeFromSvgPath");
    check(shape.contours.size() == 1 && shape.contours[0].edges.size() == 3 && shape.validate(),
          "one closed contour of three edges");
    if (shape.contours.size() == 1 && shape.contours[0].edges.size() == 3) {
        const std::vector<EdgeHolder> &edges = shape.contours[0].edges;
        check(at(edges[0]->point(0), 0, 0) && at(edges[0]->point(1), 0, 4), "the line");
        check(at(edges[1]->point(.5), 3, 3) && at(edges[1]->point(1), 4, 0),
              "the quadratic curve through (3, 3)");
        check(at(edges[2]->point(0), 4, 0) && at(edges[2]->point(1), 0, 0), "Z closes the contour");
    }
    Shape bad;
    check(!buildShapeFromSvgPath(bad, "M0 0 L1"), "a truncated path is refused");
}

void test_svg_file(const std::string &dir) {
    const std::string path = dir + "/port-probe-msdfgen.svg";
    if (FILE *file = std::fopen(path.c_str(), "wb")) {
        std::fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\" "
                   "viewBox=\"1 2 4 4\"><path d=\"M9 9 L9 10 L10 10 Z\"/>"
                   "<path d=\"M1 2 L1 6 L5 6 L5 2 Z\"/></svg>",
                   file);
        std::fclose(file);
    }
    Shape shape;
    Shape::Bounds viewBox = {0, 0, 0, 0};
    const int flags = loadSvgShape(shape, viewBox, path.c_str());
    check(flags == (SVG_IMPORT_SUCCESS_FLAG | SVG_IMPORT_INCOMPLETE_FLAG),
          "loadSvgShape reads the last of two paths");
    check(viewBox.l == 1 && viewBox.b == 2 && viewBox.r == 5 && viewBox.t == 6, "the viewBox");
    check(shape.getYAxisOrientation() == Y_DOWNWARD, "SVG coordinates point down");
    check(shape.contours.size() == 1 && shape.contours[0].edges.size() == 4, "four edges");
    if (shape.contours.size() == 1 && shape.contours[0].edges.size() == 4) {
        const std::vector<EdgeHolder> &edges = shape.contours[0].edges;
        check(at(edges[0]->point(0), 1, 2) && at(edges[1]->point(0), 1, 6) &&
                  at(edges[2]->point(0), 5, 6) && at(edges[3]->point(0), 5, 2) &&
                  at(edges[3]->point(1), 1, 2),
              "the path's points");
    }
    std::remove(path.c_str());
    check(loadSvgShape(shape, viewBox, path.c_str()) == SVG_IMPORT_FAILURE,
          "loadSvgShape of a missing file");
}

unsigned long crc32(const std::vector<unsigned char> &bytes, size_t begin, size_t end) {
    unsigned long crc = 0xffffffffUL;
    for (size_t i = begin; i < end; ++i) {
        crc ^= bytes[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1)));
    }
    return (crc ^ 0xffffffffUL) & 0xffffffffUL;
}

unsigned long be32(const std::vector<unsigned char> &bytes, size_t at) {
    return (unsigned long) bytes[at] << 24 | (unsigned long) bytes[at + 1] << 16 |
           (unsigned long) bytes[at + 2] << 8 | bytes[at + 3];
}

// The PNG signature, an IHDR chunk with its CRC, the first IDAT and IEND.
bool png_header(const std::string &path, unsigned long width, unsigned long height, int colorType) {
    std::vector<unsigned char> bytes;
    if (FILE *file = std::fopen(path.c_str(), "rb")) {
        int c;
        while ((c = std::fgetc(file)) != EOF)
            bytes.push_back((unsigned char) c);
        std::fclose(file);
    }
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    static const unsigned char iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82};
    if (bytes.size() < 8 + 25 + 8 + 12)
        return false;
    for (int i = 0; i < 8; ++i)
        if (bytes[i] != signature[i])
            return false;
    for (int i = 0; i < 12; ++i)
        if (bytes[bytes.size() - 12 + i] != iend[i])
            return false;
    return be32(bytes, 8) == 13 && std::string(bytes.begin() + 12, bytes.begin() + 16) == "IHDR" &&
           be32(bytes, 16) == width && be32(bytes, 20) == height && bytes[24] == 8 &&
           bytes[25] == colorType && bytes[26] == 0 && bytes[27] == 0 && bytes[28] == 0 &&
           be32(bytes, 29) == crc32(bytes, 12, 29) &&
           std::string(bytes.begin() + 37, bytes.begin() + 41) == "IDAT";
}

void test_png(const std::string &dir) {
    Shape shape = square();
    shape.normalize();
    edgeColoringSimple(shape, 3.0);
    Bitmap<float, 1> sdf(8, 6);
    generateSDF(sdf, shape, projection, Range(2));
    Bitmap<float, 3> msdf(5, 7);
    generateMSDF(msdf, shape, projection, Range(2));

    const std::string gray = dir + "/port-probe-msdfgen-sdf.png";
    check(savePng(sdf, gray.c_str()), "savePng of an SDF");
    check(png_header(gray, 8, 6, 0), "an 8x6 grayscale PNG");
    std::remove(gray.c_str());
    const std::string rgb = dir + "/port-probe-msdfgen-msdf.png";
    check(savePng(msdf, rgb.c_str()), "savePng of an MSDF");
    check(png_header(rgb, 5, 7, 2), "a 5x7 RGB PNG");
    std::remove(rgb.c_str());
    check(!savePng(sdf, (dir + "/missing/port-probe-msdfgen.png").c_str()),
          "savePng into a missing directory fails");
}

void test_freetype(const std::string &dir) {
    FreetypeHandle *ft = initializeFreetype();
    check(ft != NULL, "initializeFreetype");
    if (ft == NULL)
        return;
    const byte junk[4] = {1, 2, 3, 4};
    check(loadFontData(ft, junk, 4) == NULL, "loadFontData refuses data that is no font");
    check(loadFont(ft, (dir + "/missing.ttf").c_str()) == NULL, "loadFont of a missing file");
    deinitializeFreetype(ft);
}

}  // namespace

int main() {
    test_shape();
    test_coloring();
    test_sdf();
    test_msdf();
    test_svg_path();
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory.
    const char *dir = std::getenv("CABIN_PROFILE_DIR");
    check(dir != NULL, "CABIN_PROFILE_DIR");
    if (dir != NULL) {
        test_svg_file(dir);
        test_png(dir);
        test_freetype(dir);
    }
    if (failures != 0)
        return 1;
    std::puts("msdfgen: ok");
    return 0;
}
