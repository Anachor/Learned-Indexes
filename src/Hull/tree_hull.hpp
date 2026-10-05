#pragma once

// T2: keeps a treap over the segment's points whose every node knows its
// subtree's upper and lower hull. The leaves hold the points in key order, up
// to LeafSize consecutive points each, with the leaf's own hulls; the inner
// nodes are the gaps between neighbouring leaves. A node stores only how many
// vertices of each hull come from its left child's front and its right child's
// back (the bridge, chains.hpp); a vertex is found by walking down to its leaf.
// A split or a join recomputes the O(log m) nodes on its path and at most a few
// leaves' hulls, O(LeafSize) each. A join is decided on the two roots' combined
// hull before anything changes.
//
// Any two neighbouring leaves hold more than LeafSize points between them - a
// join or a split merges two that do not - so m points take fewer than
// 2m / LeafSize + 1 leaves. LeafSize = 1 is a point per leaf.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "chains.hpp"
#include "hull.hpp"
#include "vector_hull.hpp"

namespace lpma {

template <size_t LeafSize>
class BasicTreeHull {
    static_assert(LeafSize >= 1 && LeafSize <= 65535, "BasicTreeHull: leaves of 1 to 65535 points");

    // A leaf's points, y increasing, and per hull (0 upper, 1 lower) its vertices' indices.
    struct Block {
        size_t size = 0;
        Pt points[LeafSize];
        uint16_t vertices[2][LeafSize];
    };

    struct Node {
        Node *left = nullptr, *right = nullptr;  // none at a leaf
        Block *block = nullptr;                  // a leaf's points; none at a gap
        uint64_t priority = 0;                   // 0 at a leaf, else above its children's
        int64_t hi = 0;                          // the greatest y below
        // Per hull (0 upper, 1 lower reflected): the vertices, and how many come
        // from the left child's front and from the right child's back.
        int64_t count[2] = {0, 0}, from_left[2] = {0, 0}, from_right[2] = {0, 0};
        bool leaf() const { return !left; }
    };

public:
    static constexpr size_t LEAF_SIZE = LeafSize;

    BasicTreeHull() = default;
    BasicTreeHull(const BasicTreeHull &other) : root_(clone(other.root_)) {}
    BasicTreeHull(BasicTreeHull &&other) noexcept : root_(std::exchange(other.root_, nullptr)) {}
    BasicTreeHull &operator=(BasicTreeHull other) noexcept {
        std::swap(root_, other.root_);
        return *this;
    }
    ~BasicTreeHull() { destroy(root_); }

    bool operator==(const BasicTreeHull &other) const { return points() == other.points(); }

    static BasicTreeHull build(const PointView &pts, SlotRange s) {
        std::vector<Pt> points;
        pts.all_of(s, [&](Pt p) {
            points.push_back(p);
            return true;
        });
        return from_points(points);
    }

    // The tree of these points, in key order with y increasing.
    static BasicTreeHull from_points(const std::vector<Pt> &points) {
        BasicTreeHull h;
        h.root_ = build_nodes(points);
        return h;
    }

    static std::optional<std::pair<BasicTreeHull, ExactLine>> try_join(BasicTreeHull &l, SlotRange, BasicTreeHull &r,
                                                                       SlotRange, const PointView &, int64_t k) {
        if (!l.root_ || !r.root_) throw std::invalid_argument("TreeHull::try_join: no points");
        auto line = lowest_line(joined(l.root_, r.root_, 0), joined(l.root_, r.root_, 1), k);
        if (!line) return std::nullopt;
        BasicTreeHull h;
        h.root_ = fuse(std::exchange(l.root_, nullptr), std::exchange(r.root_, nullptr));
        return std::pair{std::move(h), *line};
    }

    static std::pair<BasicTreeHull, BasicTreeHull> split(BasicTreeHull &&h, SlotRange left, SlotRange right,
                                                         const PointView &) {
        auto [l, rest] = split_nodes(std::exchange(h.root_, nullptr), 2 * int64_t(left.end));
        auto [middle, r] = split_nodes(rest, 2 * int64_t(right.begin));
        destroy(middle);
        BasicTreeHull a, b;
        a.root_ = tidy_back(l);
        b.root_ = tidy_front(r);
        return {std::move(a), std::move(b)};
    }

