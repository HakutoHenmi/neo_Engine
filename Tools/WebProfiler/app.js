// Configuration
const BASE_URL = 'http://127.0.0.1:8080/api';
const METRICS_URL = `${BASE_URL}/metrics`;
const PARAMS_URL = `${BASE_URL}/parameters`;

const POLL_INTERVAL = 100;

// DOM Elements
const connectionDot = document.getElementById('connection-dot');
const connectionStatus = document.getElementById('connection-status');
const valFps = document.getElementById('val-fps');
const valDt = document.getElementById('val-dt');
const valDrawcalls = document.getElementById('val-drawcalls');
const valParticles = document.getElementById('val-particles');
const valCpu = document.getElementById('val-cpu');
const valGpu = document.getElementById('val-gpu');
const valRam = document.getElementById('val-ram');
const valVram = document.getElementById('val-vram');
const valLights = document.getElementById('val-lights');

// // Parameters DOM getters (elements rendered dynamically)
function getParamElements() {
    return {
        ambR: document.getElementById('amb-r'),
        ambG: document.getElementById('amb-g'),
        ambB: document.getElementById('amb-b'),
        ambRVal: document.getElementById('amb-r-val'),
        ambGVal: document.getElementById('amb-g-val'),
        ambBVal: document.getElementById('amb-b-val'),
        playerSpeed: document.getElementById('player-speed'),
        playerSpeedVal: document.getElementById('player-speed-val')
    };
}

// Chart initialization
const chartConfig = {
    type: 'line',
    options: {
        responsive: true,
        animation: false,
        scales: {
            x: { display: false },
            y: {
                beginAtZero: true,
                grid: { color: 'rgba(255,255,255,0.05)' },
                ticks: { color: '#94a3b8' }
            }
        },
        plugins: { legend: { display: false } },
        elements: { point: { radius: 0 }, line: { tension: 0.4, borderWidth: 3 } }
    }
};

const fpsCtx = document.getElementById('fpsChart').getContext('2d');
const fpsChart = new Chart(fpsCtx, {
    ...chartConfig,
    data: {
        labels: Array(50).fill(''),
        datasets: [{
            data: Array(50).fill(0),
            borderColor: '#38bdf8',
            backgroundColor: 'rgba(56, 189, 248, 0.1)',
            fill: true
        }]
    }
});

const drawCtx = document.getElementById('drawChart').getContext('2d');
const drawChart = new Chart(drawCtx, {
    ...chartConfig,
    data: {
        labels: Array(50).fill(''),
        datasets: [{
            data: Array(50).fill(0),
            borderColor: '#10b981',
            backgroundColor: 'rgba(16, 185, 129, 0.1)',
            fill: true
        }]
    }
});

// Update data
function updateChart(chart, newValue) {
    const data = chart.data.datasets[0].data;
    data.push(newValue);
    data.shift();
    chart.update();
}

async function fetchMetrics() {
    try {
        const response = await fetch(METRICS_URL);
        if (!response.ok) throw new Error('Network error');
        
        const data = await response.json();
        
        if (!connectionDot.classList.contains('connected')) {
            connectionDot.classList.add('connected');
            connectionStatus.textContent = 'Connected';
            connectionStatus.style.color = 'var(--success)';
            
            // Fetch initial parameters when re-connected
            fetchParameters();
        }

        valFps.textContent = data.fps.toFixed(1);
        valDt.textContent = (data.deltaTime * 1000).toFixed(2);
        valDrawcalls.textContent = data.drawCalls;
        valParticles.textContent = data.particleCount;
        valCpu.textContent = data.cpuLogicTimeMs ? data.cpuLogicTimeMs.toFixed(2) : "0.00";
        valGpu.textContent = data.gpuRenderTimeMs ? data.gpuRenderTimeMs.toFixed(2) : "0.00";
        valRam.textContent = data.systemRamUsageMB ? data.systemRamUsageMB.toFixed(0) : "0";
        valVram.textContent = data.videoRamUsageMB ? data.videoRamUsageMB.toFixed(0) : "0";
        valLights.textContent = data.lightCount ? data.lightCount : "0";

        updateChart(fpsChart, data.fps);
        updateChart(drawChart, data.drawCalls);
        
    } catch (error) {
        if (connectionDot.classList.contains('connected')) {
            connectionDot.classList.remove('connected');
            connectionStatus.textContent = 'Disconnected';
            connectionStatus.style.color = 'var(--text-muted)';
        }
    }
}

