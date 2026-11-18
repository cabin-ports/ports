// Searches and neighbor queries are checked against brute-force scans of the same boxes, and the
// serialized header and size against the layout mourner/flatbush's index.js writes.
#include <flatbush.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

using flatbush::Box;
using flatbush::Point;

std::uint32_t seed = 12345;
double nextCoord(double scale) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) / double(1u << 24) * scale;
}

template <class T>
bool intersects(const Box<T> &a, const Box<T> &b) {
    return a.mMaxX >= b.mMinX && a.mMinX <= b.mMaxX && a.mMaxY >= b.mMinY && a.mMinY <= b.mMaxY;
}

template <class T>
double distanceSquared(const Point<T> &p, const Box<T> &b) {
    const double x = p.mX, y = p.mY;
    const double dx = x < b.mMinX ? b.mMinX - x : (x > b.mMaxX ? x - b.mMaxX : 0.0);
    const double dy = y < b.mMinY ? b.mMinY - y : (y > b.mMaxY ? y - b.mMaxY : 0.0);
    return dx * dx + dy * dy;
}

template <class T>
std::vector<size_t> bruteSearch(const std::vector<Box<T>> &boxes, const Box<T> &q) {
    std::vector<size_t> ids;
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (intersects(boxes[i], q)) ids.push_back(i);
    }
    return ids;
}

std::vector<size_t> sorted(std::vector<size_t> ids) {
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Expected serialized size: an 8-byte header, then four coordinates per node, then one index per
// node, 16-bit below 16384 nodes and 32-bit from there.
size_t expectedBytes(size_t numItems, size_t nodeSize, size_t coordBytes) {
    size_t n = numItems, numNodes = numItems;
    do {
        n = (n + nodeSize - 1) / nodeSize;
        numNodes += n;
    } while (n != 1);
    return 8 + numNodes * 4 * coordBytes + numNodes * (numNodes < 16384 ? 2 : 4);
}

template <class T>
bool headerMatches(const flatbush::Flatbush<T> &index, std::uint8_t typeIndex, std::uint16_t nodeSize,
                   std::uint32_t numItems) {
    const auto bytes = index.data();
    std::uint16_t storedNodeSize = 0;
    std::uint32_t storedNumItems = 0;
    std::memcpy(&storedNodeSize, bytes.data() + 2, 2);
    std::memcpy(&storedNumItems, bytes.data() + 4, 4);
    return bytes[0] == 0xfb && bytes[1] == ((3 << 4) | typeIndex) && storedNodeSize == nodeSize &&
           storedNumItems == numItems;
}

// The k nearest boxes must have the k smallest distances, in non-decreasing order.  Distances are
// compared to rounding: compilers may fuse dx * dx + dy * dy into a multiply-add in one place and
// not another.
template <class T>
bool neighborsMatch(const std::vector<Box<T>> &boxes, const Point<T> &p, const std::vector<size_t> &got,
                    size_t k) {
    std::vector<double> all;
    for (const auto &b : boxes) all.push_back(distanceSquared(p, b));
    std::sort(all.begin(), all.end());
    if (got.size() != std::min(k, boxes.size())) return false;
    const auto ids = sorted(got);
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return false;
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] >= boxes.size() || std::fabs(distanceSquared(p, boxes[got[i]]) - all[i]) > 1e-9 * (1 + all[i]))
            return false;
    }
    return true;
}