    // The points, in key order.
    std::vector<Pt> points() const {
        std::vector<Pt> out;
        collect(root_, out);
        return out;
    }

    // The upper (o = 0) or lower (1) hull.
    std::vector<Pt> chain(int o) const {
        std::vector<Pt> out;
        for (int64_t i = 0; root_ && i < root_->count[o]; ++i) out.push_back(reflect(vertex(root_, o, i), o));
        return out;
    }

    // The memory of its nodes and leaves (not of the free ones the pools keep for reuse).
    size_t bytes() const { return bytes(root_); }

    // Every node is right: heap order, points in order, both hulls equal to the
    // monotone chains of the points below; and any two neighbouring leaves hold
    // more than LeafSize points. For tests.
    bool valid() const {
        std::vector<Pt> below;
        if (root_ && !check(root_, below)) return false;
        std::vector<size_t> sizes;
        leaf_sizes(root_, sizes);
        for (size_t i = 0; i + 1 < sizes.size(); ++i) {
            if (sizes[i] + sizes[i + 1] <= LeafSize) return false;
        }
        return true;
    }

private:
    Node *root_ = nullptr;

    static Pt reflect(Pt p, int o) { return o ? Pt{p.x, -p.y} : p; }

    // Vertex i of hull o of v's subtree.
    static Pt vertex(const Node *v, int o, int64_t i) {
        while (!v->leaf()) {
            if (i < v->from_left[o]) {
                v = v->left;
            } else {
                i += v->right->count[o] - v->from_right[o] - v->from_left[o];
                v = v->right;
            }
        }
        const Block &b = *v->block;
        return reflect(b.points[b.vertices[o][i]], o);
    }

    struct Chain {  // hull o of v's subtree
        const Node *v;
        int o;
        int64_t size() const { return v->count[o]; }
        Pt at(int64_t i) const { return vertex(v, o, i); }
    };

    struct Joined {  // the hull of two neighbouring subtrees: l's front, then r's back
        Chain l, r;
        int64_t from_l, from_r;
        int64_t size() const { return from_l + from_r; }
        Pt at(int64_t i) const { return i < from_l ? l.at(i) : r.at(i - from_l + r.size() - from_r); }
    };

    static Joined joined(const Node *l, const Node *r, int o) {
        Chain a{l, o}, b{r, o};
        auto [i, j] = bridge(a, b);
        return {a, b, i + 1, b.size() - j};
    }

    static void update(Node *v) {
        v->hi = v->right->hi;
        for (int o = 0; o < 2; ++o) {
            Joined h = joined(v->left, v->right, o);
            v->from_left[o] = h.from_l;
            v->from_right[o] = h.from_r;
            v->count[o] = h.size();
        }
    }

    // A leaf's hulls, by the monotone chain over its points, as VectorHull's.
    static void refresh_leaf(Node *v) {
        const Block &b = *v->block;
        for (int o = 0; o < 2; ++o) {
            uint16_t *h = v->block->vertices[o];
            int64_t n = 0;
            for (size_t i = 0; i < b.size; ++i) {
                Pt p = reflect(b.points[i], o);
                while (n >= 2 && cross(reflect(b.points[h[n - 2]], o), reflect(b.points[h[n - 1]], o), p) >= 0) --n;
                h[n++] = uint16_t(i);
            }
            v->count[o] = n;
        }
        v->hi = b.points[b.size - 1].y;
    }

    // Nodes and blocks are reused after release, instead of freed.
    template <class T>
    static std::vector<T *> &pool() {
        struct Pool {
            std::vector<T *> free;
            ~Pool() {
                for (T *p : free) delete p;
            }
        };
        thread_local Pool p;
        return p.free;
    }
    template <class T>
    static T *take() {
        std::vector<T *> &free = pool<T>();
        if (free.empty()) return new T;
        T *p = free.back();
        free.pop_back();
        return p;
    }
    static Node *make() {
        Node *v = take<Node>();
        *v = Node();
        return v;
    }
    static void release(Node *v) {
        if (v->block) pool<Block>().push_back(v->block);
        pool<Node>().push_back(v);
    }

    static uint64_t random_priority() {  // splitmix64, never 0
        thread_local uint64_t state = 0x2545F4914F6CDD1Dull;
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (z ^ (z >> 31)) | 1;
    }