// Initial Parameter Fetch
async function fetchParameters() {
    try {
        const res = await fetch(PARAMS_URL);
        if(!res.ok) return;
        const data = await res.json();
        const p = getParamElements();
        
        if(data.AmbientColor) {
            if (p.ambR && p.ambRVal) { p.ambR.value = data.AmbientColor.R; p.ambRVal.textContent = data.AmbientColor.R.toFixed(2); }
            if (p.ambG && p.ambGVal) { p.ambG.value = data.AmbientColor.G; p.ambGVal.textContent = data.AmbientColor.G.toFixed(2); }
            if (p.ambB && p.ambBVal) { p.ambB.value = data.AmbientColor.B; p.ambBVal.textContent = data.AmbientColor.B.toFixed(2); }
        }
        if(data.Player) {
            if (p.playerSpeed && p.playerSpeedVal) { p.playerSpeed.value = data.Player.Speed; p.playerSpeedVal.textContent = data.Player.Speed.toFixed(1); }
        }
    } catch(e) {}
}

// Update Engine Parameter
async function sendParameter(target, property, value) {
    try {
        await fetch(PARAMS_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ target, property, value: parseFloat(value) })
        });
    } catch(e) {
        console.error("Failed to send parameter", e);
    }
}

// Event Listeners for UI
function setupParamControl(inputEl, valEl, target, prop, formatFn) {
    if (!inputEl || !valEl) return;
    inputEl.addEventListener('input', (e) => {
        valEl.textContent = formatFn(e.target.value);
        sendParameter(target, prop, e.target.value);
    });
}


// Start polling
setInterval(fetchMetrics, POLL_INTERVAL);

// Layout Initialization (Split.js)
Split(['#pane-hierarchy', '#pane-center', '#pane-inspector'], {
    sizes: [20, 55, 25],
    minSize: [150, 400, 200],
    gutterSize: 6,
    cursor: 'col-resize'
});

Split(['#pane-workspace', '#pane-console'], {
    direction: 'vertical',
    sizes: [75, 25],
    minSize: [200, 100],
    gutterSize: 6,
    cursor: 'row-resize'
});

// --- Hierarchy & Dynamic Inspector Logic ---
const hierarchyList = document.getElementById('hierarchy-list');
const inspectorDynamic = document.getElementById('inspector-dynamic');
const inspectorEmpty = document.getElementById('inspector-empty');