void testDoubleIndex() {
    std::vector<Box<double>> boxes;
    flatbush::FlatbushBuilder<double> builder(1000);
    for (size_t i = 0; i < 1000; ++i) {
        const double x = nextCoord(100), y = nextCoord(100);
        const Box<double> box{x, y, x + nextCoord(5), y + nextCoord(5)};
        CHECK(builder.add(box) == i);
        boxes.push_back(box);
    }
    // A point is indexed as a zero-area box.
    CHECK(builder.add(Point<double>{50, 50}) == 1000);
    boxes.push_back(Box<double>{50, 50, 50, 50});
    const auto index = builder.finish();

    CHECK(index.numItems() == 1001 && index.nodeSize() == 16 && !index.isView());
    CHECK(headerMatches(index, 8, 16, 1001));
    CHECK(index.data().size() == expectedBytes(1001, 16, 8));
    Box<double> extent = boxes[0];
    for (const auto &b : boxes) {
        extent.mMinX = std::min(extent.mMinX, b.mMinX);
        extent.mMinY = std::min(extent.mMinY, b.mMinY);
        extent.mMaxX = std::max(extent.mMaxX, b.mMaxX);
        extent.mMaxY = std::max(extent.mMaxY, b.mMaxY);
    }
    const auto &bounds = index.bounds();
    CHECK(bounds.mMinX == extent.mMinX && bounds.mMinY == extent.mMinY && bounds.mMaxX == extent.mMaxX &&
          bounds.mMaxY == extent.mMaxY);

    // The wide queries contain whole subtrees, which the index copies without testing each box.
    const Box<double> queries[] = {{40, 40, 60, 60}, {0, 0, 10, 100}, {-10, -10, -1, -1},
                                   {0, 0, 200, 200}, {0, 0, 60, 100},  {10, 20, 90, 95}};
    for (const auto &q : queries) {
        CHECK(sorted(index.search(q)) == bruteSearch(boxes, q));
    }
    // Touching an edge counts as intersecting.
    const Box<double> &edge = boxes[7];
    const auto touching = index.search({edge.mMaxX, edge.mMaxY, edge.mMaxX + 1, edge.mMaxY + 1});
    CHECK(std::find(touching.begin(), touching.end(), size_t(7)) != touching.end());
    CHECK(sorted(index.search({50, 50, 50, 50})) == bruteSearch(boxes, Box<double>{50, 50, 50, 50}));
    CHECK(sorted(index.search({50.25, 49, 51, 51})) == bruteSearch(boxes, Box<double>{50.25, 49, 51, 51}));

    const auto isEven = [](size_t id, const Box<double> &) { return id % 2 == 0; };
    for (const auto &q : queries) {
        std::vector<size_t> evens;
        for (size_t id : bruteSearch(boxes, q)) {
            if (id % 2 == 0) evens.push_back(id);
        }
        const auto evenIds = index.search(q, isEven);
        CHECK(sorted(evenIds) == evens);
        const auto limited = index.search(q, isEven, 3);
        CHECK(limited.size() == std::min<size_t>(3, evens.size()) && limited.size() <= evenIds.size() &&
              std::equal(limited.begin(), limited.end(), evenIds.begin()));
        CHECK(index.search(q, isEven, 0).empty());
    }

    const Point<double> p{37.5, 61.25};
    // From outside every box the nearest one is at a positive distance, which bounds the search.
    const Point<double> outside{-3, 47};
    CHECK(neighborsMatch(boxes, p, index.neighbors(p, 1), 1));
    CHECK(neighborsMatch(boxes, outside, index.neighbors(outside, 1), 1));
    CHECK(neighborsMatch(boxes, p, index.neighbors(p, 10), 10));
    CHECK(neighborsMatch(boxes, p, index.neighbors(p), boxes.size()));

    // maxDistance is a Euclidean radius.
    std::vector<size_t> within;
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (distanceSquared(p, boxes[i]) <= 4.0 * 4.0) within.push_back(i);
    }
    CHECK(sorted(index.neighbors(p, flatbush::gMaxResults, 4.0)) == within);
    // With a zero radius, only boxes containing or touching the point qualify.
    CHECK(sorted(index.neighbors(Point<double>{50, 50}, flatbush::gMaxResults, 0.0)) ==
          bruteSearch(boxes, Box<double>{50, 50, 50, 50}));

    const auto isOdd = [](size_t id, const Box<double> &) { return id % 2 == 1; };
    const auto odd = index.neighbors(p, 5, flatbush::gMaxDistance, isOdd);
    std::vector<Box<double>> oddBoxes;
    for (size_t i = 1; i < boxes.size(); i += 2) oddBoxes.push_back(boxes[i]);
    bool allOdd = odd.size() == 5;
    std::vector<size_t> halved;
    for (size_t id : odd) {
        allOdd = allOdd && id % 2 == 1;
        halved.push_back(id / 2);
    }
    CHECK(allOdd && neighborsMatch(oddBoxes, p, halved, 5));
    const auto nearestOdd = index.neighbors(outside, 1, flatbush::gMaxDistance, isOdd);
    CHECK(nearestOdd.size() == 1 && nearestOdd[0] % 2 == 1 &&
          neighborsMatch(oddBoxes, outside, std::vector<size_t>{nearestOdd[0] / 2}, 1));

    // A distance callback replaces the metric, and maxDistance is compared with its result.
    const auto manhattan = [](const Point<double> &q, const Box<double> &b) {
        const double dx = q.mX < b.mMinX ? b.mMinX - q.mX : (q.mX > b.mMaxX ? q.mX - b.mMaxX : 0.0);
        const double dy = q.mY < b.mMinY ? b.mMinY - q.mY : (q.mY > b.mMaxY ? q.mY - b.mMaxY : 0.0);
        return dx + dy;
    };
    const auto byManhattan = index.neighbors(p, flatbush::gMaxResults, 3.0, isEven, manhattan);
    std::vector<size_t> wantManhattan;
    for (size_t i = 0; i < boxes.size(); i += 2) {
        if (manhattan(p, boxes[i]) <= 3.0) wantManhattan.push_back(i);
    }
    bool ordered = true;
    for (size_t i = 1; i < byManhattan.size(); ++i) {
        ordered = ordered && manhattan(p, boxes[byManhattan[i - 1]]) <= manhattan(p, boxes[byManhattan[i]]);
    }
    CHECK(!wantManhattan.empty() && ordered && sorted(byManhattan) == wantManhattan);

    // Round trips: a copy, a moved vector and a borrowed view all answer like the original.
    const auto bytes = index.data();
    const auto copy = flatbush::FlatbushBuilder<double>::from(bytes.data(), bytes.size());
    const auto owned = flatbush::FlatbushBuilder<double>::from(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    const std::vector<std::uint8_t> storage(bytes.begin(), bytes.end());
    const auto view = flatbush::FlatbushBuilder<double>::fromView(
        flatbush::span<const std::uint8_t>(storage.data(), storage.size()));
    CHECK(view.isView() && !copy.isView() && view.data().data() == storage.data());
    for (const flatbush::Flatbush<double> *other : {&copy, &owned, &view}) {
        CHECK(other->numItems() == 1001 && other->search(queries[1]) == index.search(queries[1]));
        CHECK(other->neighbors(p, 7) == index.neighbors(p, 7));
    }

    // The magic byte must be present, and a view needs suitably aligned bytes.
    std::vector<std::uint8_t> corrupt(bytes.begin(), bytes.end());
    corrupt[0] = 0;
    bool badMagic = false;
    try {
        static_cast<void>(flatbush::FlatbushBuilder<double>::from(std::move(corrupt)));
    } catch (const std::invalid_argument &) {
        badMagic = true;
    }
    CHECK(badMagic);
    std::vector<std::uint8_t> shifted(bytes.size() + 1);
    std::copy(bytes.begin(), bytes.end(), shifted.begin() + 1);
    bool misaligned = false;
    try {
        static_cast<void>(flatbush::FlatbushBuilder<double>::fromView(
            flatbush::span<const std::uint8_t>(shifted.data() + 1, bytes.size())));
    } catch (const std::invalid_argument &) {
        misaligned = true;
    }
    CHECK(misaligned);
}