    // A leaf of points[0, n), 1 <= n <= LeafSize.
    static Node *leaf(const Pt *points, size_t n) {
        Node *v = make();
        v->block = take<Block>();
        v->block->size = n;
        std::copy(points, points + n, v->block->points);
        refresh_leaf(v);
        return v;
    }

    static Node *gap() {
        Node *v = make();
        v->priority = random_priority();
        return v;
    }

    static void update_all(Node *v) {
        if (v->leaf()) return;
        update_all(v->left);
        update_all(v->right);
        update(v);
    }

    // Leaves of LeafSize points, the last maybe fewer, under a Cartesian tree of
    // the gaps between them, by priority.
    static Node *build_nodes(const std::vector<Pt> &points) {
        std::vector<Node *> leaves;
        for (size_t i = 0; i < points.size(); i += LeafSize)
            leaves.push_back(leaf(points.data() + i, std::min(LeafSize, points.size() - i)));
        if (leaves.empty()) return nullptr;
        if (leaves.size() == 1) return leaves[0];
        std::vector<Node *> gaps(leaves.size()), spine;  // gaps[g] is between leaves g - 1 and g
        for (size_t g = 1; g < leaves.size(); ++g) {
            Node *v = gaps[g] = gap(), *last = nullptr;
            while (!spine.empty() && spine.back()->priority < v->priority) {
                last = spine.back();
                spine.pop_back();
            }
            v->left = last;
            if (!spine.empty()) spine.back()->right = v;
            spine.push_back(v);
        }
        for (size_t g = 1; g < leaves.size(); ++g) {
            if (!gaps[g]->left) gaps[g]->left = leaves[g - 1];
            if (!gaps[g]->right) gaps[g]->right = leaves[g];
        }
        update_all(spine.front());
        return spine.front();
    }

    // a's points, then b's: two trees and a gap between them.
    static Node *join(Node *a, Node *g, Node *b) {
        Node *top = g;
        if (g->priority > a->priority && g->priority > b->priority) {
            g->left = a;
            g->right = b;
        } else if (a->priority > b->priority) {
            a->right = join(a->right, g, b);
            top = a;
        } else {
            b->left = join(a, g, b->left);
            top = b;
        }
        update(top);
        return top;
    }

    // a's points, then b's; a's last leaf and b's first become one if they fit in one.
    static Node *fuse(Node *a, Node *b) {
        if (!a) return b;
        if (!b) return a;
        if (last_leaf(a)->block->size + first_leaf(b)->block->size > LeafSize) return join(a, gap(), b);
        Node *x, *y;
        a = remove_last(a, x);
        b = remove_first(b, y);
        std::copy(y->block->points, y->block->points + y->block->size, x->block->points + x->block->size);
        x->block->size += y->block->size;
        refresh_leaf(x);
        release(y);
        if (a) x = join(a, gap(), x);
        if (b) x = join(x, gap(), b);
        return x;
    }

    static Node *first_leaf(Node *v) {
        while (!v->leaf()) v = v->left;
        return v;
    }
    static Node *last_leaf(Node *v) {
        while (!v->leaf()) v = v->right;
        return v;
    }

    // v without its first (last) leaf, which goes to out; nullptr if v was that leaf.
    static Node *remove_first(Node *v, Node *&out) {
        if (v->leaf()) {
            out = v;
            return nullptr;
        }
        if (v->left->leaf()) {
            out = v->left;
            Node *r = v->right;
            release(v);
            return r;
        }
        v->left = remove_first(v->left, out);
        update(v);
        return v;
    }
    static Node *remove_last(Node *v, Node *&out) {
        if (v->leaf()) {
            out = v;
            return nullptr;
        }
        if (v->right->leaf()) {
            out = v->right;
            Node *l = v->left;
            release(v);
            return l;
        }
        v->right = remove_last(v->right, out);
        update(v);
        return v;
    }