// Comprehensive Entity-Component Database (Supports engine dynamic updates)
const entityDataStore = {
    Sun: {
        name: "Sun",
        tag: "DirectionalLight",
        components: [
            {
                type: "Transform",
                properties: {
                    position: { x: 0, y: 150, z: 0 },
                    rotation: { x: 45, y: -30, z: 0 },
                    scale: { x: 1, y: 1, z: 1 }
                }
            },
            {
                type: "Directional Light",
                properties: {
                    ambientColor: { r: 0.40, g: 0.40, b: 0.45 },
                    intensity: 1.8,
                    castShadows: true,
                    shadowResolution: 2048
                }
            }
        ]
    },
    Player: {
        name: "Player",
        tag: "Actor",
        components: [
            {
                type: "Transform",
                properties: {
                    position: { x: 12.4, y: 0.5, z: -4.2 },
                    rotation: { x: 0, y: 90, z: 0 },
                    scale: { x: 1, y: 1, z: 1 }
                }
            },
            {
                type: "Character Controller",
                properties: {
                    speed: 15.0,
                    jumpForce: 8.5,
                    gravity: 9.81,
                    isGrounded: true
                }
            },
            {
                type: "Mesh Renderer",
                properties: {
                    mesh: "Models/Hero_Knight.mesh",
                    castShadows: true,
                    material: "Materials/Player_Hero.mat"
                }
            }
        ]
    },
    CrystalForest_Main: {
        name: "CrystalForest_Main",
        tag: "StaticMesh",
        components: [
            {
                type: "Transform",
                properties: {
                    position: { x: 0, y: 0, z: 0 },
                    rotation: { x: 0, y: 0, z: 0 },
                    scale: { x: 1, y: 1, z: 1 }
                }
            },
            {
                type: "Mesh Renderer",
                properties: {
                    mesh: "Models/CrystalForest_Environment.mesh",
                    triangles: 142500,
                    materials: ["Mat_Crystal_01", "Mat_Terrain_Moss"]
                }
            },
            {
                type: "RigidBody (Static)",
                properties: {
                    friction: 0.8,
                    restitution: 0.1
                }
            }
        ]
    },
    CrystalForest_Sky: {
        name: "CrystalForest_Sky",
        tag: "Skybox",
        components: [
            {
                type: "Skybox Component",
                properties: {
                    cubemap: "Textures/Sky_NightForest.hdr",
                    exposure: 1.2,
                    rotationSpeed: 0.05
                }
            }
        ]
    }
};

