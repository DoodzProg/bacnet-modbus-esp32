/**
 * @file app.js
 * @brief Front-end logic for the BMS Gateway dashboard.
 * @details Handles API polling, dynamic DOM updates, network configuration 
 * modal logic, and UI state management (themes, clipboards).
 * @author [Your Name / Project Name]
 * @date 2024
 */

// ==============================================================================
// GLOBAL VARIABLES
// ==============================================================================

/** @type {Object} Holds the current system and network configuration state */
let sysData = {};

// ==============================================================================
// SYSTEM INIT & POLLING
// ==============================================================================

/**
 * @brief Initializes the dashboard by fetching system network parameters.
 * @details Modifies the DOM to display current IPs and adapts the Network 
 * Configuration button based on the active Wi-Fi mode (AP or STA).
 */
function loadSystemInfo() {
    fetch('/api/system')
        .then(response => response.json())
        .then(data => {
            sysData = data;
            document.getElementById('clientIpSpan').innerText = data.clientIP;
            document.getElementById('espIpSpan').innerText = data.espIP;
            
            const btn = document.getElementById('networkBtn');
            const btnText = document.getElementById('networkBtnText');
            btn.style.display = 'flex'; 
            
            if(data.isAP) {
                btnText.innerText = "Connect to Enterprise Wi-Fi";
            } else {
                btnText.innerText = "Isolate Device (Local)";
            }
        })
        .catch(err => console.error("Error fetching system info:", err));
}

// ==============================================================================
// NETWORK CONFIGURATION MODAL
// ==============================================================================

/**
 * @brief Opens and populates the Network Configuration Modal.
 * @details The content dynamically changes depending on whether the ESP32 
 * is currently acting as an Access Point (AP) or is connected to a Station (STA).
 */
function openModal() {
    const title = document.getElementById('modalTitle');
    const body = document.getElementById('modalBody');
    const confirmBtn = document.getElementById('modalConfirmBtn');
    
    if (sysData.isAP) {
        title.innerText = "Wi-Fi Network Connection";
        body.innerHTML = `The device will attempt to connect to the preconfigured Wi-Fi network: <b>${sysData.staSSID}</b>.
        
        <div style="margin-top:15px; background:var(--bg-body); padding:15px; border:1px solid var(--border-color); border-radius:4px;">
            <div style="font-size:11px; font-weight:bold; margin-bottom:5px;">CHANGE WI-FI NETWORK :</div>
            <div class="input-row" style="margin-top:0;">
                <select id="wifiSelect" class="input-field">
                    <option value="${sysData.staSSID}">${sysData.staSSID} (Current)</option>
                </select>
                <button class="btn btn-action" onclick="scanWifi(this)">Scan</button>
            </div>
            <div style="font-size:11px; font-weight:bold; margin-top:15px; margin-bottom:5px;">PASSWORD :</div>
            <input type="password" id="wifiPass" class="input-field" placeholder="*******" value="${sysData.staPASS || ''}">
        </div>

        <span class="warning-text">⚠️ Warning: You will be disconnected from the ESP32's local Wi-Fi.<br><br>Do you wish to proceed?</span>`;
        confirmBtn.onclick = () => saveAndSwitch('wifi');
    } else {
        title.innerText = "Switch to Local Network";
        body.innerHTML = `You are about to disconnect the device from the current network to force it onto its own exclusive local Wi-Fi.
        
        <div style="margin-top:15px; background:var(--bg-body); padding:15px; border:1px solid var(--border-color); border-radius:4px;">
            <div style="font-size:11px; font-weight:bold; margin-bottom:5px;">SSID (ESP32 WIFI NAME) :</div>
            <input type="text" id="apSsidInput" class="input-field" value="${sysData.apSSID}">
            <div style="font-size:11px; font-weight:bold; margin-top:15px; margin-bottom:5px;">PASSWORD :</div>
            <input type="text" id="apPassInput" class="input-field" value="${sysData.apPASS}">
        </div>

        <span class="warning-text">⚠️ You will need to reconnect manually to this access point and will lose your current Internet connection.</span>`;
        confirmBtn.onclick = () => saveAndSwitch('local');
    }
    document.getElementById('netModal').style.display = 'flex';
}

