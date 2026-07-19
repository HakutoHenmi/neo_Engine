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

// Parameters DOM
const ambR = document.getElementById('amb-r');
const ambG = document.getElementById('amb-g');
const ambB = document.getElementById('amb-b');
const ambRVal = document.getElementById('amb-r-val');
const ambGVal = document.getElementById('amb-g-val');
const ambBVal = document.getElementById('amb-b-val');
const playerSpeed = document.getElementById('player-speed');
const playerSpeedVal = document.getElementById('player-speed-val');

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
        
        if(data.AmbientColor) {
            ambR.value = data.AmbientColor.R; ambRVal.textContent = data.AmbientColor.R.toFixed(2);
            ambG.value = data.AmbientColor.G; ambGVal.textContent = data.AmbientColor.G.toFixed(2);
            ambB.value = data.AmbientColor.B; ambBVal.textContent = data.AmbientColor.B.toFixed(2);
        }
        if(data.Player) {
            playerSpeed.value = data.Player.Speed; playerSpeedVal.textContent = data.Player.Speed.toFixed(1);
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
    inputEl.addEventListener('input', (e) => {
        valEl.textContent = formatFn(e.target.value);
        sendParameter(target, prop, e.target.value);
    });
}

setupParamControl(ambR, ambRVal, "AmbientColor", "R", v => parseFloat(v).toFixed(2));
setupParamControl(ambG, ambGVal, "AmbientColor", "G", v => parseFloat(v).toFixed(2));
setupParamControl(ambB, ambBVal, "AmbientColor", "B", v => parseFloat(v).toFixed(2));
setupParamControl(playerSpeed, playerSpeedVal, "Player", "Speed", v => parseFloat(v).toFixed(1));


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

// --- Hierarchy & Inspector Logic ---
const hierarchyList = document.getElementById('hierarchy-list');
const inspectorPanels = document.querySelectorAll('.inspector-panel');

hierarchyList.addEventListener('click', (e) => {
    const li = e.target.closest('li');
    if (!li) return;

    // Remove selected class from all
    hierarchyList.querySelectorAll('li').forEach(el => el.classList.remove('selected'));
    
    // Add selected class to clicked item
    li.classList.add('selected');

    // Get entity name
    const entityName = li.getAttribute('data-entity');
    
    // Hide all inspector panels
    inspectorPanels.forEach(panel => panel.style.display = 'none');

    // Show appropriate inspector panel
    const targetPanel = document.getElementById(`inspector-${entityName}`);
    if (targetPanel) {
        targetPanel.style.display = 'block';
    } else if (entityName) {
        // Fallback for generic entities (like meshes)
        const genericPanel = document.getElementById('inspector-Generic');
        document.getElementById('generic-inspector-title').textContent = entityName;
        genericPanel.style.display = 'block';
    } else {
        document.getElementById('inspector-empty').style.display = 'block';
    }

    // Log selection to console
    engineLog(`Selected Entity: ${entityName}`);
});

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