function renderEntityInspector(entityName) {
    const data = entityDataStore[entityName];
    if (!data) {
        inspectorDynamic.style.display = 'none';
        inspectorEmpty.style.display = 'block';
        return;
    }

    inspectorEmpty.style.display = 'none';
    inspectorDynamic.style.display = 'block';
    inspectorDynamic.innerHTML = '';

    // Header
    const headerCard = document.createElement('div');
    headerCard.className = 'settings-group glass-card';
    headerCard.innerHTML = `
        <div class="group-header" style="display:flex; justify-content:space-between; align-items:center;">
            <h3>${data.name}</h3>
            <span class="tag" style="font-size:0.75rem; background:rgba(56,189,248,0.2); color:var(--accent); padding:2px 8px; border-radius:4px;">${data.tag}</span>
        </div>
    `;
    inspectorDynamic.appendChild(headerCard);

    // Components
    data.components.forEach((comp, cIdx) => {
        const card = document.createElement('div');
        card.className = 'component-card';

        let bodyHtml = '';
        if (comp.type === 'Transform') {
            bodyHtml = `
                <div class="vector3-control">
                    <label>Position</label>
                    <div class="vector3-inputs">
                        <div class="vector-field"><span class="axis-tag x">X</span><input type="number" step="0.1" value="${comp.properties.position.x}" data-path="${entityName}.Transform.pos.x"></div>
                        <div class="vector-field"><span class="axis-tag y">Y</span><input type="number" step="0.1" value="${comp.properties.position.y}" data-path="${entityName}.Transform.pos.y"></div>
                        <div class="vector-field"><span class="axis-tag z">Z</span><input type="number" step="0.1" value="${comp.properties.position.z}" data-path="${entityName}.Transform.pos.z"></div>
                    </div>
                </div>
                <div class="vector3-control" style="margin-top:8px;">
                    <label>Rotation (Deg)</label>
                    <div class="vector3-inputs">
                        <div class="vector-field"><span class="axis-tag x">X</span><input type="number" step="1" value="${comp.properties.rotation.x}" data-path="${entityName}.Transform.rot.x"></div>
                        <div class="vector-field"><span class="axis-tag y">Y</span><input type="number" step="1" value="${comp.properties.rotation.y}" data-path="${entityName}.Transform.rot.y"></div>
                        <div class="vector-field"><span class="axis-tag z">Z</span><input type="number" step="1" value="${comp.properties.rotation.z}" data-path="${entityName}.Transform.rot.z"></div>
                    </div>
                </div>
                <div class="vector3-control" style="margin-top:8px;">
                    <label>Scale</label>
                    <div class="vector3-inputs">
                        <div class="vector-field"><span class="axis-tag x">X</span><input type="number" step="0.1" value="${comp.properties.scale.x}" data-path="${entityName}.Transform.scale.x"></div>
                        <div class="vector-field"><span class="axis-tag y">Y</span><input type="number" step="0.1" value="${comp.properties.scale.y}" data-path="${entityName}.Transform.scale.y"></div>
                        <div class="vector-field"><span class="axis-tag z">Z</span><input type="number" step="0.1" value="${comp.properties.scale.z}" data-path="${entityName}.Transform.scale.z"></div>
                    </div>
                </div>
            `;
        } else if (comp.type === 'Directional Light') {
            bodyHtml = `
                <div class="control-row">
                    <label>Intensity</label>
                    <input type="range" min="0" max="5" step="0.1" value="${comp.properties.intensity}" id="light-intensity-input">
                    <span class="val-display" id="light-intensity-val">${comp.properties.intensity}</span>
                </div>
                <div class="control-row">
                    <label>Ambient Light R</label>
                    <input type="range" min="0" max="1" step="0.01" value="${comp.properties.ambientColor.r}" id="amb-r">
                    <span class="val-display" id="amb-r-val">${comp.properties.ambientColor.r.toFixed(2)}</span>
                </div>
                <div class="control-row">
                    <label>Ambient Light G</label>
                    <input type="range" min="0" max="1" step="0.01" value="${comp.properties.ambientColor.g}" id="amb-g">
                    <span class="val-display" id="amb-g-val">${comp.properties.ambientColor.g.toFixed(2)}</span>
                </div>
                <div class="control-row">
                    <label>Ambient Light B</label>
                    <input type="range" min="0" max="1" step="0.01" value="${comp.properties.ambientColor.b}" id="amb-b">
                    <span class="val-display" id="amb-b-val">${comp.properties.ambientColor.b.toFixed(2)}</span>
                </div>
            `;
        } else if (comp.type === 'Character Controller') {
            bodyHtml = `
                <div class="control-row">
                    <label>Movement Speed</label>
                    <input type="range" min="5" max="40" step="0.5" value="${comp.properties.speed}" id="player-speed">
                    <span class="val-display" id="player-speed-val">${comp.properties.speed.toFixed(1)}</span>
                </div>
                <div class="control-row">
                    <label>Jump Force</label>
                    <input type="number" step="0.5" value="${comp.properties.jumpForce}" class="glass-input" style="width:70px; text-align:right;">
                </div>
            `;
        } else {
            // Generic Key-Value Listing
            bodyHtml = Object.entries(comp.properties).map(([k, v]) => `
                <div class="control-row" style="display:flex; justify-content:space-between; font-size:0.85rem;">
                    <span style="color:var(--text-muted);">${k}:</span>
                    <span style="font-family:monospace;">${Array.isArray(v) ? v.join(', ') : v}</span>
                </div>
            `).join('');
        }

        card.innerHTML = `
            <div class="component-header">
                <span>⚙️ ${comp.type}</span>
            </div>
            <div class="component-body">
                ${bodyHtml}
            </div>
        `;
        inspectorDynamic.appendChild(card);
    });

    // Wire up event listeners for inputs
    inspectorDynamic.querySelectorAll('input').forEach(input => {
        input.addEventListener('change', (e) => {
            const path = e.target.getAttribute('data-path') || e.target.id;
            sendParameter(entityName, path, e.target.value);
            engineLog(`Updated [${entityName}] ${path} => ${e.target.value}`);
        });
    });

    // Re-wire ambient / player speed sliders if rendered
    const ambR = document.getElementById('amb-r');
    const ambG = document.getElementById('amb-g');
    const ambB = document.getElementById('amb-b');
    const ambRVal = document.getElementById('amb-r-val');
    const ambGVal = document.getElementById('amb-g-val');
    const ambBVal = document.getElementById('amb-b-val');
    if (ambR && ambRVal) setupParamControl(ambR, ambRVal, "AmbientColor", "R", v => parseFloat(v).toFixed(2));
    if (ambG && ambGVal) setupParamControl(ambG, ambGVal, "AmbientColor", "G", v => parseFloat(v).toFixed(2));
    if (ambB && ambBVal) setupParamControl(ambB, ambBVal, "AmbientColor", "B", v => parseFloat(v).toFixed(2));

    const playerSpeed = document.getElementById('player-speed');
    const playerSpeedVal = document.getElementById('player-speed-val');
    if (playerSpeed && playerSpeedVal) setupParamControl(playerSpeed, playerSpeedVal, "Player", "Speed", v => parseFloat(v).toFixed(1));
}

