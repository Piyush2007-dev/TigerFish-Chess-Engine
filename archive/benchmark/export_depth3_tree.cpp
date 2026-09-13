// export_depth3_tree.cpp — Generates a D3.js Graphic Tree Visualizer (Root to Leaf SVG Tree)
#include "../../engine/search.cpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

struct TreeNode {
    int id;
    int depth;
    string side;
    string move;
    int alpha_in;
    int beta_in;
    int alpha_out;
    int beta_out;
    int score;
    bool is_leaf;
    bool cutoff_triggered;
    int pruned_count;
    vector<TreeNode> children;
};

int global_node_id = 0;

TreeNode full_minimax_trace(Board& board, int depth, int alpha, int beta, bool maximizing, string move_played) {
    TreeNode node;
    node.id = ++global_node_id;
    node.depth = depth;
    node.side = maximizing ? "WHITE" : "BLACK";
    node.move = move_played;
    node.alpha_in = alpha;
    node.beta_in = beta;
    node.is_leaf = false;
    node.cutoff_triggered = false;
    node.pruned_count = 0;

    if (depth == 0) {
        node.is_leaf = true;
        node.score = Engine().evaluate(board);
        node.alpha_out = alpha;
        node.beta_out = beta;
        return node;
    }

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    if (moves.size() == 0) {
        node.is_leaf = true;
        if (is_in_check(board)) {
            node.score = maximizing ? (-20000 - depth) : (20000 + depth);
        } else {
            node.score = 0;
        }
        node.alpha_out = alpha;
        node.beta_out = beta;
        return node;
    }

    if (maximizing) {
        int best = -100000;
        for (int i = 0; i < moves.size(); i++) {
            uint32_t m = moves.move_list[i];
            string uci = move_to_uci(m);

            board.make_move(m);
            TreeNode child = full_minimax_trace(board, depth - 1, alpha, beta, false, uci);
            board.unmake_move();

            if (child.score > best) best = child.score;
            if (best > alpha) alpha = best;

            node.children.push_back(child);

            if (beta <= alpha) {
                node.cutoff_triggered = true;
                node.pruned_count = moves.size() - i - 1;
                break;
            }
        }
        node.score = best;
        node.alpha_out = alpha;
        node.beta_out = beta;
        return node;
    } else {
        int best = 100000;
        for (int i = 0; i < moves.size(); i++) {
            uint32_t m = moves.move_list[i];
            string uci = move_to_uci(m);

            board.make_move(m);
            TreeNode child = full_minimax_trace(board, depth - 1, alpha, beta, true, uci);
            board.unmake_move();

            if (child.score < best) best = child.score;
            if (best < beta) beta = best;

            node.children.push_back(child);

            if (beta <= alpha) {
                node.cutoff_triggered = true;
                node.pruned_count = moves.size() - i - 1;
                break;
            }
        }
        node.score = best;
        node.alpha_out = alpha;
        node.beta_out = beta;
        return node;
    }
}

void print_json(ofstream& out, const TreeNode& n, int indent = 2) {
    string ind(indent, ' ');
    out << ind << "{\n";
    out << ind << "  \"id\": " << n.id << ",\n";
    out << ind << "  \"depth\": " << n.depth << ",\n";
    out << ind << "  \"side\": \"" << n.side << "\",\n";
    out << ind << "  \"move\": \"" << n.move << "\",\n";
    out << ind << "  \"alpha_in\": " << n.alpha_in << ",\n";
    out << ind << "  \"beta_in\": " << n.beta_in << ",\n";
    out << ind << "  \"alpha_out\": " << n.alpha_out << ",\n";
    out << ind << "  \"beta_out\": " << n.beta_out << ",\n";
    out << ind << "  \"score\": " << n.score << ",\n";
    out << ind << "  \"is_leaf\": " << (n.is_leaf ? "true" : "false") << ",\n";
    out << ind << "  \"cutoff\": " << (n.cutoff_triggered ? "true" : "false") << ",\n";
    out << ind << "  \"pruned_moves\": " << n.pruned_count << ",\n";
    out << ind << "  \"children\": [\n";
    for (size_t i = 0; i < n.children.size(); i++) {
        print_json(out, n.children[i], indent + 4);
        if (i + 1 < n.children.size()) out << ",\n";
        else out << "\n";
    }
    out << ind << "  ]\n";
    out << ind << "}";
}

