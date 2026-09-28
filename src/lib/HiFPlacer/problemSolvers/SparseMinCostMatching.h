#ifndef AMF_SPARSE_MIN_COST_MATCHING_H
#define AMF_SPARSE_MIN_COST_MATCHING_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

// Unit-capacity bipartite matching. Maximize cardinality up to the requested
// limit, then minimize the sum of the original candidate-edge costs.
namespace amf_matching
{
using Adjacency = std::vector<std::vector<std::pair<int, float>>>;
using Clock = std::chrono::steady_clock;
inline double seconds(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct Result
{
    std::vector<int> leftToRight;
    int cardinality = 0;
    double cost = 0;
    std::size_t edges = 0, components = 0, largestLeft = 0, largestEdges = 0;
    double preparationSeconds = 0, solveSeconds = 0, longestTaskSeconds = 0;
};

namespace detail
{
class DisjointSet
{
    std::vector<int> parent, size;
  public:
    explicit DisjointSet(int count) : parent(count), size(count, 1)
    {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int find(int node)
    {
        while (parent[node] != node)
        {
            parent[node] = parent[parent[node]];
            node = parent[node];
        }
        return node;
    }
    void unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (size[a] < size[b]) std::swap(a, b);
        parent[b] = a;
        size[a] += size[b];
    }
};

struct Component
{
    std::vector<int> left, right;
    std::size_t edges = 0;
};

// Consecutive forward/reverse edges avoid per-edge allocation. Reverse cost
// is exactly -forward cost; there is no path-length-dependent epsilon penalty.
class Network
{
    struct Edge { int to; double cost; bool available; };
    std::vector<Edge> edges;
    std::vector<std::vector<int>> outgoing;
  public:
    Network(int nodes, std::size_t forwardEdges) : outgoing(nodes)
    {
        edges.reserve(2 * forwardEdges);
    }
    int add(int from, int to, double cost)
    {
        const int id = static_cast<int>(edges.size());
        edges.push_back({to, cost, true});
        edges.push_back({from, -cost, false});
        outgoing[from].push_back(id);
        outgoing[to].push_back(id + 1);
        return id;
    }
    bool used(int edge) const { return !edges[edge].available; }

    void augment(int source, int sink, int limit, bool useDijkstra)
    {
        const int count = static_cast<int>(outgoing.size());
        const double infinity = std::numeric_limits<double>::infinity();
        std::vector<double> potential(count, 0), distance(count);
        std::vector<int> previous(count);
        std::vector<int> fifo;
        fifo.reserve(count);
        std::vector<unsigned char> queued(count, 0);
        using Entry = std::pair<double, int>;
        for (int flow = 0; flow < limit; ++flow)
        {
            std::fill(distance.begin(), distance.end(), infinity);
            std::fill(previous.begin(), previous.end(), -1);
            distance[source] = 0;
            if (useDijkstra)
            {
                std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
                queue.emplace(0, source);
                while (!queue.empty())
                {
                    const auto current = queue.top();
                    queue.pop();
                    const int from = current.second;
                    if (current.first != distance[from]) continue;
                    for (const int id : outgoing[from])
                    {
                        const auto &edge = edges[id];
                        if (!edge.available) continue;
                        double reduced = edge.cost + potential[from] - potential[edge.to];
                        if (reduced < 0)
                        {
                            const double tolerance = 64 * std::numeric_limits<double>::epsilon() *
                                (1 + std::abs(edge.cost) + std::abs(potential[from]) + std::abs(potential[edge.to]));
                            if (reduced < -tolerance)
                                throw std::logic_error("matching: invalid reduced cost");
                            reduced = 0; // Only absorb floating-point cancellation.
                        }
                        const double candidate = current.first + reduced;
                        if (candidate < distance[edge.to])
                        {
                            distance[edge.to] = candidate;
                            previous[edge.to] = id;
                            queue.emplace(candidate, edge.to);
                        }
                    }
                }
            }
            else
            {
                fifo.clear();
                std::fill(queued.begin(), queued.end(), 0);
                fifo.push_back(source);
                queued[source] = 1;
                for (std::size_t head = 0; head < fifo.size(); ++head)
                {
                    const int from = fifo[head];
                    queued[from] = 0;
                    for (int id : outgoing[from])
                    {
                        const auto &edge = edges[id];
                        if (!edge.available) continue;
                        const double candidate = distance[from] + edge.cost;
                        const double tolerance = 64 * std::numeric_limits<double>::epsilon() *
                            (1 + std::abs(distance[from]) + std::abs(edge.cost));
                        if (candidate + tolerance < distance[edge.to])
                        {
                            distance[edge.to] = candidate;
                            previous[edge.to] = id;
                            if (!queued[edge.to])
                            {
                                fifo.push_back(edge.to);
                                queued[edge.to] = 1;
                            }
                        }
                    }
                }
            }
            if (previous[sink] < 0) break; // A maximum partial matching is valid.
            if (useDijkstra)
                for (int node = 0; node < count; ++node)
                    if (std::isfinite(distance[node])) potential[node] += distance[node];
            for (int node = sink; node != source; )
            {
                const int id = previous[node];
                if (id < 0) throw std::logic_error("matching: missing augmenting predecessor");
                edges[id].available = false;
                edges[id ^ 1].available = true;
                node = edges[id ^ 1].to;
            }
        }
    }
};

// Sparse rectangular assignment by a shortest alternating path from each new
// row. Private dummy columns make deficient candidate graphs feasible. Their
// penalty exceeds every possible real-edge total, so maximizing the number of
// real assignments takes precedence over minimizing cost. No dense matrix is
// created. The primal/dual invariant is u[row] + v[column] <= edge cost, with
// equality on assigned edges and v == 0 on unassigned columns.
inline void solveAssignment(const Component &component, const Adjacency &adjacency,
                            const std::vector<int> &rightLocal, std::vector<int> &matches)
{
    const int rows = static_cast<int>(component.left.size());
    const int realColumns = static_cast<int>(component.right.size());
    const int columns = realColumns + rows;
    double maximumCost = 0;
    for (int left : component.left)
        for (const auto &edge : adjacency[left]) maximumCost = std::max(maximumCost, double(edge.second));
    const double dummyCost = (rows + 1.0) * (maximumCost + 1.0);
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> rowDual(rows, 0), columnDual(columns, 0), distance(columns, infinity);
    std::vector<int> rowColumn(rows, -1), columnRow(columns, -1), predecessor(columns, -1);
    std::vector<unsigned char> settled(columns, 0);
    std::vector<int> touched, visited;
    // Free columns win ties, avoiding unnecessary zero-cost alternating trees.
    using Entry = std::tuple<double, int, int>;
    std::vector<Entry> heap;
    touched.reserve(columns);
    visited.reserve(columns);
    heap.reserve(columns);
    for (int root = 0; root < rows; ++root)
    {
        rowDual[root] = dummyCost - columnDual[realColumns + root];
        for (const auto &edge : adjacency[component.left[root]])
            rowDual[root] = std::min(rowDual[root], double(edge.second) - columnDual[rightLocal[edge.first]]);
        auto relaxRow = [&](int row, double baseDistance) {
            auto offer = [&](int column, double cost) {
                if (settled[column]) return;
                double reduced = cost - rowDual[row] - columnDual[column];
                if (reduced < 0)
                {
                    const double tolerance = 64 * std::numeric_limits<double>::epsilon() *
                        (1 + std::abs(cost) + std::abs(rowDual[row]) + std::abs(columnDual[column]));
                    if (reduced < -tolerance) throw std::logic_error("assignment: infeasible dual potentials");
                    reduced = 0;
                }
                const double candidate = baseDistance + reduced;
                if (candidate < distance[column])
                {
                    if (!std::isfinite(distance[column])) touched.push_back(column);
                    distance[column] = candidate;
                    predecessor[column] = row;
                    heap.emplace_back(candidate, columnRow[column] < 0 ? 0 : 1, column);
                    std::push_heap(heap.begin(), heap.end(), std::greater<Entry>());
                }
            };
            for (const auto &edge : adjacency[component.left[row]]) offer(rightLocal[edge.first], edge.second);
            offer(realColumns + row, dummyCost);
        };
        relaxRow(root, 0);
        int endpoint = -1;
        while (!heap.empty())
        {
            std::pop_heap(heap.begin(), heap.end(), std::greater<Entry>());
            const auto entry = heap.back();
            heap.pop_back();
            const int column = std::get<2>(entry);
            if (settled[column] || std::get<0>(entry) != distance[column]) continue;
            settled[column] = 1;
            visited.push_back(column);
            if (columnRow[column] < 0) { endpoint = column; break; }
            relaxRow(columnRow[column], distance[column]);
        }
        if (endpoint < 0) throw std::logic_error("assignment: private dummy column was not reachable");
        const double pathCost = distance[endpoint];
        rowDual[root] += pathCost;
        for (int column : visited)
        {
            const double delta = pathCost - distance[column];
            if (columnRow[column] >= 0) rowDual[columnRow[column]] += delta;
            columnDual[column] -= delta;
        }
        for (int column = endpoint; column >= 0; )
        {
            const int row = predecessor[column];
            if (row < 0) throw std::logic_error("assignment: incomplete alternating path");
            const int previousColumn = rowColumn[row];
            rowColumn[row] = column;
            columnRow[column] = row;
            column = previousColumn;
        }
        for (int column : touched)
        {
            distance[column] = infinity;
            predecessor[column] = -1;
            settled[column] = 0;
        }
        heap.clear();
        touched.clear();
        visited.clear();
    }
    for (int row = 0; row < rows; ++row)
    {
        if (rowColumn[row] < 0) throw std::logic_error("assignment: missing row assignment");
        if (rowColumn[row] < realColumns) matches[component.left[row]] = component.right[rowColumn[row]];
    }
}

inline void solveComponent(const Component &component, const Adjacency &adjacency,
                           const std::vector<int> &rightLocal, int limit, std::vector<int> &matches,
                           bool useDijkstra, bool useAssignment)
{
    if (component.edges == 0 || limit == 0) return;
    if (component.left.size() == 1)
    {
        const int left = component.left.front();
        const auto best = std::min_element(adjacency[left].begin(), adjacency[left].end(),
            [](const auto &a, const auto &b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
        matches[left] = best->first;
        return;
    }
    if (component.right.size() == 1)
    {
        int bestLeft = -1;
        float bestCost = std::numeric_limits<float>::infinity();
        for (int left : component.left)
            for (const auto &edge : adjacency[left])
                if (edge.second < bestCost)
                {
                    bestLeft = left;
                    bestCost = edge.second;
                }
        matches[bestLeft] = component.right.front();
        return;
    }
    const int leftCount = static_cast<int>(component.left.size());
    if (useAssignment && limit >= leftCount)
    {
        solveAssignment(component, adjacency, rightLocal, matches);
        return;
    }
    const int source = leftCount + static_cast<int>(component.right.size());
    const int sink = source + 1;
    Network network(sink + 1, component.edges + source);
    // The edge ID table is linear in the candidate count, with no global-node
    // arrays inside a task. Extraction retains the original candidate identity.
    std::vector<std::vector<int>> candidateEdges(leftCount);
    for (int local = 0; local < leftCount; ++local)
    {
        const auto &row = adjacency[component.left[local]];
        candidateEdges[local].reserve(row.size());
        for (const auto &edge : row)
            candidateEdges[local].push_back(network.add(local, leftCount + rightLocal[edge.first], edge.second));
        network.add(source, local, 0);
    }
    for (int local = 0; local < static_cast<int>(component.right.size()); ++local)
        network.add(leftCount + local, sink, 0);
    network.augment(source, sink, std::min(limit, leftCount), useDijkstra || useAssignment);
    for (int local = 0; local < leftCount; ++local)
        for (std::size_t edge = 0; edge < candidateEdges[local].size(); ++edge)
            if (network.used(candidateEdges[local][edge]))
            {
                const int left = component.left[local];
                if (matches[left] >= 0) throw std::logic_error("matching: duplicate left assignment");
                matches[left] = adjacency[left][edge].first;
            }
}
} // namespace detail

inline Result solve(const Adjacency &adjacency, int rightCount, int requested, int threads,
                    bool useDijkstra = true, bool useAssignment = false)
{
    const auto started = Clock::now();
    const int leftCount = static_cast<int>(adjacency.size());
    if (rightCount < 0 || requested < 0 || requested > leftCount || threads < 1)
        throw std::invalid_argument("matching: invalid dimensions, target, or thread count");
    Result result;
    result.leftToRight.assign(leftCount, -1);
    detail::DisjointSet sets(leftCount + rightCount);
    std::vector<unsigned char> usedRight(rightCount, 0);
    for (int left = 0; left < leftCount; ++left)
        for (const auto &edge : adjacency[left])
        {
            if (edge.first < 0 || edge.first >= rightCount || !std::isfinite(edge.second) || edge.second < 0)
                throw std::invalid_argument("matching: invalid candidate endpoint or nonnegative cost");
            sets.unite(left, leftCount + edge.first);
            usedRight[edge.first] = 1;
            ++result.edges;
        }
    // For a global limit below the number of left nodes, components compete for
    // that limit. Keep one network in this uncommon case instead of independently
    // truncating each component (which would change the optimization problem).
    const bool limited = requested < leftCount;
    std::vector<detail::Component> components;
    std::vector<int> rootToComponent(leftCount + rightCount, -1), rightLocal(rightCount, -1);
    for (int left = 0; left < leftCount; ++left)
    {
        const int root = limited ? 0 : sets.find(left);
        if (rootToComponent[root] < 0)
        {
            rootToComponent[root] = static_cast<int>(components.size());
            components.emplace_back();
        }
        auto &component = components[rootToComponent[root]];
        component.left.push_back(left);
        component.edges += adjacency[left].size();
    }
    for (int right = 0; right < rightCount; ++right)
        if (usedRight[right])
        {
            const int root = limited ? 0 : sets.find(leftCount + right);
            auto &component = components[rootToComponent[root]];
            rightLocal[right] = static_cast<int>(component.right.size());
            component.right.push_back(right);
        }
    result.components = components.size();
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(components.size()); ++i)
    {
        const auto &component = components[i];
        result.largestLeft = std::max(result.largestLeft, component.left.size());
        result.largestEdges = std::max(result.largestEdges, component.edges);
        if (component.edges) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return static_cast<long double>(components[a].left.size()) * components[a].edges >
               static_cast<long double>(components[b].left.size()) * components[b].edges;
    });
    result.preparationSeconds = seconds(started);
    const auto solveStarted = Clock::now();
    std::vector<double> taskTimes(order.size(), 0);
    std::vector<std::exception_ptr> errors(order.size());
    const int workerCount = std::max(1, std::min(threads, static_cast<int>(order.size())));
#pragma omp parallel for schedule(dynamic, 1) num_threads(workerCount)
    for (int task = 0; task < static_cast<int>(order.size()); ++task)
    {
        const auto taskStarted = Clock::now();
        try
        {
            detail::solveComponent(components[order[task]], adjacency, rightLocal, requested,
                                   result.leftToRight, useDijkstra, useAssignment);
        }
        catch (...) { errors[task] = std::current_exception(); }
        taskTimes[task] = seconds(taskStarted);
    }
    for (const auto &error : errors) if (error) std::rethrow_exception(error);
    result.solveSeconds = seconds(solveStarted);
    for (double elapsed : taskTimes) result.longestTaskSeconds = std::max(result.longestTaskSeconds, elapsed);
    std::vector<int> owner(rightCount, -1);
    for (int left = 0; left < leftCount; ++left)
    {
        const int right = result.leftToRight[left];
        if (right < 0) continue;
        if (right >= rightCount || owner[right] >= 0)
            throw std::logic_error("matching: duplicate or invalid right assignment");
        owner[right] = left;
        double cost = std::numeric_limits<double>::infinity();
        for (const auto &edge : adjacency[left])
            if (edge.first == right) cost = std::min(cost, static_cast<double>(edge.second));
        if (!std::isfinite(cost)) throw std::logic_error("matching: assignment outside candidate graph");
        result.cost += cost;
        ++result.cardinality;
    }
    if (result.cardinality > requested) throw std::logic_error("matching: exceeded target cardinality");
    return result;
}
} // namespace amf_matching
#endif