hierarchyList.addEventListener('click', (e) => {
    const li = e.target.closest('li');
    if (!li) return;

    hierarchyList.querySelectorAll('li').forEach(el => el.classList.remove('selected'));
    li.classList.add('selected');

    const entityName = li.getAttribute('data-entity');
    renderEntityInspector(entityName);
    engineLog(`Selected Entity: ${entityName}`);
});

// Render initial selection
renderEntityInspector('Player');


// --- Console Logic ---
const consoleContent = document.getElementById('console-content');

function engineLog(message, type = 'info') {
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    
    const time = new Date().toLocaleTimeString();
    entry.textContent = `[${time}] ${message}`;
    
    consoleContent.appendChild(entry);
    consoleContent.scrollTop = consoleContent.scrollHeight;
}

// Override console methods to also log to our custom console
const origLog = console.log;
const origWarn = console.warn;
const origError = console.error;

console.log = (...args) => {
    origLog(...args);
    engineLog(args.join(' '), 'info');
};
console.warn = (...args) => {
    origWarn(...args);
    engineLog(args.join(' '), 'warn');
};
console.error = (...args) => {
    origError(...args);
    engineLog(args.join(' '), 'error');
};

// Tab Switching Logic (Top Toolbar)
document.querySelectorAll('.tab-btn').forEach(tab => {
    tab.addEventListener('click', () => {
        document.querySelectorAll('.tab-btn').forEach(t => t.classList.remove('active'));
        document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
        
        tab.classList.add('active');
        const target = tab.getAttribute('data-tab');
        document.getElementById(`tab-${target}`).classList.add('active');
    });
});

// AI Assistant Logic
const btnAnalyze = document.getElementById('btn-analyze');
const aiMessages = document.getElementById('ai-messages');
const apiKeyInput = document.getElementById('groq-api-key');
const btnSaveKey = document.getElementById('btn-save-key');

// Load API key from local storage on init
let GROQ_API_KEY = localStorage.getItem('neoEngineGroqApiKey') || "";
if (GROQ_API_KEY) {
    apiKeyInput.value = GROQ_API_KEY;
}

btnSaveKey.addEventListener('click', () => {
    GROQ_API_KEY = apiKeyInput.value.trim();
    if (GROQ_API_KEY) {
        localStorage.setItem('neoEngineGroqApiKey', GROQ_API_KEY);
        btnSaveKey.textContent = "Saved!";
        setTimeout(() => btnSaveKey.textContent = "Save", 2000);
    }
});

function addAIMessage(text, sender = 'ai') {
    const msgDiv = document.createElement('div');
    msgDiv.className = `ai-message ${sender}`;
    
    // Simple markdown parsing for the response
    if (sender === 'ai') {
        let formattedText = text
            .replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>')
            .replace(/`(.*?)`/g, '<code>$1</code>')
            .replace(/\n/g, '<br>');
        msgDiv.innerHTML = formattedText;
    } else {
        msgDiv.textContent = text;
    }

    aiMessages.appendChild(msgDiv);
    aiMessages.scrollTop = aiMessages.scrollHeight;
}

