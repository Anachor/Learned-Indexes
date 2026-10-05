#pragma once

// T2: keeps a treap over the segment's points - the points are its leaves, in
// key order - whose every node knows its subtree's upper and lower hull. A node
// stores only how many vertices of each hull come from its left child's front
// and its right child's back (the bridge, chains.hpp); a vertex is found by
// walking down. A split or a join recomputes the O(log m) nodes on its path. A
// join is decided on the two roots' combined hull before anything changes.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "chains.hpp"
#include "hull.hpp"
#include "vector_hull.hpp"

namespace lpma {

class TreeHull {
    struct Node {
        Node *left = nullptr, *right = nullptr;  // none at a leaf
        uint64_t priority = 0;                    // 0 at a leaf, else above its children's
        Pt point{0, 0};                           // a leaf's point
        int64_t hi = 0;                           // the greatest y below
        // Per hull (0 upper, 1 lower reflected): the vertices, and how many come
        // from the left child's front and from the right child's back.
        int64_t count[2] = {1, 1}, from_left[2] = {0, 0}, from_right[2] = {0, 0};
        bool leaf() const { return !left; }
    };

public:
    TreeHull() = default;
    TreeHull(const TreeHull &other) : root_(clone(other.root_)) {}
    TreeHull(TreeHull &&other) noexcept : root_(std::exchange(other.root_, nullptr)) {}
    TreeHull &operator=(TreeHull other) noexcept {
        std::swap(root_, other.root_);
        return *this;
    }
    ~TreeHull() { destroy(root_); }

    bool operator==(const TreeHull &other) const { return points() == other.points(); }

    static TreeHull build(const PointView &pts, SlotRange s) {
        std::vector<Pt> points;
        pts.all_of(s, [&](Pt p) {
            points.push_back(p);
            return true;
        });
        return from_points(points);
    }

    // The tree of these points, in key order with y increasing.
    static TreeHull from_points(const std::vector<Pt> &points) {
        TreeHull h;
        h.root_ = build_nodes(points);
        return h;
    }

    static std::optional<std::pair<TreeHull, ExactLine>> try_join(TreeHull &l, SlotRange, TreeHull &r, SlotRange,
                                                                  const PointView &, int64_t k) {
        if (!l.root_ || !r.root_) throw std::invalid_argument("TreeHull::try_join: no points");
        auto line = lowest_line(joined(l.root_, r.root_, 0), joined(l.root_, r.root_, 1), k);
        if (!line) return std::nullopt;
        TreeHull h;
        h.root_ = merge(std::exchange(l.root_, nullptr), std::exchange(r.root_, nullptr));
        return std::pair{std::move(h), *line};
    }

    static std::pair<TreeHull, TreeHull> split(TreeHull &&h, SlotRange left, SlotRange right, const PointView &) {
        auto [l, rest] = split_nodes(std::exchange(h.root_, nullptr), 2 * int64_t(left.end));
        auto [middle, r] = split_nodes(rest, 2 * int64_t(right.begin));
        destroy(middle);
        TreeHull a, b;
        a.root_ = l;
        b.root_ = r;
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

    // Every node is right: heap order, points in order, and both hulls equal to
    // the monotone chains of the points below. For tests.
    bool valid() const {
        std::vector<Pt> below;
        return !root_ || check(root_, below);
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
        return reflect(v->point, o);
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

    // Nodes are reused after release, instead of freed.
    struct Pool {
        std::vector<Node *> nodes;
        ~Pool() {
            for (Node *v : nodes) delete v;
        }
    };
    static std::vector<Node *> &pool() {
        thread_local Pool p;
        return p.nodes;
    }
    static Node *make() {
        std::vector<Node *> &free = pool();
        if (free.empty()) return new Node;
        Node *v = free.back();
        free.pop_back();
        *v = Node();
        return v;
    }
    static void release(Node *v) { pool().push_back(v); }

    static uint64_t random_priority() {  // splitmix64, never 0
        thread_local uint64_t state = 0x2545F4914F6CDD1Dull;
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (z ^ (z >> 31)) | 1;
    }

    static Node *leaf(Pt p) {
        Node *v = make();
        v->point = p;
        v->hi = p.y;
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

    // A Cartesian tree of the gaps between neighbouring points, by priority.
    static Node *build_nodes(const std::vector<Pt> &points) {
        if (points.empty()) return nullptr;
        if (points.size() == 1) return leaf(points[0]);
        std::vector<Node *> gaps(points.size()), spine;  // gaps[g] is between points g - 1 and g
        for (size_t g = 1; g < points.size(); ++g) {
            Node *v = gaps[g] = gap(), *last = nullptr;
            while (!spine.empty() && spine.back()->priority < v->priority) {
                last = spine.back();
                spine.pop_back();
            }
            v->left = last;
            if (!spine.empty()) spine.back()->right = v;
            spine.push_back(v);
        }
        for (size_t g = 1; g < points.size(); ++g) {
            if (!gaps[g]->left) gaps[g]->left = leaf(points[g - 1]);
            if (!gaps[g]->right) gaps[g]->right = leaf(points[g]);
        }
        update_all(spine.front());
        return spine.front();
    }

    // a's points, then b's.
    static Node *merge(Node *a, Node *b) {
        if (!a) return b;
        if (!b) return a;
        return join(a, gap(), b);
    }

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

    // v's points with y below y0, and the rest.
    static std::pair<Node *, Node *> split_nodes(Node *v, int64_t y0) {
        if (!v) return {nullptr, nullptr};
        if (v->leaf()) {
            if (v->point.y < y0) return {v, nullptr};
            return {nullptr, v};
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
        c->left = clone(v->left);
        c->right = clone(v->right);
        return c;
    }

    static void collect(const Node *v, std::vector<Pt> &out) {
        if (!v) return;
        if (v->leaf()) out.push_back(v->point);
        collect(v->left, out);
        collect(v->right, out);
    }

    static bool check(const Node *v, std::vector<Pt> &below) {
        size_t begin = below.size();
        if (v->leaf()) {
            below.push_back(v->point);
            return !v->right && v->priority == 0 && v->hi == v->point.y && v->count[0] == 1 && v->count[1] == 1;
        }
        if (!v->right || v->left->priority >= v->priority || v->right->priority >= v->priority) return false;
        if (!check(v->left, below)) return false;
        size_t middle = below.size();
        if (!check(v->right, below)) return false;
        if (below[middle - 1].x >= below[middle].x || below[middle - 1].y >= below[middle].y) return false;
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

static_assert(Hull<TreeHull>);

}  // namespace lpma