/**
 * @brief Triggers an asynchronous Wi-Fi scan on the ESP32.
 * @param {HTMLElement} btn - The button element that triggered the scan, used for state feedback.
 */
function scanWifi(btn) {
    btn.innerText = "Scanning...";
    const sel = document.getElementById('wifiSelect');
    fetch('/api/scan')
        .then(r => r.json())
        .then(nets => {
            sel.innerHTML = "";
            nets.forEach(n => {
                let opt = document.createElement('option');
                opt.value = n.ssid; 
                opt.text = `${n.ssid} (${n.rssi}dBm)`;
                sel.add(opt);
            });
            btn.innerText = "Scan";
        })
        .catch(err => {
            btn.innerText = "Error";
            console.error("Scan failed:", err);
        });
}

/**
 * @brief Closes the active Network Configuration Modal.
 */
function closeModal() {
    document.getElementById('netModal').style.display = 'none';
}

/**
 * @brief Submits new network credentials and triggers an ESP32 reboot.
 * @param {string} mode - 'wifi' for standard STA mode, 'local' for AP isolation mode.
 */
function saveAndSwitch(mode) {
    let url = `/api/switch_network?mode=${mode}`;
    
    if (mode === 'wifi') {
        url += `&ssid=${encodeURIComponent(document.getElementById('wifiSelect').value)}`;
        url += `&pass=${encodeURIComponent(document.getElementById('wifiPass').value)}`;
    } else {
        url += `&ap_ssid=${encodeURIComponent(document.getElementById('apSsidInput').value)}`;
        url += `&ap_pass=${encodeURIComponent(document.getElementById('apPassInput').value)}`;
    }

    document.getElementById('modalBody').innerHTML = "<br><br><center><b>Rebooting equipment...</b><br><br>Please wait and change Wi-Fi network if necessary.</center><br><br>";
    document.getElementById('modalActions').style.display = 'none';
    
    fetch(url).then(() => {
        setTimeout(() => { window.location.reload(); }, 12000);
    }).catch(() => {
        setTimeout(() => { window.location.reload(); }, 12000);
    });
}

// ==============================================================================
// UI HELPERS (CLIPBOARD & THEMES)
// ==============================================================================

/**
 * @brief Fallback clipboard copy mechanism for non-HTTPS environments (ESP32).
 * @param {string} textToCopy - The raw string to copy.
 * @param {HTMLElement} el - The clicked element to animate.
 * @param {string} originalHtml - The original inner HTML to restore.
 */
function forceCopy(textToCopy, el, originalHtml) {
    const textArea = document.createElement("textarea");
    textArea.value = textToCopy;
    textArea.style.position = "fixed"; // Avoid scrolling to bottom
    textArea.style.opacity = "0";
    document.body.appendChild(textArea);
    textArea.select();
    
    try {
        // execCommand is deprecated but essential for HTTP local fallbacks
        document.execCommand('copy');
        animateCopySuccess(el, originalHtml);
    } catch (err) {
        console.error("Copy failed", err);
    }
    document.body.removeChild(textArea);
}

/**
 * @brief Animates the element to confirm a successful copy to clipboard.
 */
function animateCopySuccess(el, originalHtml) {
    el.innerHTML = "<b>COPIED!</b>"; 
    el.style.color = "var(--color-run)"; 
    el.style.borderColor = "var(--color-run)";
    el.style.justifyContent = "center";
    setTimeout(() => { 
        el.innerHTML = originalHtml; 
        el.style.color = ""; 
        el.style.borderColor = ""; 
        el.style.justifyContent = "space-between";
    }, 1500);
}

/**
 * @brief Triggered when a protocol port is clicked.
 */
function copyPort(el, portToCopy) {
    forceCopy(portToCopy, el, el.innerHTML);
}

/**
 * @brief Triggered when an IP address is clicked.
 */