void testBuilder() {
    flatbush::FlatbushBuilder<std::int32_t> builder(4, 2);
    bool empty = false;
    try {
        static_cast<void>(builder.finish());
    } catch (const std::invalid_argument &) {
        empty = true;
    }
    CHECK(empty);

    // finish() empties the builder, so it can build an independent second index.
    CHECK(builder.add({1, 1, 2, 2}) == 0 && builder.add({5, 5, 6, 6}) == 1);
    const auto first = builder.finish();
    CHECK(builder.add({10, 10, 11, 11}) == 0);
    const auto second = builder.finish();
    CHECK(first.numItems() == 2 && first.nodeSize() == 2 && second.numItems() == 1);
    CHECK(sorted(first.search({0, 0, 5, 5})) == (std::vector<size_t>{0, 1}));
    CHECK(second.search({0, 0, 5, 5}).empty() && second.search({10, 10, 10, 10}) == std::vector<size_t>{0});
    CHECK(headerMatches(first, 5, 2, 2) && first.data().size() == expectedBytes(2, 2, 4));
    // int32_t and float boxes are the same size, so only the encoded type tells them apart.
    bool wrongType = false;
    try {
        static_cast<void>(flatbush::FlatbushBuilder<float>::from(first.data().data(), first.data().size()));
    } catch (const std::invalid_argument &) {
        wrongType = true;
    }
    CHECK(wrongType);

    // clear() discards what was added.
    builder.add({1, 1, 1, 1});
    builder.clear();
    builder.add({3, 3, 3, 3});
    const auto cleared = builder.finish();
    CHECK(cleared.numItems() == 1 && cleared.search({1, 1, 1, 1}).empty());
}

