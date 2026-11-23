#pragma once
// The shape of a session's screen: which panes there are, where the dividers sit, how
// many tabs the session has, and how big the whole thing gets when more than one view is
// looking at it.
//
// This is the record behind "replace tmux" (ROADMAP, M4). What it encodes is the decision
// that the arrangement belongs to the *session* and not to the window showing it, so a tty
// attach and a second window see the same splits, and closing a window takes away a view
// rather than the work. (Kaida, 2026-11-21: "IT should be up to the user to be able to
// arrange their session as they want, like 1 pane using the half left of the screen and 2
// pane horizontally spliting the right side if they want, or just 2 horizontal or 2
// vertical ones, or 4x4 square. also resizing should be possible".) A record that could
// only describe a grid cannot express the first thing on that list, which is why a tree of
// splits is the record and a grid is something you can build out of it.
//
// Qt-free on purpose, like the parser, the screen and the config: this is arithmetic and
// decisions, and being testable without a display is how it stays correct while the thing
// that draws it is still moving.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kai {

// Which way a split divides the area it was given. The words describe the two panes rather
// than the line between them, because "split vertical" has meant both since the first
// terminal that said it, and a type gets to be unambiguous where a sentence cannot.
enum class Split : std::uint8_t { LeftRight, UpDown };

inline const char* textFor(Split split) {
    return split == Split::LeftRight ? "left-right" : "up-down";
}

inline bool splitFromText(std::string_view text, Split& out) {
    if (text == "left-right") {
        out = Split::LeftRight;
        return true;
    }
    if (text == "up-down") {
        out = Split::UpDown;
        return true;
    }
    return false;
}

// Leading and trailing blanks, for the words a layout is written in.
inline std::string trimLayout(std::string_view text) {
    size_t a = 0;
    size_t b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r'))
        --b;
    return std::string(text.substr(a, b - a));
}

// A rectangle of cells: what a pane asks of the window that owns it, and what that window
// then hands to `setPanes()` so the widget knows where one pane ends and the next begins.
struct CellRect {
    int top = 0;
    int left = 0;
    int rows = 0;
    int cols = 0;

    bool empty() const { return rows <= 0 || cols <= 0; }
    int cells() const { return rows * cols; }
    bool contains(int row, int col) const {
        return row >= top && row < top + rows && col >= left && col < left + cols;
    }
    bool operator==(const CellRect& other) const {
        return top == other.top && left == other.left && rows == other.rows &&
               cols == other.cols;
    }
};

// How big a view is, as it said in its hello.
struct ViewSize {
    int rows = 0;
    int cols = 0;

    bool operator==(const ViewSize& other) const {
        return rows == other.rows && cols == other.cols;
    }
};

// The tree. A node is either a pane — a leaf with a number — or a split with two children
// and the ratio between them. Indices rather than pointers, so the whole thing is a value:
// copyable, comparable, and one frame away from the wire.
class Layout {
public:
    static constexpr int kNil = -1;
    // A pane smaller than this is a pane you cannot read, and a divider that takes a whole
    // pane away is not a divider.
    static constexpr int kMinPaneCells = 1;

    // A window that has never been split: one pane, focused, filling whatever it is given.
    Layout() {
        Node first;
        first.pane = 0; // a leaf left at the sentinel is a pane no number can reach
        nodes_.push_back(first);
        root_ = 0;
        focused_ = 0;
        next_pane_id_ = 1;
    }

    // ---- what there is -------------------------------------------------------
    int paneCount() const { return static_cast<int>(panes().size()); }
    bool hasPane(int pane) const { return nodeIndexOfPane(pane) != kNil; }
    int focused() const { return focused_; }
    int splitCount() const { return static_cast<int>(nodes_.size()) - paneCount(); }

    // Panes in reading order — top to bottom, left to right — which is also the order the
    // keys walk them in, so `Alt+Right` here means the pane to the right rather than the
    // pane with the next number.
    std::vector<int> panes() const {
        std::vector<int> out;
        collect(root_, out);
        return out;
    }

    void focus(int pane) {
        if (hasPane(pane))
            focused_ = pane;
    }