btnAnalyze.addEventListener('click', async () => {
    if (!GROQ_API_KEY) {
        addAIMessage("Error: Please enter and save your Groq API Key first.", "system");
        return;
    }

    if (!connectionDot.classList.contains('connected')) {
        addAIMessage("Error: Not connected to NeoEngine.", "system");
        return;
    }

    btnAnalyze.disabled = true;
    btnAnalyze.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="18" height="18"><circle cx="12" cy="12" r="10"></circle><path d="M12 6v6l4 2"></path></svg> Analyzing...';

    // Collect current metrics
    const currentFPS = valFps.textContent;
    const currentDT = valDt.textContent;
    const currentDrawCalls = valDrawcalls.textContent;
    const currentParticles = valParticles.textContent;
    const currentCpu = valCpu.textContent;
    const currentGpu = valGpu.textContent;
    const currentRam = valRam.textContent;
    const currentVram = valVram.textContent;
    const currentLights = valLights.textContent;

    addAIMessage(`Requested analysis with current metrics:\nFPS: ${currentFPS}, Frame Time: ${currentDT}ms, Draw Calls: ${currentDrawCalls}, Particles: ${currentParticles}\nCPU: ${currentCpu}ms, GPU: ${currentGpu}ms, RAM: ${currentRam}MB, VRAM: ${currentVram}MB, Lights: ${currentLights}`, "user");

    const prompt = `あなたは優秀なゲームエンジンのプロファイラ・アシスタントです。現在のエンジンのパフォーマンス指標を分析し、改善点や問題点を日本語で短く簡潔に指摘してください。
【現在の指標】
- FPS: ${currentFPS}
- Frame Time: ${currentDT} ms
- Draw Calls: ${currentDrawCalls}
- Particles: ${currentParticles}
- CPU Logic Time: ${currentCpu} ms
- GPU Render Time: ${currentGpu} ms
- System RAM: ${currentRam} MB
- Video RAM: ${currentVram} MB
- Lights: ${currentLights}

問題がなければ「パフォーマンスは良好です」と伝えてください。各指標に対して異常に多い場合や遅延がある場合はその最適化を提案してください。`;

    try {
        const response = await fetch("https://api.groq.com/openai/v1/chat/completions", {
            method: "POST",
            headers: {
                "Authorization": `Bearer ${GROQ_API_KEY}`,
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                model: "llama-3.3-70b-versatile", // Using LLaMA 3.3 70B via Groq
                messages: [
                    { role: "system", content: "You are a helpful game engine performance analyzer. Always answer in Japanese." },
                    { role: "user", content: prompt }
                ],
                temperature: 0.5,
                max_tokens: 500
            })
        });

        if (!response.ok) {
            throw new Error(`API Error: ${response.status}`);
        }

        const data = await response.json();
        const aiReply = data.choices[0].message.content;
        addAIMessage(aiReply, "ai");

    } catch (error) {
        console.error(error);
        addAIMessage(`通信エラーが発生しました: ${error.message}`, "system");
    } finally {
        btnAnalyze.disabled = false;
        btnAnalyze.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="18" height="18"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"></polyline></svg> Analyze Performance';
    }
});

// ==========================================
// --- Asset & Memory Audit Logic -----------
// ==========================================
let currentAssets = [];

async function fetchAssets() {
    try {
        const response = await fetch(`${BASE_URL}/assets`);
        if (response.ok) {
            currentAssets = await response.json();
            renderAssetTable(currentAssetType, document.getElementById('asset-search')?.value || '');
            updateAssetChartData();
            updateAssetTotalStats();
        }
    } catch(e) {}
}