// Two items fit in one node, and the nearer one is added after the farther one.
void testNearestInOneNode() {
    flatbush::FlatbushBuilder<double> builder(2);
    builder.add({10, 0, 11, 1});
    builder.add({0, 8, 1, 9});
    const auto index = builder.finish();
    const auto any = [](size_t, const Box<double> &) { return true; };
    const Point<double> origin{0, 0};
    CHECK(index.neighbors(origin, 1) == std::vector<size_t>{1});
    CHECK(index.neighbors(origin, 1, flatbush::gMaxDistance, any) == std::vector<size_t>{1});
    CHECK(index.neighbors(origin, 2, 9.5) == std::vector<size_t>{1});
}

// Over 16383 nodes the index switches to 32-bit node indices.
void testWideIndex() {
    std::vector<Box<std::int32_t>> boxes;
    flatbush::FlatbushBuilder<std::int32_t> builder(20000);
    for (std::int32_t i = 0; i < 20000; ++i) {
        const std::int32_t x = (i % 200) * 10, y = (i / 200) * 10;
        const Box<std::int32_t> box{x, y, x + 3 + i % 5, y + 3 + i % 7};
        builder.add(box);
        boxes.push_back(box);
    }
    const auto index = builder.finish();
    CHECK(headerMatches(index, 5, 16, 20000));
    CHECK(index.data().size() == expectedBytes(20000, 16, 4));
    const Box<std::int32_t> queries[] = {{95, 95, 125, 125}, {0, 0, 1999, 0}, {1000, 0, 1000, 990}, {0, 0, 1000, 1000}};
    for (const auto &q : queries) {
        CHECK(sorted(index.search(q)) == bruteSearch(boxes, q));
    }
    const Point<std::int32_t> p{1234, 567};
    const Point<std::int32_t> outside{-7, 503};
    CHECK(neighborsMatch(boxes, outside, index.neighbors(outside, 1), 1));
    CHECK(neighborsMatch(boxes, p, index.neighbors(p, 25), 25));

    const auto bytes = index.data();
    const auto copy = flatbush::FlatbushBuilder<std::int32_t>::from(bytes.data(), bytes.size());
    CHECK(sorted(copy.search(queries[0])) == bruteSearch(boxes, queries[0]));
}

}  // namespace

int main() {
    testDoubleIndex();
    testBuilder();
    testNearestInOneNode();
    testWideIndex();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("flatbush: ok\n");
    return 0;
}