    int focusStep(int delta) {
        const std::vector<int> list = panes();
        if (list.empty())
            return kNil;
        size_t at = 0;
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i] == focused_)
                at = i;
        }
        const long long count = static_cast<long long>(list.size());
        long long next = (static_cast<long long>(at) + delta) % count;
        if (next < 0)
            next += count;
        focused_ = list[static_cast<size_t>(next)];
        return focused_;
    }

    // ---- the shape -----------------------------------------------------------
    //
    // Split one pane in two and hand back the new pane's number, or -1 when there is no
    // room in that direction at the size given. Room is asked about rather than assumed: a
    // ten-column window is a real thing, and a split that leaves a zero-cell pane is a
    // window with a pane nobody can see, reach, or close by pointing at.
    int splitPane(int pane, Split how, int rows, int cols, double ratio = 0.5) {
        const int at = nodeIndexOfPane(pane);
        if (at == kNil)
            return kNil;
        const CellRect here = rectOf(pane, rows, cols);
        const int extent = how == Split::LeftRight ? here.cols : here.rows;
        if (extent < 2 * kMinPaneCells + 1)
            return kNil;

        // Where the pane sits is asked before anything is added, because the branch about
        // to be made will itself claim to be the pane's parent — and a root that believes
        // it has a parent above it is a root that never moves.
        const int above = parentOf(at);

        Node fresh;
        fresh.pane = next_pane_id_++;
        nodes_.push_back(fresh);
        const int second = static_cast<int>(nodes_.size()) - 1;

        Node branch;
        branch.leaf = false;
        branch.split = how;
        branch.ratio = clampRatio(ratio);
        branch.first = at;
        branch.second = second;
        nodes_.push_back(branch);
        const int branch_at = static_cast<int>(nodes_.size()) - 1;
        // The branch takes the split pane's place — under its parent, or as the root when
        // it had none. Forgetting the root case leaves a tree whose top is still the old
        // leaf and whose new branch is nothing but unreachable nodes.
        if (above == kNil)
            root_ = branch_at;
        else
            reparentChild(above, at, branch_at);
        focused_ = fresh.pane; // the pane somebody just asked for is the one they want
        return fresh.pane;
    }

    // Close a pane and give its area back to whatever shared the split with it. The last
    // pane cannot be closed: a window with no pane is not a window holding nothing, it is a
    // window that cannot show anything at all.
    bool closePane(int pane) {
        const int at = nodeIndexOfPane(pane);
        if (at == kNil || paneCount() <= 1)
            return false;
        const int parent = parentOf(at);
        if (parent == kNil)
            return false; // the root leaf, which is the last pane, refused just above
        const int sibling = nodes_[static_cast<size_t>(parent)].first == at
                                ? nodes_[static_cast<size_t>(parent)].second
                                : nodes_[static_cast<size_t>(parent)].first;
        if (root_ == parent)
            root_ = sibling;
        else
            reparentChild(parentOf(parent), parent, sibling);
        if (focused_ == pane) {
            const std::vector<int> left = panes();
            focused_ = left.empty() ? kNil : left.front();
        }
        return true;
    }

    // Move the divider of a pane's own split by whole cells. Positive is right or down,
    // whichever way that divider runs, and it stops at the edges rather than wrapping: a
    // divider that teleported across the window when a pane ran out of room would be the
    // one gesture in this file nobody could predict.
    bool resizeDivider(int pane, int delta, int rows, int cols) {
        const int at = nodeIndexOfPane(pane);
        if (at == kNil)
            return false;
        const int parent = parentOf(at);
        if (parent == kNil)
            return false; // one pane, no divider to move
        Node& branch = nodes_[static_cast<size_t>(parent)];
        const CellRect area = rectOfNode(parent, rows, cols);
        const int extent = branch.split == Split::LeftRight ? area.cols : area.rows;
        int first = 0;
        int second = 0;
        divide(extent, branch.ratio, first, second);
        const int shared = extent - dividerCost(extent);
        const int widest = std::max(kMinPaneCells, shared - kMinPaneCells);
        const int moved = std::clamp(first + delta, kMinPaneCells, widest);
        if (moved == first || shared <= 0)
            return false;
        branch.ratio = static_cast<double>(moved) / static_cast<double>(shared);
        return true;
    }

    // Every divider back to the middle: what "unstick this layout" means, and also what a
    // grid is built out of.
    void equalise() { equaliseNode(root_); }

    // One rect per pane, in `panes()` order. A divider costs a cell, which the window draws
    // and which `dividerCells` counts, so that a check can add the whole screen up and find
    // no hole in it.
    std::vector<CellRect> rects(int rows, int cols) const {
        std::vector<CellRect> by_node(nodes_.size(), CellRect{});
        if (rows > 0 && cols > 0)
            rectsNode(root_, CellRect{0, 0, rows, cols}, by_node);
        std::vector<CellRect> ordered;
        collectRects(root_, by_node, ordered);
        return ordered;
    }

    CellRect rectOf(int pane, int rows, int cols) const {
        const std::vector<int> list = panes();
        const std::vector<CellRect> all = rects(rows, cols);
        for (size_t i = 0; i < list.size() && i < all.size(); ++i) {
            if (list[i] == pane)
                return all[i];
        }
        return CellRect{};
    }

    // Where the dividers are, as cells of the screen. The window has to draw them and the
    // pointer has to know when it is on one, and a check that adds the screen up needs them
    // too: a divider is the one cell that belongs to no pane, which is not the same as
    // belonging to nothing. Computed in the same walk that places the panes, so a divider
    // cannot disagree with the panes either side of it.
    std::vector<CellRect> dividerRects(int rows, int cols) const {
        std::vector<CellRect> out;
        std::vector<CellRect> by_node(nodes_.size(), CellRect{});
        if (rows > 0 && cols > 0)
            rectsNode(root_, CellRect{0, 0, rows, cols}, by_node, &out);
        return out;
    }

    // How many cells the dividers take at this size — the difference between a tiled screen
    // and one with lines missing from it.
    int dividerCells(int rows, int cols) const {
        if (rows <= 0 || cols <= 0)
            return 0;
        return dividersIn(root_, rows, cols);
    }

    // A grid: as even as it can be, which is the shape of "4x4 square" and a fair starting
    // point for anybody who means to drag from there.
    static Layout grid(int rows, int cols) {
        Layout out;
        out.nodes_.clear();
        out.root_ = kNil;
        out.next_pane_id_ = 0;
        out.root_ = out.buildGrid(std::max(1, rows), std::max(1, cols));
        out.focused_ = out.panes().front();
        return out;
    }

    // ---- the words a preset is made of ---------------------------------------
    //
    // Indentation is the tree, so the file reads like the screen it describes:
    //
    //   window main
    //     split left-right 0.500
    //       pane
    //       split up-down 0.600
    //         pane
    //         pane
    //
    // Written by hand, or saved from a running session, and read back the same either way.
    std::string toText() const {
        std::ostringstream text;
        writeNode(root_, 1, text);
        return text.str();
    }

    // Reading those words. A line nobody understands is named with its number and the word
    // that was wrong, because a half-read layout is worse than none: it is a window with
    // panes in the wrong places and no reason given. The same promise the config file makes.
    static Layout fromText(std::string_view text, std::vector<std::string>& problems) {
        Layout out;
        out.nodes_.clear();
        out.root_ = kNil;
        out.next_pane_id_ = 0;

        std::vector<Line> lines;
        {
            const std::string all(text);
            size_t start = 0;
            size_t line_no = 0;
            while (start <= all.size()) {
                const size_t end = all.find('\n', start);
                const std::string line = all.substr(
                    start, end == std::string::npos ? std::string::npos : end - start);
                start = end == std::string::npos ? all.size() + 1 : end + 1;
                ++line_no;
                const std::string body = trimLayout(line);
                if (body.empty() || body.front() == '#')
                    continue;
                if (body.rfind("window", 0) == 0)
                    continue; // SessionLayout reads those; a bare Layout skips one.
                Line parsed;
                parsed.depth = indentOf(line) / 2;
                if (body == "pane") {
                    lines.push_back(parsed);
                    continue;
                }
                if (body.rfind("split ", 0) == 0) {
                    parsed.leaf = false;
                    std::string rest = trimLayout(body.substr(6));
                    const size_t space = rest.find(' ');
                    const std::string word = space == std::string::npos
                                                 ? rest
                                                 : trimLayout(rest.substr(0, space));
                    const std::string tail = space == std::string::npos
                                                 ? std::string()
                                                 : trimLayout(rest.substr(space + 1));
                    if (!splitFromText(word, parsed.split)) {
                        problems.push_back("line " + std::to_string(line_no) +
                                           ": split wants left-right or up-down, got '" + word +
                                           "'");
                        continue;
                    }
                    if (!tail.empty()) {
                        double got = -1.0;
                        try {
                            got = std::stod(tail);
                        } catch (...) {
                            got = -1.0;
                        }
                        if (!(got > 0.0) || !(got < 1.0)) {
                            problems.push_back("line " + std::to_string(line_no) +
                                               ": a split's ratio wants a number between 0 and"
                                               " 1, got '" +
                                               tail + "'");
                            continue;
                        }
                        parsed.ratio = clampRatio(got);
                    }
                    lines.push_back(parsed);
                    continue;
                }
                problems.push_back("line " + std::to_string(line_no) + ": I do not know '" +
                                   body + "' — a layout line is `pane` or `split left-right 0.5`");
            }
        }

        size_t at = 0;
        out.root_ = out.buildFrom(lines, at, lines.empty() ? 0 : lines.front().depth);
        if (out.root_ == kNil) {
            Node leaf;
            leaf.pane = out.next_pane_id_++;
            out.nodes_.push_back(leaf);
            out.root_ = static_cast<int>(out.nodes_.size()) - 1;
        }
        out.focused_ = out.panes().empty() ? kNil : out.panes().front();
        return out;
    }