function updateAssetTotalStats() {
    let totalMemory = 0;
    let texMemory = 0;
    let meshMemory = 0;
    currentAssets.forEach(a => {
        totalMemory += a.size;
        if (a.type === 'Texture') texMemory += a.size;
        if (a.type === 'Mesh') meshMemory += a.size;
    });

    const elCount = document.getElementById('val-asset-count');
    if (elCount) elCount.textContent = currentAssets.length;

    const elTotal = document.getElementById('val-asset-total-mem');
    if (elTotal) elTotal.innerHTML = `${totalMemory.toFixed(1)} <span class="unit">MB</span>`;

    const elTex = document.getElementById('val-texture-mem');
    if (elTex) elTex.innerHTML = `${texMemory.toFixed(1)} <span class="unit">MB</span>`;

    const elMesh = document.getElementById('val-mesh-mem');
    if (elMesh) elMesh.innerHTML = `${meshMemory.toFixed(1)} <span class="unit">MB</span>`;
}

function updateAssetChartData() {
    if (!assetChartInstance) return;
    
    let tex = 0, mesh = 0, shader = 0, audio = 0;
    currentAssets.forEach(a => {
        if (a.type === 'Texture') tex += a.size;
        else if (a.type === 'Mesh') mesh += a.size;
        else if (a.type === 'Audio') audio += a.size;
        else shader += a.size; // fallback for others
    });

    assetChartInstance.data.datasets[0].data = [tex, mesh, shader, audio];
    assetChartInstance.update();
}

let assetChartInstance = null;

function initAssetChart() {
    const ctx = document.getElementById('assetMemoryChart')?.getContext('2d');
    if (!ctx) return;

    if (assetChartInstance) assetChartInstance.destroy();

    assetChartInstance = new Chart(ctx, {
        type: 'doughnut',
        data: {
            labels: ['Textures (VRAM)', 'Meshes / Geometry', 'Shaders & DXIL', 'Audio Buffers'],
            datasets: [{
                data: [0, 0, 0, 0], // Updated dynamically
                backgroundColor: ['#38bdf8', '#10b981', '#c084fc', '#fbbf24'],
                borderWidth: 0
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'bottom',
                    labels: { color: '#94a3b8', font: { family: 'Outfit', size: 11 } }
                }
            },
            cutout: '70%'
        }
    });
}

function renderAssetTable(filterType = 'all', searchQuery = '') {
    const tbody = document.getElementById('asset-table-body');
    if (!tbody) return;

    tbody.innerHTML = '';

    const query = searchQuery.toLowerCase();
    const filtered = currentAssets.filter(item => {
        const matchesType = (filterType === 'all' || item.type === filterType);
        const matchesSearch = item.name.toLowerCase().includes(query) || item.details.toLowerCase().includes(query);
        return matchesType && matchesSearch;
    });

    filtered.forEach(asset => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td style="font-family:monospace; font-weight:600; color:#f8fafc;">${asset.name}</td>
            <td><span class="asset-type-badge ${asset.type}">${asset.type}</span></td>
            <td style="font-family:monospace; color:var(--accent); font-weight:600;">${asset.sizeStr}</td>
            <td style="color:var(--text-muted); font-size:0.8rem;">${asset.details}</td>
            <td style="text-align:center; font-weight:600;">${asset.refCount}</td>
            <td><span class="status-badge ${asset.status}">${asset.status}</span></td>
        `;
        tbody.appendChild(tr);
    });
}

// Asset Audit Filter Listeners
let currentAssetType = 'all';
document.querySelectorAll('#asset-type-filter .btn-chip').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('#asset-type-filter .btn-chip').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentAssetType = btn.getAttribute('data-type');
        const query = document.getElementById('asset-search')?.value || '';
        renderAssetTable(currentAssetType, query);
    });
});

document.getElementById('asset-search')?.addEventListener('input', (e) => {
    renderAssetTable(currentAssetType, e.target.value);
});


// Initial Call on Load
window.addEventListener('DOMContentLoaded', () => {
    initAssetChart();
    renderAssetTable();
    fetchAssets();
    setInterval(fetchAssets, 1500); // Polling every 1.5s
});

// Re-draw canvases when switching tabs
document.querySelectorAll('.tab-btn').forEach(tab => {
    tab.addEventListener('click', () => {
        const target = tab.getAttribute('data-tab');
        if (target === 'asset-audit') {
            setTimeout(initAssetChart, 50);
        }
    });
});





