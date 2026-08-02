const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const PORT = 5000;
const ENGINE_EXE = path.join(__dirname, 'game.exe');

// Spawn the C++ chess engine in interactive mode persistently
let engineProcess = null;
let currentResolve = null;
let stdoutBuffer = '';
const commandQueue = [];
let isEngineBusy = false;

function startEngine() {
    console.log("Spawning persistent TigerFish engine process...");
    engineProcess = spawn(ENGINE_EXE, ['interactive']);

    engineProcess.stdout.on('data', (data) => {
        stdoutBuffer += data.toString();
        const delim = '===READY===';
        const index = stdoutBuffer.indexOf(delim);
        if (index !== -1) {
            const response = stdoutBuffer.substring(0, index).trim();
            // find the end of the delimiter and any trailing newline characters (\r or \n)
            let endIdx = index + delim.length;
            while (endIdx < stdoutBuffer.length && (stdoutBuffer[endIdx] === '\r' || stdoutBuffer[endIdx] === '\n')) {
                endIdx++;
            }
            stdoutBuffer = stdoutBuffer.substring(endIdx);
            if (currentResolve) {
                currentResolve(response);
                currentResolve = null;
            }
        }
    });

    engineProcess.stderr.on('data', (data) => {
        console.error(`Engine stderr: ${data}`);
    });

    engineProcess.on('close', (code) => {
        console.log(`Engine process exited with code ${code}. Restarting...`);
        isEngineBusy = false;
        stdoutBuffer = '';
        if (currentResolve) {
            currentResolve(JSON.stringify({ error: "Engine crashed" }));
            currentResolve = null;
        }
        startEngine();
    });
}

startEngine();

function executeQueue() {
    if (isEngineBusy || commandQueue.length === 0) return;
    isEngineBusy = true;
    const { cmd, resolve } = commandQueue.shift();
    currentResolve = (response) => {
        isEngineBusy = false;
        resolve(response);
        executeQueue(); // Process next queued command
    };
    engineProcess.stdin.write(cmd + '\n');
}

function sendCommand(cmd) {
    return new Promise((resolve) => {
        commandQueue.push({ cmd, resolve });
        executeQueue();
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

    const parsedUrl = new URL(req.url, `http://localhost:${PORT}`);
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
        const fen = parsedUrl.searchParams.get('fen');
        if (!fen) {
            res.writeHead(400, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'Missing FEN query parameter' }));
            return;
        }

        try {
            const output = await sendCommand(`moves ${fen}`);
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
                const { fen, move, depth } = data;
                if (!fen || !move) {
                    res.writeHead(400, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: 'Missing FEN or move in request body' }));
                    return;
                }

                const searchDepth = (depth !== undefined) ? parseInt(depth) : 6;
                const output = await sendCommand(`make ${move} ${searchDepth} ${fen}`);

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