private:
    // One layout line, already understood: how deep it is indented, and whether it is a
    // pane or a split. Reading the words and building the tree are separate passes so that
    // a bad line can be named and skipped without half-building anything.
    struct Line {
        int depth = 0;
        bool leaf = true;
        Split split = Split::LeftRight;
        double ratio = 0.5;
    };

    struct Node {
        bool leaf = true;
        int pane = -1;
        Split split = Split::LeftRight;
        double ratio = 0.5;
        int first = kNil;
        int second = kNil;
    };

    // ---- the shared maths ----------------------------------------------------
    //
    // One cell of an extent belongs to the divider rather than to either side, and a pane
    // never goes to nothing: a window too small for its layout keeps every pane at one cell
    // and lets the last of them have whatever is left.
    static void divide(int extent, double ratio, int& first, int& second) {
        if (extent <= 0) {
            first = 0;
            second = 0;
            return;
        }
        const int shared = extent - dividerCost(extent);
        if (shared < 2) { // no room for two, so one of them has it all
            first = std::max(1, std::min(extent, shared > 0 ? shared : extent));
            second = 0;
            return;
        }
        // Draw what the record says. The bounds on a ratio belong where a ratio is
        // accepted — a split asked for, a preset read in — and not here: a divider dragged
        // near the edge stores the ratio that puts it there, and clamping it again on the
        // way to the screen would leave the record describing one divider and the window
        // drawing another, which is a lie that survives a save and reload. A ratio that is
        // not a fraction at all is nobody's intention, so it lands in the middle.
        const double honest = (ratio > 0.0 && ratio < 1.0) ? ratio : 0.5;
        // Floored, so an even split of an odd extent gives the smaller half to the pane on
        // the left or the top: 80 columns are 39, a line, and 40, which is how every other
        // terminal counts them.
        const int got = std::clamp(static_cast<int>(std::floor(shared * honest)), kMinPaneCells,
                                   shared - kMinPaneCells);
        first = got;
        second = shared - got;
    }

    // A divider only exists where there is something on both sides of it.
    static int dividerCost(int extent) { return extent >= 2 * kMinPaneCells + 1 ? 1 : 0; }

    // A ratio is the share of an area that lies on one side of a divider, so the only
    // bounds it can have are the ones that make it a share at all. How thin a pane may get
    // is a question about cells, answered where the cells are counted (`kMinPaneCells`) and
    // not here: a divider dragged near the edge has to be able to say where it is, or the
    // record describes one divider and the window draws another.
    static double clampRatio(double ratio) {
        if (!(ratio > 0.0 && ratio < 1.0))
            return 0.5;
        return ratio;
    }

    // ---- walking the tree ----------------------------------------------------
    // Only the panes still on the tree. A closed pane's node stays in the array — a
    // number is never handed out twice, so its number is gone for good — and answering
    // from the array would keep saying that a pane which is not on the screen anywhere
    // still exists.
    int nodeIndexOfPane(int pane) const { return nodeIndexOfPaneFrom(root_, pane); }

    int nodeIndexOfPaneFrom(int at, int pane) const {
        if (at == kNil)
            return kNil;
        const Node& n = nodes_[static_cast<size_t>(at)];
        if (n.leaf)
            return n.pane == pane ? at : kNil;
        const int first = nodeIndexOfPaneFrom(n.first, pane);
        return first != kNil ? first : nodeIndexOfPaneFrom(n.second, pane);
    }

    void collect(int at, std::vector<int>& out) const {
        if (at == kNil)
            return;
        const Node& n = nodes_[static_cast<size_t>(at)];
        if (n.leaf) {
            out.push_back(n.pane);
            return;
        }
        collect(n.first, out);
        collect(n.second, out);
    }

    void collectRects(int at, const std::vector<CellRect>& by_node,
                      std::vector<CellRect>& out) const {
        if (at == kNil)
            return;
        const Node& n = nodes_[static_cast<size_t>(at)];
        if (n.leaf) {
            out.push_back(by_node[static_cast<size_t>(at)]);
            return;
        }
        collectRects(n.first, by_node, out);
        collectRects(n.second, by_node, out);
    }

    int parentOf(int at) const { return parentOfFrom(root_, kNil, at); }

    // Only branches still on the tree have a child. A closed pane's parent stays in the
    // array for good — a pane number is never handed out twice — and a detached branch that
    // still points at a live pane would answer as its parent a second time: the split made
    // for that pane would hang off the dead branch and never be reachable again.
    int parentOfFrom(int at, int parent, int target) const {
        if (at == kNil)
            return kNil;
        if (at == target)
            return parent;
        const Node& n = nodes_[static_cast<size_t>(at)];
        if (n.leaf)
            return kNil;
        const int in_first = parentOfFrom(n.first, at, target);
        return in_first != kNil ? in_first : parentOfFrom(n.second, at, target);
    }

    // Hand `from`'s place under `parent` to `to`. The parent is named rather than hunted
    // for: the branch that was just made to hold a split pane has that pane as a child too,
    // and rewriting that pointer is a tree with a loop where a pane used to be.
    void reparentChild(int parent, int from, int to) {
        if (parent == kNil)
            return;
        Node& n = nodes_[static_cast<size_t>(parent)];
        if (n.first == from)
            n.first = to;
        else if (n.second == from)
            n.second = to;
    }

    void rectsNode(int at, const CellRect& area, std::vector<CellRect>& out,
                   std::vector<CellRect>* dividers = nullptr) const {
        const Node& n = nodes_[static_cast<size_t>(at)];
        // Every node keeps the area it was handed, not just the panes. A split is a place
        // too — it is the place the divider sits in — and asking a branch for its rect has
        // to answer with that area or the divider in it counts as nothing and a drag of it
        // finds no room to move in.
        out[static_cast<size_t>(at)] = area;
        if (n.leaf)
            return;
        CellRect a = area;
        CellRect b = area;
        if (n.split == Split::LeftRight) {
            int first = 0;
            int second = 0;
            divide(area.cols, n.ratio, first, second);
            a.cols = first;
            b.left = area.left + first + dividerCost(area.cols);
            b.cols = second;
            if (dividers != nullptr && dividerCost(area.cols) > 0 && first > 0 && second > 0)
                dividers->push_back(CellRect{area.top, area.left + first, area.rows, 1});
        } else {
            int first = 0;
            int second = 0;
            divide(area.rows, n.ratio, first, second);
            a.rows = first;
            b.top = area.top + first + dividerCost(area.rows);
            b.rows = second;
            if (dividers != nullptr && dividerCost(area.rows) > 0 && first > 0 && second > 0)
                dividers->push_back(CellRect{area.top + first, area.left, 1, area.cols});
        }
        if (!b.empty())
            rectsNode(n.second, b, out, dividers);
        else
            out[static_cast<size_t>(n.second)] = CellRect{}; // no room left: empty, not wrong
        rectsNode(n.first, a, out, dividers);
    }

    CellRect rectOfNode(int at, int rows, int cols) const {
        std::vector<CellRect> out(nodes_.size(), CellRect{});
        if (rows > 0 && cols > 0)
            rectsNode(at, CellRect{0, 0, rows, cols}, out);
        return out[static_cast<size_t>(at)];
    }

    int dividersIn(int at, int rows, int cols) const {
        const Node& n = nodes_[static_cast<size_t>(at)];
        if (n.leaf)
            return 0;
        const CellRect area = rectOfNode(at, rows, cols);
        const int extent = n.split == Split::LeftRight ? area.cols : area.rows;
        return dividerCost(extent) + dividersIn(n.first, rows, cols) +
               dividersIn(n.second, rows, cols);
    }

    void equaliseNode(int at) {
        Node& n = nodes_[static_cast<size_t>(at)];
        if (n.leaf)
            return;
        n.ratio = 0.5;
        equaliseNode(n.first);
        equaliseNode(n.second);
    }

    int newLeaf() {
        Node leaf;
        leaf.pane = next_pane_id_++;
        nodes_.push_back(leaf);
        return static_cast<int>(nodes_.size()) - 1;
    }

    int newBranch(Split how, double ratio, int first, int second) {
        Node branch;
        branch.leaf = false;
        branch.split = how;
        branch.ratio = clampRatio(ratio);
        branch.first = first;
        branch.second = second;
        nodes_.push_back(branch);
        return static_cast<int>(nodes_.size()) - 1;
    }

    int buildGrid(int rows, int cols) {
        if (rows == 1 && cols == 1)
            return newLeaf();
        if (cols > 1) {
            const int first = buildGrid(rows, 1);
            const int second = buildGrid(rows, cols - 1);
            return newBranch(Split::LeftRight, 1.0 / static_cast<double>(cols), first, second);
        }
        const int first = buildGrid(1, cols);
        const int second = buildGrid(rows - 1, cols);
        return newBranch(Split::UpDown, 1.0 / static_cast<double>(rows), first, second);
    }

    // Recursive descent over the indented lines: a `pane` is a leaf, a `split` takes the
    // two things that follow it at a deeper indentation. A split that only found one child
    // is not a split, so it becomes that child — a divider with nothing on one side is a
    // line drawn for no reason.
    int buildFrom(const std::vector<Line>& lines, size_t& at, int depth) {
        if (at >= lines.size() || lines[at].depth < depth)
            return kNil;
        const Line& line = lines[at];
        const int mine = line.depth;
        if (line.leaf) {
            ++at;
            return newLeaf();
        }
        ++at;
        const int first = buildFrom(lines, at, mine + 1);
        const int second = buildFrom(lines, at, mine + 1);
        if (first == kNil && second == kNil)
            return kNil;
        if (first == kNil)
            return second;
        if (second == kNil)
            return first;
        return newBranch(line.split, line.ratio, first, second);
    }

    void writeNode(int at, int depth, std::ostringstream& text) const {
        if (at == kNil)
            return;
        const Node& n = nodes_[static_cast<size_t>(at)];
        const std::string pad(static_cast<size_t>(depth) * 2, ' ');
        if (n.leaf) {
            text << pad << "pane\n";
            return;
        }
        std::ostringstream ratio;
        ratio.setf(std::ios::fixed);
        ratio.precision(6);
        // Rounded *up* at the sixth place rather than to nearest. The split floors, so a
        // ratio written a hair under the stored one divides to one cell fewer on the way
        // back in, and a divider that shifts a cell when a preset is reloaded is a preset
        // that does not save the arrangement.
        ratio << std::ceil(n.ratio * 1000000.0) / 1000000.0;
        text << pad << "split " << textFor(n.split) << " " << ratio.str() << "\n";
        writeNode(n.first, depth + 1, text);
        writeNode(n.second, depth + 1, text);
    }

    static int indentOf(const std::string& line) {
        int n = 0;
        while (n < static_cast<int>(line.size()) && line[static_cast<size_t>(n)] == ' ')
            ++n;
        return n;
    }

    std::vector<Node> nodes_;
    int root_ = kNil;
    int focused_ = kNil;
    int next_pane_id_ = 1;
};