int main() {
    init_rays();
    init_magics();

    Board board;
    board.set_fen(board.START_FEN);

    cout << "Tracing complete Depth 3 recursion tree for SVG Tree Visualizer...\n";
    TreeNode root = full_minimax_trace(board, 3, -100000, 100000, true, "ROOT");

    ofstream out("old_files/benchmark/tree_visualizer.html");
    out << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>TigerFish Search Tree Diagram</title>
    <script src="https://d3js.org/d3.v7.min.js"></script>
    <style>
        body { font-family: 'Segoe UI', system-ui, sans-serif; background: #0f111a; color: #cdd6f4; margin: 0; overflow: hidden; }
        #header { position: absolute; top: 15px; left: 20px; z-index: 10; background: rgba(24, 24, 37, 0.9); padding: 15px 20px; border-radius: 10px; border: 1px solid #45475a; box-shadow: 0 8px 32px rgba(0,0,0,0.5); backdrop-filter: blur(10px); }
        h1 { margin: 0 0 8px 0; font-size: 20px; color: #89b4fa; }
        .stats { font-size: 13px; line-height: 1.6; }
        .legend { margin-top: 10px; display: flex; gap: 15px; font-size: 12px; }
        .legend-item { display: flex; align-items: center; gap: 6px; }
        .dot { width: 12px; height: 12px; border-radius: 50%; display: inline-block; }
        svg { width: 100vw; height: 100vh; cursor: grab; }
        svg:active { cursor: grabbing; }
        .link { fill: none; stroke: #45475a; stroke-width: 2px; }
        .link.cutoff-link { stroke: #fab387; stroke-width: 3px; stroke-dasharray: 4,4; }
        .node circle { cursor: pointer; transition: all 0.2s; }
        .node text { font-size: 12px; font-family: monospace; fill: #cdd6f4; text-shadow: 0 2px 4px rgba(0,0,0,0.8); }
        .tooltip { position: absolute; pointer-events: none; background: #181825; border: 1px solid #89b4fa; padding: 10px 14px; border-radius: 8px; font-family: monospace; font-size: 12px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); opacity: 0; transition: opacity 0.2s; z-index: 100; }
    </style>
</head>
<body>
    <div id="header">
        <h1>🐅 TigerFish Root-to-Leaf Search Tree (Depth 3)</h1>
        <div class="stats" id="stats"></div>
        <div class="legend">
            <div class="legend-item"><span class="dot" style="background:#89b4fa"></span> White (Max)</div>
            <div class="legend-item"><span class="dot" style="background:#f38ba8"></span> Black (Min)</div>
            <div class="legend-item"><span class="dot" style="background:#a6e3a1"></span> Leaf (Eval)</div>
            <div class="legend-item"><span class="dot" style="background:#fab387"></span> Cutoff</div>
        </div>
    </div>
    <div class="tooltip" id="tooltip"></div>
    <svg id="svg"></svg>

    <script>
    const treeData = 
)";
    
    print_json(out, root, 4);

    out << R"(;

    document.getElementById('stats').innerHTML = `
        Total Visited Nodes: <strong>)" << global_node_id << R"(</strong> | Best Move: <strong style="color:#a6e3a1">)" << root.children[0].move << R"(</strong> (Score: +)" << root.score << R"()<br>
        <span style="color:#f9e2af">Scroll to Zoom | Drag to Pan | Click nodes to Expand/Collapse</span>
    `;

    const svg = d3.select("#svg");
    const width = window.innerWidth;
    const height = window.innerHeight;

    const g = svg.append("g");

    const zoom = d3.zoom().scaleExtent([0.1, 3]).on("zoom", (e) => {
        g.attr("transform", e.transform);
    });

    svg.call(zoom);

    const root = d3.hierarchy(treeData);
    
    // Collapse deeper nodes initially so tree is clean to navigate
    root.children.forEach(collapse);

    function collapse(d) {
        if(d.children) {
            d._children = d.children;
            d._children.forEach(collapse);
            d.children = null;
        }
    }

    const treeLayout = d3.tree().nodeSize([45, 180]);

    update(root);

    function update(source) {
        treeLayout(root);

        const nodes = root.descendants();
        const links = root.links();

        // Normalize for fixed-depth vertical spacing
        nodes.forEach(d => { d.y = d.depth * 180; });

        // Update Nodes
        const node = g.selectAll('g.node')
            .data(nodes, d => d.data.id);

        const nodeEnter = node.enter().append('g')
            .attr('class', 'node')
            .attr("transform", d => `translate(${source.y0 || source.y},${source.x0 || source.x})`)
            .on('click', (event, d) => {
                if (d.children) {
                    d._children = d.children;
                    d.children = null;
                } else if (d._children) {
                    d.children = d._children;
                    d._children = null;
                }
                update(d);
            })
            .on('mouseover', (event, d) => {
                const tt = d3.select("#tooltip");
                tt.style("opacity", 1)
                  .style("left", (event.pageX + 15) + "px")
                  .style("top", (event.pageY + 15) + "px")
                  .html(`
                    <strong>Node ID #${d.data.id}</strong><br>
                    Move: <span style="color:#f9e2af">${d.data.move}</span><br>
                    Side: ${d.data.side}<br>
                    In: [α:${d.data.alpha_in}, β:${d.data.beta_in}]<br>
                    Out: [α:${d.data.alpha_out}, β:${d.data.beta_out}]<br>
                    Score: <strong>${d.data.score}</strong><br>
                    ${d.data.cutoff ? `<span style="color:#fab387">Cutoff! Pruned ${d.data.pruned_moves} moves</span>` : ''}
                  `);
            })
            .on('mouseout', () => { d3.select("#tooltip").style("opacity", 0); });

        nodeEnter.append('circle')
            .attr('r', 10)
            .attr('fill', d => {
                if (d.data.cutoff) return '#fab387';
                if (d.data.is_leaf) return '#a6e3a1';
                return d.data.side === 'WHITE' ? '#89b4fa' : '#f38ba8';
            })
            .attr('stroke', '#181825')
            .attr('stroke-width', 2);

        nodeEnter.append('text')
            .attr("dy", ".35em")
            .attr("x", d => d.children || d._children ? -15 : 15)
            .attr("text-anchor", d => d.children || d._children ? "end" : "start")
            .text(d => `${d.data.move} (${d.data.score})`);

        const nodeUpdate = nodeEnter.merge(node);

        nodeUpdate.transition().duration(300)
            .attr("transform", d => `translate(${d.y},${d.x})`);

        nodeUpdate.select('circle')
            .attr('fill', d => {
                if (d.data.cutoff) return '#fab387';
                if (d.data.is_leaf) return '#a6e3a1';
                return d.data.side === 'WHITE' ? '#89b4fa' : '#f38ba8';
            });

        const nodeExit = node.exit().transition().duration(300)
            .attr("transform", d => `translate(${source.y},${source.x})`)
            .remove();

        // Update Links
        const link = g.selectAll('path.link')
            .data(links, d => d.target.data.id);

        const linkEnter = link.enter().insert('path', "g")
            .attr('class', d => `link ${d.target.data.cutoff ? 'cutoff-link' : ''}`)
            .attr('d', d => {
                const o = {x: source.x0 || source.x, y: source.y0 || source.y};
                return diagonal(o, o);
            });

        const linkUpdate = linkEnter.merge(link);

        linkUpdate.transition().duration(300)
            .attr('d', d => diagonal(d.source, d.target));

        link.exit().transition().duration(300)
            .attr('d', d => {
                const o = {x: source.x, y: source.y};
                return diagonal(o, o);
            })
            .remove();

        nodes.forEach(d => {
            d.x0 = d.x;
            d.y0 = d.y;
        });
    }

    function diagonal(s, d) {
        return `M ${s.y} ${s.x}
                C ${(s.y + d.y) / 2} ${s.x},
                  ${(s.y + d.y) / 2} ${d.x},
                  ${d.y} ${d.x}`;
    }

    // Initial center positioning
    svg.call(zoom.transform, d3.zoomIdentity.translate(100, height / 2).scale(0.8));
    </script>
</body>
</html>
)";

    out.close();
    cout << "SUCCESS! Root-to-Leaf SVG Tree Visualizer generated at old_files/benchmark/tree_visualizer.html\n";
    return 0;
}