function copyIp(el, spanId) {
    const ipToCopy = document.getElementById(spanId).innerText;
    if(ipToCopy === "...") return; 
    forceCopy(ipToCopy, el, el.innerHTML);
}

/** Theme SVGs */
const sunSvg = `<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" stroke-width="2" fill="none"><circle cx="12" cy="12" r="5"></circle><line x1="12" y1="1" x2="12" y2="3"></line><line x1="12" y1="21" x2="12" y2="23"></line><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line><line x1="1" y1="12" x2="3" y2="12"></line><line x1="21" y1="12" x2="23" y2="12"></line><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line></svg>`;
const moonSvg = `<svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" stroke-width="2" fill="none"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path></svg>`;

/**
 * @brief Toggles the CSS data-theme attribute between light and dark.
 * @param {boolean} isDark - Target theme state.
 */
function applyTheme(isDark) { 
    htmlEl.setAttribute('data-theme', isDark ? 'dark' : 'light'); 
    themeBtn.innerHTML = isDark ? sunSvg : moonSvg; 
}

const htmlEl = document.documentElement;
const themeBtn = document.getElementById('themeToggle');
applyTheme(window.matchMedia("(prefers-color-scheme: dark)").matches);
themeBtn.addEventListener('click', () => applyTheme(htmlEl.getAttribute('data-theme') === 'light'));

// ==============================================================================
// DOM RENDERING
// ==============================================================================

/**
 * @brief Generates the HTML string for a single telemetry card.
 * @param {string} name - The physical point name (e.g., 'FanSpeed').
 * @param {string|number|boolean} val - The current value.
 * @param {boolean} isBin - True if the point is Binary (ON/OFF), false if Analog.
 * @return {string} The constructed HTML block.
 */
function createCard(name, val, isBin) {
    let wrp = "", ext = "", isTemp = name.toLowerCase().includes('temp');
    if (isBin) {
        wrp = `<div class="value-text"><div class="indicator-square ${val ? 'state-run' : 'state-stop'}"></div>${val ? 'RUN' : 'STOP'}</div>`;
    } else {
        let fVal = parseFloat(val).toFixed(1);
        if (isTemp) {
            let pct = Math.max(0, Math.min(100, (fVal / 50) * 100));
            wrp = `<div class="value-text">${fVal}<span class="unit">°C</span></div><div class="thermo-vertical"><div class="thermo-tube-v"><div class="thermo-fill-v" style="height: ${pct}%;"></div></div><div class="thermo-bulb-v"></div></div>`;
        } else {
            let pct = Math.max(0, Math.min(100, fVal));
            wrp = `<div class="value-text">${fVal}<span class="unit">%</span></div>`;
            ext = `<div class="gauge-container"><div class="gauge-fill" style="width: ${pct}%;"></div></div>`;
        }
    }
    return `<div class="card"><div class="card-header"><div class="card-label">${name}</div><div class="badge-ro">MONITOR</div></div><div class="value-display"><div class="value-wrapper">${wrp}</div>${ext}</div></div>`;
}

/**
 * @brief Rebuilds the dashboard DOM with fresh JSON data.
 * @param {Object} data - Parsed JSON object from /api/data.
 */
function renderDashboard(data) {
    // 1. Update Hardware RAM stats
    if (data.sys) {
        const ramKb = (data.sys.ram_used / 1024).toFixed(0);
        const totalKb = (data.sys.ram_total / 1024).toFixed(0);
        document.getElementById('ramBadge').innerText = `RAM: ${data.sys.ram_pct}% (${ramKb}/${totalKb} KB)`;
    }

    // 2. Update Telemetry Cards
    let html = "";
    data.binary.forEach(pt => html += createCard(pt.name, pt.value, true));
    data.analog.forEach(pt => html += createCard(pt.name, pt.value, false));
    document.getElementById("dashboard").innerHTML = html;
}

// ==============================================================================
// MAIN EXECUTION
// ==============================================================================

loadSystemInfo();

setInterval(() => {
    fetch('/api/data')
        .then(r => r.json())
        .then(data => renderDashboard(data))
        .catch(() => {});
}, 1000);