// A tab: a name, a tree of panes, and which of them the keys are standing in.
struct WindowLayout {
    std::string name;
    Layout layout;
};

// A session: its tabs, and which one is showing. The level between a session and its panes
// is what makes `kai-term attach` mean something when the session it reaches has three
// tabs: it arrives at the tab that was showing, not at whichever was made first.
// (Kaida, 2026-11-21, on whether a tab is a session or a window of one: a window inside a
// session, the shape tmux users already have in their hands.)
struct SessionLayout {
    std::vector<WindowLayout> windows;
    int active = 0;

    SessionLayout() { windows.push_back(WindowLayout{}); }

    int count() const { return static_cast<int>(windows.size()); }

    WindowLayout& current() {
        if (windows.empty())
            windows.push_back(WindowLayout{});
        active = std::clamp(active, 0, static_cast<int>(windows.size()) - 1);
        return windows[static_cast<size_t>(active)];
    }

    const WindowLayout& current() const {
        static const WindowLayout none{};
        if (windows.empty())
            return none;
        return windows[static_cast<size_t>(
            std::clamp(active, 0, static_cast<int>(windows.size()) - 1))];
    }

    int addWindow(std::string name) {
        WindowLayout fresh;
        fresh.name = std::move(name);
        windows.push_back(std::move(fresh));
        active = static_cast<int>(windows.size()) - 1;
        return active;
    }

