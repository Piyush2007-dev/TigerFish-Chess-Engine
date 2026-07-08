const http = require('http');
const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const url = require('url');

const PORT = 8080;
const ENGINE_EXE = path.join(__dirname, 'chess_engine.exe');

// Helper to run C++ subprocess
function runEngine(args) {
    return new Promise((resolve, reject) => {
        execFile(ENGINE_EXE, args, (error, stdout, stderr) => {
            if (error) {
                reject(stderr || error.message);
            } else {
                resolve(stdout.trim());
            }
        });
    });
}

const server = http.createServer(async (req, res) => {
    // Enable CORS for frontend
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    const parsedUrl = url.parse(req.url, true);
    const pathname = parsedUrl.pathname;

    // Route: Static files
    if (req.method === 'GET' && (pathname === '/' || pathname === '/index.html')) {
        const filePath = path.join(__dirname, 'index.html');
        fs.readFile(filePath, (err, data) => {
            if (err) {
                res.writeHead(404, { 'Content-Type': 'text/plain' });
                res.end('index.html not found. Make sure it is in the same directory as server.js.');
            } else {
                res.writeHead(200, { 'Content-Type': 'text/html' });
                res.end(data);
            }
        });
        return;
    }

    // Route: GET /api/state?fen=...
    if (req.method === 'GET' && pathname === '/api/state') {
        const fen = parsedUrl.query.fen;
        if (!fen) {
            res.writeHead(400, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'Missing FEN query parameter' }));
            return;
        }

        try {
            const output = await runEngine(['moves', fen]);
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(output);
        } catch (err) {
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'Engine error', details: err.toString() }));
        }
        return;
    }

    // Route: POST /api/move
    if (req.method === 'POST' && pathname === '/api/move') {
        let body = '';
        req.on('data', chunk => { body += chunk; });
        req.on('end', async () => {
            try {
                const data = JSON.parse(body);
                const { fen, move } = data;
                if (!fen || !move) {
                    res.writeHead(400, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: 'Missing FEN or move in request body' }));
                    return;
                }

                // 1. Play move
                const newFen = await runEngine(['make', fen, move]);

                // 2. Get state for the new position
                const output = await runEngine(['moves', newFen]);

                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(output);
            } catch (err) {
                res.writeHead(500, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Engine or parsing error', details: err.toString() }));
            }
        });
        return;
    }

    // 404 fallback
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not Found');
});

server.listen(PORT, () => {
    console.log(`TigerFish Chess server running at http://127.0.0.1:${PORT}`);
});