    // After a split, which can shrink the leaf at a new end: t's last (first)
    // two leaves become one if they fit in one.
    static Node *tidy_back(Node *t) {
        if (!t || t->leaf()) return t;
        Node *g = t;  // the gap before the last leaf
        while (!g->right->leaf()) g = g->right;
        if (last_leaf(g->left)->block->size + g->right->block->size > LeafSize) return t;
        Node *y;
        t = remove_last(t, y);
        return fuse(t, y);
    }
    static Node *tidy_front(Node *t) {
        if (!t || t->leaf()) return t;
        Node *g = t;  // the gap after the first leaf
        while (!g->left->leaf()) g = g->left;
        if (g->left->block->size + first_leaf(g->right)->block->size > LeafSize) return t;
        Node *x;
        t = remove_first(t, x);
        return fuse(x, t);
    }

    // v's points with y below y0, and the rest.
    static std::pair<Node *, Node *> split_nodes(Node *v, int64_t y0) {
        if (!v) return {nullptr, nullptr};
        if (v->leaf()) {
            Block &b = *v->block;
            size_t i = size_t(std::partition_point(b.points, b.points + b.size, [&](Pt p) { return p.y < y0; }) -
                              b.points);
            if (i == b.size) return {v, nullptr};
            if (i == 0) return {nullptr, v};
            Node *w = leaf(b.points + i, b.size - i);
            b.size = i;
            refresh_leaf(v);
            return {v, w};
        }
        if (y0 <= v->left->hi) {  // the cut is in the left subtree
            auto [a, b] = split_nodes(v->left, y0);
            if (!a) return {nullptr, v};
            v->left = b;
            update(v);
            return {a, v};
        }
        auto [a, b] = split_nodes(v->right, y0);  // the left subtree goes left
        if (!b) return {v, nullptr};
        if (!a) {  // the cut is v's own gap
            Node *l = v->left;
            release(v);
            return {l, b};
        }
        v->right = a;
        update(v);
        return {v, b};
    }

    static void destroy(Node *v) {
        if (!v) return;
        destroy(v->left);
        destroy(v->right);
        release(v);
    }

    static Node *clone(const Node *v) {
        if (!v) return nullptr;
        Node *c = make();
        *c = *v;
        if (v->block) {
            c->block = take<Block>();
            *c->block = *v->block;
        }
        c->left = clone(v->left);
        c->right = clone(v->right);
        return c;
    }

    static void collect(const Node *v, std::vector<Pt> &out) {
        if (!v) return;
        if (v->leaf()) out.insert(out.end(), v->block->points, v->block->points + v->block->size);
        collect(v->left, out);
        collect(v->right, out);
    }

    static void leaf_sizes(const Node *v, std::vector<size_t> &out) {
        if (!v) return;
        if (v->leaf()) out.push_back(v->block->size);
        leaf_sizes(v->left, out);
        leaf_sizes(v->right, out);
    }

    static size_t bytes(const Node *v) {
        if (!v) return 0;
        return sizeof(Node) + (v->block ? sizeof(Block) : 0) + bytes(v->left) + bytes(v->right);
    }

    static bool check(const Node *v, std::vector<Pt> &below) {
        size_t begin = below.size();
        if (v->leaf()) {
            const Block *b = v->block;
            if (v->right || !b || v->priority != 0 || b->size == 0 || b->size > LeafSize) return false;
            below.insert(below.end(), b->points, b->points + b->size);
            for (size_t i = begin + 1; i < below.size(); ++i) {
                if (below[i - 1].x >= below[i].x || below[i - 1].y >= below[i].y) return false;
            }
        } else {
            if (v->block || !v->right || v->left->priority >= v->priority || v->right->priority >= v->priority)
                return false;
            if (!check(v->left, below)) return false;
            size_t middle = below.size();
            if (!check(v->right, below)) return false;
            if (below[middle - 1].x >= below[middle].x || below[middle - 1].y >= below[middle].y) return false;
        }
        if (v->hi != below.back().y) return false;

        std::vector<Pt> hull[2];
        for (size_t i = begin; i < below.size(); ++i) {
            VectorHull::push_upper(hull[0], below[i]);
            VectorHull::push_lower(hull[1], below[i]);
        }
        for (int o = 0; o < 2; ++o) {
            if (v->count[o] != int64_t(hull[o].size())) return false;
            for (int64_t i = 0; i < v->count[o]; ++i) {
                if (reflect(vertex(v, o, i), o) != hull[o][i]) return false;
            }
        }
        return true;
    }
};

// The leaf size the index uses.
using TreeHull = BasicTreeHull<32>;

static_assert(Hull<TreeHull>);

}  // namespace lpma