    // The last tab cannot be closed, for the reason the last pane cannot: what is left
    // would be a session with nothing to show.
    bool removeWindow(int at) {
        if (windows.size() <= 1 || at < 0 || at >= static_cast<int>(windows.size()))
            return false;
        windows.erase(windows.begin() + at);
        active = std::clamp(active - (at < active ? 1 : 0), 0,
                            static_cast<int>(windows.size()) - 1);
        return true;
    }

    std::string toText() const {
        std::ostringstream text;
        for (size_t i = 0; i < windows.size(); ++i) {
            const std::string name =
                windows[i].name.empty() ? "window-" + std::to_string(i + 1) : windows[i].name;
            text << "window " << name << "\n";
            text << windows[i].layout.toText();
            if (i + 1 < windows.size())
                text << "\n";
        }
        return text.str();
    }

    static SessionLayout fromText(std::string_view text, std::vector<std::string>& problems) {
        SessionLayout out;
        out.windows.clear();
        const std::string all(text);
        size_t start = 0;
        std::string chunk;
        std::string name;
        // Whether a `window` line has been read whose body has not been turned into a tab
        // yet. A tab and its panes are separate lines, so "has nothing under it" and "has
        // not got anything under it yet" look the same until the header is remembered.
        bool open = false;
        auto flush = [&]() {
            WindowLayout w;
            w.name = name;
            w.layout = Layout::fromText(chunk, problems);
            out.windows.push_back(std::move(w));
            chunk.clear();
            name.clear();
            open = false;
        };
        while (start <= all.size()) {
            const size_t end = all.find('\n', start);
            const std::string line = all.substr(
                start, end == std::string::npos ? std::string::npos : end - start);
            start = end == std::string::npos ? all.size() + 1 : end + 1;
            std::string body = line;
            size_t lead = 0;
            while (lead < body.size() && body[lead] == ' ')
                ++lead;
            body = body.substr(lead);
            if (body.empty() || body.front() == '#')
                continue;
            if (body == "window" || body.rfind("window ", 0) == 0) {
                if (!chunk.empty() || open)
                    flush();
                name = trimLayout(body.substr(6));
                open = true;
                continue;
            }
            chunk += line;
            chunk += '\n';
        }
        if (!chunk.empty() || open)
            flush();
        if (out.windows.empty())
            out.windows.push_back(WindowLayout{});
        return out;
    }
};

// Whose size a session takes when more than one view is looking at it: the smallest, so
// nobody is handed a screen bigger than the one they are standing in. This is the rule
// Kaida chose on 2026-11-21 over the one the daemon has done since the beginning — the
// last view to speak wins — because "the screen means one thing" turned out to mean
// "whatever arrived last says", so a phone-sized attach could take a wide window's session
// down with it, and a wide one could show a small attach rows it cannot display.
//
// A pure function rather than a line in the daemon, so the rule can be checked without a
// socket. The daemon's job is to remember who is looking; this is what it does with that.
inline ViewSize smallestView(const std::vector<ViewSize>& views, ViewSize fallback) {
    ViewSize best = fallback;
    bool any = false;
    for (const ViewSize& v : views) {
        if (v.rows < 1 || v.cols < 1)
            continue; // a view that has not said its size yet is nobody's limit
        if (!any || v.rows < best.rows || (v.rows == best.rows && v.cols < best.cols)) {
            best = v;
            any = true;
        }
    }
    return best;
}

} // namespace kai
