/**
 * @file        app.js
 * @brief       Front-end SPA logic for the BMS Gateway dashboard.
 *
 * @details     Single entry-point JavaScript module.  Responsibilities:
 *              - Bootstrap: theme detection, initial API calls, polling loop.
 *              - Telemetry rendering: live values, gauges, thermometers.
 *              - Section management: create, rename, delete, reorder (D&D).
 *              - Point management: add, edit, delete, reorder (D&D).
 *              - Point / section modals: validation, address-conflict check.
 *              - Network modal: explicit STA (join) / AP (isolate) mode
 *                selection, Wi-Fi scan, apply + reboot flow.
 *              - Theme toggle: light / dark with OS preference detection.
 *              - Clipboard helpers: click-to-copy for IPs and ports.
 *              - Front-end value cache: last known values are stored in
 *                sessionStorage so a page reload (e.g. after reboot) can
 *                restore readouts immediately while the first poll completes.
 *
 * @note        API CONTRACT — do NOT modify any fetch() calls, URLs, or
 *              request / response shapes.  All changes must remain purely
 *              front-end (rendering, UI state, styles).
 *
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

'use strict';

// ==============================================================================
// MODULE-LEVEL STATE
// ==============================================================================

/** @type {Object} Last-received /api/system payload. */
let sysData = {};

/**
 * @type {{ binary: Object[], analog: Object[], sections: Object[] }}
 * In-memory mirror of the backend configuration.
 */
let cfg = { binary: [], analog: [], sections: [] };

/**
 * @type {Object.<string, {value: *, writable: boolean}>}
 * Latest polled values keyed by point name.
 */
let liveValues = {};

/** @type {boolean} Whether edit mode is currently active. */
let editMode = false;

/** @type {Sortable|null} SortableJS instance for the sections root. */
let sectionSortable = null;

/** @type {Sortable[]} SortableJS instances for each points grid. */
let pointSortables = [];

/** @type {string|null} Point name being edited; null when adding. */
let editingPointName = null;

/** @type {string|null} Section id being edited; null when adding. */
let editingSectionId = null;

/** @type {number} Selected column count in the section modal. */
let selectedCols = 4;

/** @type {number|null} Timer id for debounced address-conflict check. */
let addrCheckTimer = null;

// ==============================================================================
// CONSTANTS — SVG ICONS
// These inline SVGs replace all emoji usage throughout the UI.
// ============================================================================== */

/** Gear / wrench configuration icon. */
const ICON_CONFIG = `<svg viewBox="0 0 24 24" width="11" height="11" stroke="currentColor"
    stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round">
    <circle cx="12" cy="12" r="3"/>
    <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65
    1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9
    19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0
    4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65
    0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65
    1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0
    1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0
    1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>
</svg>`;

/** Trash-bin delete icon. */
const ICON_DELETE = `<svg viewBox="0 0 24 24" width="11" height="11" stroke="currentColor"
    stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round">
    <polyline points="3 6 5 6 21 6"/>
    <path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"/>
    <path d="M10 11v6M14 11v6"/>
    <path d="M9 6V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2"/>
</svg>`;

/** Play / run arrow icon used in the binary write button. */
const ICON_PLAY = `<svg viewBox="0 0 24 24" width="11" height="11" stroke="currentColor"
    stroke-width="2" fill="currentColor" stroke-linecap="round">
    <polygon points="5 3 19 12 5 21 5 3"/>
</svg>`;

/** Stop square icon used in the binary write button. */
const ICON_STOP = `<svg viewBox="0 0 24 24" width="11" height="11" stroke="currentColor"
    stroke-width="2" fill="currentColor" stroke-linecap="round">
    <rect x="3" y="3" width="18" height="18" rx="2"/>
</svg>`;

/** Warning triangle icon used in network modal and error notices. */
const ICON_WARN = `<svg viewBox="0 0 24 24" width="13" height="13" stroke="currentColor"
    stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round">
    <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2
    2 0 0 0-3.42 0z"/>
    <line x1="12" y1="9" x2="12" y2="13"/>
    <line x1="12" y1="17" x2="12.01" y2="17"/>
</svg>`;

/** Sun icon for light-mode toggle. */
const SUN_SVG = `<svg viewBox="0 0 24 24" width="15" height="15" stroke="currentColor"
    stroke-width="2" fill="none">
    <circle cx="12" cy="12" r="5"/>
    <line x1="12" y1="1"  x2="12" y2="3"/>
    <line x1="12" y1="21" x2="12" y2="23"/>
    <line x1="4.22"  y1="4.22"  x2="5.64"  y2="5.64"/>
    <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/>
    <line x1="1"  y1="12" x2="3"  y2="12"/>
    <line x1="21" y1="12" x2="23" y2="12"/>
    <line x1="4.22"  y1="19.78" x2="5.64"  y2="18.36"/>
    <line x1="18.36" y1="5.64"  x2="19.78" y2="4.22"/>
</svg>`;

/** Moon icon for dark-mode toggle. */
const MOON_SVG = `<svg viewBox="0 0 24 24" width="15" height="15" stroke="currentColor"
    stroke-width="2" fill="none">
    <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>
</svg>`;

// ==============================================================================
// SESSION-STORAGE VALUE CACHE
// Restores last-known values immediately on reload (e.g. post-reboot).
// Keys are prefixed to avoid collisions.
// ==============================================================================

/** Namespace prefix for all sessionStorage keys used by this module. */
const SS_PREFIX = 'bms_lv_';

/**
 * @brief Persist a single live-value entry to sessionStorage.
 * @param {string} name   - Point name.
 * @param {*}      value  - Current value (number or boolean).
 */
function cacheLiveValue(name, value) {
    try {
        sessionStorage.setItem(SS_PREFIX + name, JSON.stringify(value));
    } catch (_) { /* sessionStorage may be unavailable in some contexts */ }
}

/**
 * @brief Restore all cached live values into the liveValues object.
 *        Called once at startup so the UI is not blank while the first
 *        poll is in flight.
 */
function restoreCachedValues() {
    try {
        for (let i = 0; i < sessionStorage.length; i++) {
            const key = sessionStorage.key(i);
            if (!key || !key.startsWith(SS_PREFIX)) continue;
            const name = key.slice(SS_PREFIX.length);
            const raw  = sessionStorage.getItem(key);
            if (raw !== null) {
                // writable state is unknown until the first poll; set false.
                liveValues[name] = { value: JSON.parse(raw), writable: false };
            }
        }
    } catch (_) {}
}

// ==============================================================================
// BOOTSTRAP
// ==============================================================================

document.addEventListener('DOMContentLoaded', () => {
    // Detect OS colour scheme preference and apply it immediately.
    applyTheme(window.matchMedia('(prefers-color-scheme: dark)').matches);

    document.getElementById('themeToggle').addEventListener('click', () => {
        applyTheme(document.documentElement.getAttribute('data-theme') === 'light');
    });

    // Pre-populate with any cached values so the UI is not blank on reload.
    restoreCachedValues();

    loadSystemInfo();
    loadConfig();

    // Start the 1-second telemetry polling loop.
    setInterval(pollData, 1000);
});

// ==============================================================================
// API CALLS
// NOTE: Do NOT modify these fetch() calls, URLs, or payload shapes.
// ==============================================================================

/**
 * @brief Fetches system information and populates the top bar.
 */
function loadSystemInfo() {
    fetch('/api/system')
        .then(r => r.json())
        .then(data => {
            sysData = data;
            document.getElementById('clientIpSpan').innerText = data.clientIP;
            document.getElementById('espIpSpan').innerText    = data.espIP;

            const btn     = document.getElementById('networkBtn');
            const btnText = document.getElementById('networkBtnText');
            btn.style.display = 'flex';
            // Label reflects current mode so the action is self-explanatory.
            btnText.innerText = data.isAP
                ? 'Connect to Enterprise Wi-Fi'
                : 'Isolate Device (Local AP)';
        })
        .catch(err => console.error('[SYS] Failed to fetch system info:', err));
}

/**
 * @brief Fetches the full point + section configuration and triggers a
 *        full re-render of the dashboard.
 */
function loadConfig() {
    fetch('/api/config')
        .then(r => r.json())
        .then(data => {
            cfg = data;
            renderSections();
            if (editMode) attachSortables();
        })
        .catch(err => console.error('[CFG] Failed to fetch config:', err));
}

/**
 * @brief Polls /api/data every second.  Updates the RAM badge and
 *        refreshes all live-value readouts.
 */
function pollData() {
    fetch('/api/data')
        .then(r => r.json())
        .then(data => {
            // Update RAM usage indicator in the top bar.
            if (data.sys) {
                const ramKb   = (data.sys.ram_used  / 1024).toFixed(0);
                const totalKb = (data.sys.ram_total / 1024).toFixed(0);
                const el      = document.getElementById('ramBadge');
                if (el) {
                    el.innerHTML = `
                        <svg viewBox="0 0 24 24" width="10" height="10"
                             stroke="currentColor" stroke-width="2" fill="none">
                            <rect x="2" y="7" width="20" height="10" rx="1"/>
                            <path d="M7 7V5M12 7V5M17 7V5M7 17v2M12 17v2M17 17v2"/>
                        </svg>
                        RAM: ${data.sys.ram_pct}% (${ramKb}/${totalKb} KB)`;
                }
            }

            // Merge polled values into the live-value map and persist cache.
            liveValues = {};
            (data.binary || []).forEach(pt => {
                liveValues[pt.name] = { value: pt.value, writable: pt.writable };
                cacheLiveValue(pt.name, pt.value);
            });
            (data.analog || []).forEach(pt => {
                liveValues[pt.name] = { value: pt.value, writable: pt.writable };
                cacheLiveValue(pt.name, pt.value);
            });

            renderValues();
        })
        .catch(() => {
            // Silent failure — the ONLINE badge still blinks; no alert.
        });
}

/**
 * @brief Persists the current DOM order of sections and points to the
 *        backend.  Called after every drag-and-drop event.
 *
 * @note  Reads layout strictly from the DOM post-drag so that
 *        cross-section moves are captured correctly.
 */
async function saveLayout() {
    const sectionEls = document.querySelectorAll('#sectionsRoot .section-card');
    const newSections = [];

    sectionEls.forEach(sectionEl => {
        const id       = sectionEl.dataset.sectionId;
        const existing = cfg.sections.find(s => s.id === id);
        if (!existing) return;

        // Read all point names currently residing in this section's DOM grid.
        const pointEls = sectionEl.querySelectorAll(
            '.points-grid > .card[data-point-name]'
        );
        const points = Array.from(pointEls).map(c => c.dataset.pointName);

        newSections.push({
            id:        existing.id,
            label:     existing.label,
            widthCols: existing.widthCols,
            points
        });
    });

    // Keep in-memory config in sync to prevent stale re-renders.
    cfg.sections = newSections;

    await fetch('/api/config/layout', {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify(newSections)
    }).catch(err => console.error('[LAYOUT] Save failed:', err));
}

// ==============================================================================
// RENDERING
// ==============================================================================

/**
 * @brief Rebuilds the entire sections/points DOM from the in-memory cfg.
 */
function renderSections() {
    const root = document.getElementById('sectionsRoot');
    root.innerHTML = '';

    cfg.sections.forEach(section => {
        root.appendChild(buildSectionEl(section));
    });

    renderValues();
}

/**
 * @brief Constructs the DOM element for one section card.
 *
 * @param {Object} section - Section descriptor from cfg.sections.
 * @returns {HTMLElement}
 */
function buildSectionEl(section) {
    const el = document.createElement('div');
    el.className    = `section-card section-cols-${section.widthCols || 4}`;
    el.dataset.sectionId = section.id;

    el.innerHTML = `
        <div class="section-header">
            <div class="section-label" title="${escAttr(section.label)}">${escHtml(section.label)}</div>
            <div class="section-actions">
                <div class="section-drag-handle" title="Drag to reorder section">
                    <div class="drag-handle-icon">
                        <span></span><span></span><span></span>
                    </div>
                </div>
                <button class="btn-section-settings"
                        onclick="openSectionModal('${escAttr(section.id)}')">
                    ${ICON_CONFIG} Settings
                </button>
            </div>
        </div>
        <div class="points-grid" data-section-id="${escAttr(section.id)}"></div>
    `;

    const grid = el.querySelector('.points-grid');

    section.points.forEach(pointName => {
        const binDef = cfg.binary.find(p => p.name === pointName);
        const anaDef = cfg.analog.find(p => p.name === pointName);
        if (binDef) grid.appendChild(buildCardEl(binDef, 'binary'));
        if (anaDef) grid.appendChild(buildCardEl(anaDef, 'analog'));
    });

    return el;
}

/**
 * @brief Constructs the DOM element for one point card.
 *
 * @param {Object} def  - Point definition from cfg.binary or cfg.analog.
 * @param {string} type - 'binary' or 'analog'.
 * @returns {HTMLElement}
 */
function buildCardEl(def, type) {
    const el = document.createElement('div');
    el.className        = 'card';
    el.dataset.pointName = def.name;
    el.dataset.pointType = type;

    // Build protocol badge markup.
    let protoBadges = '';
    if (def.protocol & 1) {
        const addr   = (type === 'binary') ? def.modbusCoil : def.modbusReg;
        const prefix = (type === 'binary') ? 'Coil:' : 'Reg:';
        protoBadges += `<span class="badge-proto badge-modbus"
            title="Modbus TCP">MB ${prefix}${addr}</span>`;
    }
    if (def.protocol & 2) {
        const prefix = (type === 'binary') ? 'BV:' : 'AV:';
        protoBadges += `<span class="badge-proto badge-bacnet"
            title="BACnet/IP">BN ${prefix}${def.bacnetInstance}</span>`;
    }

    const accessBadge = def.writable
        ? ''
        : '<span class="badge-ro">READ ONLY</span>';

    // Edit controls are placed at the card footer (hidden outside edit mode).
    // Config and Delete buttons use SVG icons — no emojis.
    el.innerHTML = `
        <div class="card-header">
            <div class="card-label" title="${escAttr(def.name)}">${escHtml(def.name)}</div>
            <div class="card-meta">
                ${protoBadges}
                ${accessBadge}
            </div>
        </div>
        <div class="value-display" id="vdisp-${escAttr(def.name)}">
            <div class="value-wrapper">
                <div class="value-text" id="vtext-${escAttr(def.name)}">—</div>
            </div>
        </div>
        <div class="write-control" id="wctrl-${escAttr(def.name)}"></div>
        <div class="edit-controls">
            <button class="btn-card-action config"
                    onclick="openEditPointModal('${escAttr(def.name)}')"
                    title="Configure point">
                ${ICON_CONFIG} Config
            </button>
            <button class="btn-card-action delete"
                    onclick="deletePoint('${escAttr(def.name)}')"
                    title="Delete point">
                ${ICON_DELETE} Delete
            </button>
        </div>
    `;

    return el;
}

/**
 * @brief Updates value displays and write controls for all live points.
 *        Called after every poll and after edit-mode transitions.
 */
function renderValues() {
    Object.entries(liveValues).forEach(([name, info]) => {
        const { value, writable } = info;

        const binDef = cfg.binary.find(p => p.name === name);
        const anaDef = cfg.analog.find(p => p.name === name);

        const vtextEl = document.getElementById(`vtext-${name}`);
        const wctrlEl = document.getElementById(`wctrl-${name}`);
        if (!vtextEl) return;

        if (binDef) {
            // Binary point: coloured square + RUN / STOP label.
            vtextEl.innerHTML = `
                <div class="indicator-square ${value ? 'state-run' : 'state-stop'}"></div>
                ${value ? 'RUN' : 'STOP'}`;

            if (wctrlEl && !editMode && writable) {
                // Reuse existing toggle button if present; update its state.
                if (!wctrlEl.querySelector('.write-toggle')) {
                    wctrlEl.innerHTML = `
                        <button class="write-toggle ${value ? 'is-run' : ''}"
                                onclick="writePoint('${escAttr(name)}', ${!value})">
                            ${value ? ICON_STOP + ' RUNNING — Click to STOP'
                                    : ICON_PLAY + ' STOPPED — Click to RUN'}
                        </button>`;
                } else {
                    const btn = wctrlEl.querySelector('.write-toggle');
                    btn.className = `write-toggle ${value ? 'is-run' : ''}`;
                    btn.innerHTML = value
                        ? ICON_STOP + ' RUNNING — Click to STOP'
                        : ICON_PLAY + ' STOPPED — Click to RUN';
                    btn.setAttribute('onclick',
                        `writePoint('${escAttr(name)}', ${!value})`);
                }
            } else if (wctrlEl) {
                wctrlEl.innerHTML = '';
            }

        } else if (anaDef) {
            // Analog point: numeric readout + optional gauge or thermometer.
            const fVal  = parseFloat(value).toFixed(1);
            const unit  = anaDef.unit || '';
            const isTemp = unit.includes('°') || name.toLowerCase().includes('temp');

            const vdispEl = document.getElementById(`vdisp-${name}`);
            if (!vdispEl) return;

            if (isTemp) {
                // Vertical thermometer visualisation.
                const pct = Math.max(0, Math.min(100, (parseFloat(fVal) / 50) * 100));
                vdispEl.innerHTML = `
                    <div class="value-wrapper">
                        <div class="value-text" id="vtext-${escAttr(name)}">
                            ${fVal}<span class="unit">${escHtml(unit)}</span>
                        </div>
                        <div class="thermo-vertical">
                            <div class="thermo-tube-v">
                                <div class="thermo-fill-v" style="height:${pct}%;"></div>
                            </div>
                            <div class="thermo-bulb-v"></div>
                        </div>
                    </div>`;
            } else {
                // Horizontal gauge bar visualisation.
                const pct = Math.max(0, Math.min(100, parseFloat(fVal)));
                vdispEl.innerHTML = `
                    <div class="value-wrapper">
                        <div class="value-text" id="vtext-${escAttr(name)}">
                            ${fVal}<span class="unit">${escHtml(unit)}</span>
                        </div>
                    </div>
                    <div class="gauge-container">
                        <div class="gauge-fill" style="width:${pct}%;"></div>
                    </div>`;
            }

            if (wctrlEl && !editMode && writable) {
                // Analog write control: range slider + number input.
                if (!wctrlEl.querySelector('.write-slider')) {
                    const min = 0;
                    const max = isTemp ? 50 : 100;
                    wctrlEl.innerHTML = `
                        <div class="write-analog-group"
                             style="display:flex;gap:8px;align-items:center;
                                    justify-content:center;margin-top:8px;">
                            <input type="range" class="write-slider" style="flex:1;"
                                   min="${min}" max="${max}" step="0.1" value="${fVal}"
                                   onchange="writePoint('${escAttr(name)}',
                                       parseFloat(this.value))"
                                   oninput="document.getElementById(
                                       'wnum-${escAttr(name)}').value=this.value">
                            <input type="number" id="wnum-${escAttr(name)}"
                                   class="input-field"
                                   style="width:64px;padding:4px 6px;
                                          text-align:center;font-weight:700;"
                                   min="${min}" max="${max}" step="0.1"
                                   value="${fVal}"
                                   onchange="writePoint('${escAttr(name)}',
                                       parseFloat(this.value))">
                        </div>`;
                } else {
                    // Update existing controls without rebuilding them (preserves
                    // focus state while the user is interacting).
                    const slider   = wctrlEl.querySelector('.write-slider');
                    const numInput = wctrlEl.querySelector('input[type="number"]');
                    if (
                        document.activeElement !== slider &&
                        document.activeElement !== numInput
                    ) {
                        slider.value   = fVal;
                        numInput.value = fVal;
                    }
                }
            } else if (wctrlEl) {
                wctrlEl.innerHTML = '';
            }
        }
    });
}

// ==============================================================================
// EDIT MODE
// ==============================================================================

/**
 * @brief Toggles edit mode on / off, attaches or destroys Sortable
 *        instances, and triggers a value re-render.
 */
function toggleEditMode() {
    editMode = !editMode;

    const btn  = document.getElementById('editToggleBtn');
    const txt  = document.getElementById('editToggleTxt');
    const tb   = document.getElementById('editToolbar');
    const root = document.getElementById('sectionsRoot');

    if (editMode) {
        btn.classList.add('active');
        txt.innerText = 'Exit Edit Mode';
        tb.classList.add('visible');
        root.classList.add('edit-active');
        attachSortables();
    } else {
        btn.classList.remove('active');
        txt.innerText = 'Edit Layout';
        tb.classList.remove('visible');
        root.classList.remove('edit-active');
        destroySortables();
        saveLayout();
    }

    renderValues();
}

/**
 * @brief Creates SortableJS instances for the sections root and for
 *        every points grid.  All point grids share the same group so
 *        cards can be dragged between sections.
 */
function attachSortables() {
    destroySortables();

    if (typeof Sortable === 'undefined') {
        console.warn('[SORT] SortableJS not yet loaded — retrying in 500 ms.');
        setTimeout(attachSortables, 500);
        return;
    }

    // Section-level sortable (drag by the handle only).
    sectionSortable = Sortable.create(document.getElementById('sectionsRoot'), {
        animation:        200,
        handle:           '.section-drag-handle',
        ghostClass:       'section-ghost',
        chosenClass:      'section-chosen',
        placeholderClass: 'section-placeholder',
        onEnd:            saveLayout
    });

    // Point-level sortables — cross-section drag enabled via shared group name.
    document.querySelectorAll('.points-grid').forEach(grid => {
        const ps = Sortable.create(grid, {
            group: {
                name: 'points',
                pull: true,
                put:  true
            },
            animation:        160,
            ghostClass:       'card-ghost',
            chosenClass:      'card-chosen',
            placeholderClass: 'card-placeholder',
            // Prevent drag initiation on interactive controls inside cards.
            filter:           '.btn-card-action, .write-toggle, .write-slider',
            preventOnFilter:  false,
            onEnd:            saveLayout
        });
        pointSortables.push(ps);
    });
}

/**
 * @brief Destroys all active SortableJS instances and clears the arrays.
 */
function destroySortables() {
    if (sectionSortable) {
        sectionSortable.destroy();
        sectionSortable = null;
    }
    pointSortables.forEach(s => s.destroy());
    pointSortables = [];
}

// ==============================================================================
// WRITE API
// ==============================================================================

/**
 * @brief Sends a write command for a point value to the backend.
 *
 * @param {string}          name  - Point name.
 * @param {number|boolean}  value - New value.
 */
function writePoint(name, value) {
    fetch('/api/point/write', {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ name, value })
    })
    .then(r => r.json())
    .then(res => {
        if (!res.ok) {
            console.error('[WRITE] Server rejected write:', res.error);
        }
    })
    .catch(err => console.error('[WRITE] Network error:', err));
}

// ==============================================================================
// POINT MODAL — ADD / EDIT
// ==============================================================================

/**
 * @brief Opens the point modal in "Add" mode with suggested next addresses.
 */
function openAddPointModal() {
    editingPointName = null;
    document.getElementById('pointModalTitle').innerText = 'ADD NEW POINT';
    document.getElementById('pmConfirmBtn').innerText    = 'Add Point';
    document.getElementById('pmName').value              = '';
    document.getElementById('pmType').value              = 'analog';
    document.getElementById('pmType').disabled           = false;
    document.getElementById('pmProtoModbus').checked     = true;
    document.getElementById('pmProtoBacnet').checked     = true;
    document.getElementById('pmWritable').checked        = true;
    document.getElementById('pmCoil').value              = suggestNextCoil();
    document.getElementById('pmReg').value               = suggestNextReg();
    document.getElementById('pmBacInst').value           = suggestNextBacInst('analog');
    document.getElementById('pmScale').value             = '1';
    document.getElementById('pmUnit').value              = '';
    document.getElementById('pmError').innerText         = '';
    document.getElementById('pmSectionGroup').style.display = '';

    // Populate section selector.
    const sel = document.getElementById('pmSection');
    sel.innerHTML = '';
    cfg.sections.forEach(s => {
        const opt  = document.createElement('option');
        opt.value  = s.id;
        opt.text   = s.label;
        sel.add(opt);
    });

    onPointTypeChange();
    clearAddrStatus();
    document.getElementById('pointModal').classList.add('open');
}

/**
 * @brief Opens the point modal in "Edit" mode pre-populated with the
 *        existing point definition.
 *
 * @param {string} name - Point name to edit.
 */
function openEditPointModal(name) {
    editingPointName = name;

    const binDef = cfg.binary.find(p => p.name === name);
    const anaDef = cfg.analog.find(p => p.name === name);
    const def    = binDef || anaDef;
    const type   = binDef ? 'binary' : 'analog';

    if (!def) return;

    document.getElementById('pointModalTitle').innerText = 'EDIT POINT — ' + name;
    document.getElementById('pmConfirmBtn').innerText    = 'Save Changes';
    document.getElementById('pmName').value              = def.name;
    document.getElementById('pmType').value              = type;
    document.getElementById('pmType').disabled           = true;
    document.getElementById('pmProtoModbus').checked     = !!(def.protocol & 1);
    document.getElementById('pmProtoBacnet').checked     = !!(def.protocol & 2);
    document.getElementById('pmWritable').checked        = def.writable;
    document.getElementById('pmBacInst').value           = def.bacnetInstance;
    document.getElementById('pmError').innerText         = '';
    document.getElementById('pmSectionGroup').style.display = 'none';

    if (type === 'binary') {
        document.getElementById('pmCoil').value = def.modbusCoil;
    } else {
        document.getElementById('pmReg').value   = def.modbusReg;
        document.getElementById('pmScale').value = def.modbusScale;
        document.getElementById('pmUnit').value  = def.unit || '';
    }

    onPointTypeChange();
    clearAddrStatus();
    document.getElementById('pointModal').classList.add('open');
}

/** @brief Closes the point configuration modal. */
function closePointModal() {
    document.getElementById('pointModal').classList.remove('open');
    editingPointName = null;
    clearTimeout(addrCheckTimer);
}

/**
 * @brief Adjusts form field visibility based on the selected point type
 *        and active protocols.  Called on type or protocol change.
 */
function onPointTypeChange() {
    const type      = document.getElementById('pmType').value;
    const hasModbus = document.getElementById('pmProtoModbus').checked;
    const hasBacnet = document.getElementById('pmProtoBacnet').checked;

    document.getElementById('pmModbusSection').style.display =
        hasModbus ? '' : 'none';
    document.getElementById('pmBacnetSection').style.display =
        hasBacnet ? '' : 'none';
    document.getElementById('pmCoilGroup').style.display =
        (type === 'binary' && hasModbus) ? '' : 'none';
    document.getElementById('pmRegGroup').style.display =
        (type === 'analog' && hasModbus) ? '' : 'none';
    document.getElementById('pmScaleGroup').style.display =
        (type === 'analog' && hasModbus) ? '' : 'none';
    document.getElementById('pmUnitGroup').style.display =
        (type === 'analog') ? '' : 'none';
}

/** @brief Proxy called when a protocol checkbox changes. */
function onProtoChange() { onPointTypeChange(); }

/**
 * @brief Debounces the address-conflict API call by 350 ms to avoid
 *        flooding the backend while the user is typing.
 */
function checkAddress() {
    clearTimeout(addrCheckTimer);
    addrCheckTimer = setTimeout(_doCheckAddress, 350);
}

/**
 * @brief Performs the actual /api/point/check request and updates the
 *        inline status indicators.
 */
async function _doCheckAddress() {
    const type   = document.getElementById('pmType').value;
    const params = new URLSearchParams({ type });

    if (editingPointName) params.append('excludeName', editingPointName);

    if (type === 'binary') {
        params.append('modbusCoil',     document.getElementById('pmCoil').value);
        params.append('bacnetInstance', document.getElementById('pmBacInst').value);
    } else {
        params.append('modbusReg',      document.getElementById('pmReg').value);
        params.append('bacnetInstance', document.getElementById('pmBacInst').value);
    }

    try {
        const r   = await fetch('/api/point/check?' + params.toString());
        const res = await r.json();

        _setAddrStatus('coilStatus',    res.modbusOk);
        _setAddrStatus('regStatus',     res.modbusOk);
        _setAddrStatus('bacInstStatus', res.bacnetOk);

        const pmError = document.getElementById('pmError');
        pmError.innerText = res.available
            ? ''
            : 'Address conflict detected — please choose different values.';
    } catch (_) { /* Non-critical — validation is also enforced server-side */ }
}

/**
 * @brief Sets the visual state of an address-status indicator.
 *
 * @param {string}  elementId - ID of the .addr-status element.
 * @param {boolean} ok        - Whether the address is available.
 */
function _setAddrStatus(elementId, ok) {
    const el = document.getElementById(elementId);
    if (!el) return;
    el.className  = 'addr-status ' + (ok ? 'ok' : 'error');
    el.textContent = ok ? '✓' : '✗';
}

/** @brief Clears all address-status indicators. */
function clearAddrStatus() {
    ['coilStatus', 'regStatus', 'bacInstStatus'].forEach(id => {
        const el = document.getElementById(id);
        if (el) { el.className = 'addr-status'; el.textContent = ''; }
    });
}

/**
 * @brief Displays a full-screen reboot overlay and reloads the page
 *        after a countdown (used when a protocol stack restart is needed).
 */
function showRebootOverlay() {
    const overlay       = document.createElement('div');
    overlay.className   = 'modal-overlay open';
    overlay.style.zIndex = '9999';
    overlay.innerHTML   = `
        <div class="modal-box reboot-overlay-box">
            <h2>Restarting Gateway</h2>
            <p>Applying changes to BACnet and Modbus stacks…</p>
            <p>Reloading interface in <strong id="rbCount">5</strong> s</p>
        </div>`;
    document.body.appendChild(overlay);

    let c = 5;
    const timer = setInterval(() => {
        c--;
        const el = document.getElementById('rbCount');
        if (el) el.innerText = c;
        if (c <= 0) {
            clearInterval(timer);
            window.location.reload();
        }
    }, 1000);
}

/**
 * @brief Validates the point modal form and submits to /api/point/add or
 *        /api/point/update depending on the current modal mode.
 */
async function submitPointModal() {
    const name  = document.getElementById('pmName').value.trim();
    const type  = document.getElementById('pmType').value;
    const errEl = document.getElementById('pmError');

    if (!name) {
        errEl.innerText = 'Point name is required.';
        return;
    }
    if (!/^[\w\-]+$/.test(name)) {
        errEl.innerText =
            'Name must contain only letters, digits, underscores, or hyphens.';
        return;
    }

    let protocol = 0;
    if (document.getElementById('pmProtoModbus').checked) protocol |= 1;
    if (document.getElementById('pmProtoBacnet').checked) protocol |= 2;
    if (protocol === 0) {
        errEl.innerText = 'At least one protocol must be selected.';
        return;
    }

    const payload = {
        name,
        type,
        protocol,
        writable:       document.getElementById('pmWritable').checked,
        bacnetInstance: parseInt(document.getElementById('pmBacInst').value, 10) || 0
    };

    if (type === 'binary') {
        payload.modbusCoil = parseInt(document.getElementById('pmCoil').value, 10) || 0;
    } else {
        payload.modbusReg   = parseInt(document.getElementById('pmReg').value,   10) || 0;
        payload.modbusScale = parseFloat(document.getElementById('pmScale').value)    || 1.0;
        payload.unit        = document.getElementById('pmUnit').value.trim();
    }

    const isEdit = editingPointName !== null;

    if (isEdit) {
        if (name !== editingPointName) payload.newName = name;
        payload.name = editingPointName;
    } else {
        payload.sectionId = document.getElementById('pmSection').value;
    }

    const endpoint = isEdit ? '/api/point/update' : '/api/point/add';

    try {
        const r   = await fetch(endpoint, {
            method:  'POST',
            headers: { 'Content-Type': 'application/json' },
            body:    JSON.stringify(payload)
        });
        const res = await r.json();

        if (res.error) {
            errEl.innerText = res.error;
            return;
        }

        closePointModal();
        showRebootOverlay();
    } catch (err) {
        errEl.innerText = 'Network error: ' + err.message;
    }
}

/**
 * @brief Asks for confirmation then sends a delete request to the backend.
 *        A reboot overlay is shown on success.
 *
 * @param {string} name - Point name to delete.
 */
async function deletePoint(name) {
    if (!name) return;
    if (!window.confirm(
            `Delete the point '${name}'?\nThis operation cannot be undone.`
        )) return;

    try {
        const r   = await fetch('/api/point/delete', {
            method:  'POST',
            headers: { 'Content-Type': 'application/json' },
            body:    JSON.stringify({ name })
        });
        const res = await r.json();
        if (res.error) { alert('Error: ' + res.error); return; }
        showRebootOverlay();
    } catch (err) {
        alert('Network error: ' + err.message);
    }
}

// ==============================================================================
// SECTION MODAL
// ==============================================================================

/**
 * @brief Opens the section modal in edit mode for an existing section.
 *
 * @param {string} sectionId - Target section id.
 */
function openSectionModal(sectionId) {
    const sec = cfg.sections.find(s => s.id === sectionId);
    if (!sec) return;

    editingSectionId = sectionId;
    selectedCols     = sec.widthCols || 4;

    document.getElementById('sectionModalTitle').innerText = 'SECTION SETTINGS';
    document.getElementById('smLabel').value               = sec.label;
    document.getElementById('smError').innerText           = '';

    document.querySelectorAll('.col-opt').forEach(btn => {
        btn.classList.toggle(
            'active',
            parseInt(btn.dataset.cols, 10) === selectedCols
        );
    });

    // Show "Delete Section" only when the section has no points.
    const isEmpty = !sec.points || sec.points.length === 0;
    document.getElementById('smDeleteBtn').style.display = isEmpty ? '' : 'none';

    document.getElementById('sectionModal').classList.add('open');
}

/** @brief Opens the section modal in "Add" mode. */
function openAddSectionModal() {
    editingSectionId = null;
    selectedCols     = 4;

    document.getElementById('sectionModalTitle').innerText = 'ADD SECTION';
    document.getElementById('smLabel').value               = '';
    document.getElementById('smError').innerText           = '';
    document.getElementById('smDeleteBtn').style.display   = 'none';

    document.querySelectorAll('.col-opt').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.cols === '4');
    });

    document.getElementById('sectionModal').classList.add('open');
}

/** @brief Closes the section configuration modal. */
function closeSectionModal() {
    document.getElementById('sectionModal').classList.remove('open');
    editingSectionId = null;
}

/**
 * @brief Updates the active state of column-picker buttons and stores
 *        the selection in the module-level variable.
 *
 * @param {number} cols - Selected column count (1–4).
 */
function selectCols(cols) {
    selectedCols = cols;
    document.querySelectorAll('.col-opt').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.cols, 10) === cols);
    });
}

/**
 * @brief Validates the section modal and either updates an existing
 *        section or creates a new one, then persists to the backend.
 */
async function submitSectionModal() {
    const label = document.getElementById('smLabel').value.trim();
    if (!label) {
        document.getElementById('smError').innerText = 'Label is required.';
        return;
    }

    if (editingSectionId) {
        // Update existing section.
        const sec = cfg.sections.find(s => s.id === editingSectionId);
        if (sec) {
            sec.label     = label;
            sec.widthCols = selectedCols;
        }
    } else {
        // Create a new section.
        cfg.sections.push({
            id:        'section-' + Date.now().toString(36),
            label,
            widthCols: selectedCols,
            points:    []
        });
    }

    await fetch('/api/config/layout', {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify(cfg.sections)
    }).catch(err => console.error('[SECTION] Save failed:', err));

    closeSectionModal();
    renderSections();
    if (editMode) attachSortables();
}

/**
 * @brief Deletes the currently-edited section.  The section must be
 *        empty (no points) — enforced both here and by the UI (the
 *        delete button is hidden for non-empty sections).
 */
async function deleteSection() {
    if (!editingSectionId) return;

    const sec = cfg.sections.find(s => s.id === editingSectionId);
    if (sec && sec.points && sec.points.length > 0) {
        document.getElementById('smError').innerText =
            'Move all points out of this section before deleting it.';
        return;
    }

    cfg.sections = cfg.sections.filter(s => s.id !== editingSectionId);

    await fetch('/api/config/layout', {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify(cfg.sections)
    }).catch(err => console.error('[SECTION] Delete failed:', err));

    closeSectionModal();
    renderSections();
    if (editMode) attachSortables();
}

// ==============================================================================
// ADDRESS SUGGESTION HELPERS
// ==============================================================================

/**
 * @brief Returns the lowest unused Modbus coil address.
 * @returns {number}
 */
function suggestNextCoil() {
    const used = new Set(cfg.binary.map(p => p.modbusCoil));
    let addr = 0;
    while (used.has(addr)) addr++;
    return addr;
}

/**
 * @brief Returns the lowest unused Modbus holding register address
 *        starting from 100 (conventional base for holding registers).
 * @returns {number}
 */
function suggestNextReg() {
    const used = new Set(cfg.analog.map(p => p.modbusReg));
    let addr = 100;
    while (used.has(addr)) addr++;
    return addr;
}

/**
 * @brief Returns the lowest unused BACnet object instance for the given type.
 *
 * @param {'binary'|'analog'} type
 * @returns {number}
 */
function suggestNextBacInst(type) {
    const arr  = type === 'binary' ? cfg.binary : cfg.analog;
    const used = new Set(arr.map(p => p.bacnetInstance));
    let inst = 0;
    while (used.has(inst)) inst++;
    return inst;
}

// ==============================================================================
// NETWORK MODAL — STA / AP SELECTION
// ==============================================================================

/**
 * @brief Opens the network configuration modal.
 *        The modal always presents both modes (STA and AP) as distinct
 *        panels, regardless of the current device state.  The panel
 *        matching the current mode is pre-selected.
 */
function openNetModal() {
    const title   = document.getElementById('netModalTitle');
    const body    = document.getElementById('netModalBody');
    const actions = document.getElementById('netModalActions');
    const confirm = document.getElementById('netModalConfirmBtn');

    title.innerText       = 'Network Configuration';
    actions.style.display = '';

    // Current mode determines which panel is pre-selected.
    const currentMode = sysData.isAP ? 'ap' : 'sta';

    body.innerHTML = `
        <!-- Mode selector — two clearly-labelled choices -->
        <div class="net-mode-selector">
            <button id="netBtnSta"
                    class="net-mode-btn ${currentMode === 'sta' ? 'active' : ''}"
                    onclick="_netSelectMode('sta')">
                <!-- Enterprise Wi-Fi icon -->
                <svg viewBox="0 0 24 24" width="22" height="22"
                     stroke="currentColor" stroke-width="2" fill="none">
                    <path d="M5 12.55a11 11 0 0 1 14.08 0"/>
                    <path d="M1.42 9a16 16 0 0 1 21.16 0"/>
                    <path d="M8.53 16.11a6 6 0 0 1 6.95 0"/>
                    <line x1="12" y1="20" x2="12.01" y2="20"/>
                </svg>
                Join Existing Network
                <span style="font-size:9px;opacity:0.7;">Station (STA) mode</span>
            </button>
            <button id="netBtnAp"
                    class="net-mode-btn ${currentMode === 'ap' ? 'active' : ''}"
                    onclick="_netSelectMode('ap')">
                <!-- Access-point / hotspot icon -->
                <svg viewBox="0 0 24 24" width="22" height="22"
                     stroke="currentColor" stroke-width="2" fill="none">
                    <circle cx="12" cy="12" r="3"/>
                    <path d="M6.3 6.3a8 8 0 0 0 0 11.31"/>
                    <path d="M17.7 6.3a8 8 0 0 1 0 11.31"/>
                    <path d="M3.5 3.5a12 12 0 0 0 0 16.97"/>
                    <path d="M20.5 3.5a12 12 0 0 1 0 16.97"/>
                </svg>
                Create Isolated Network
                <span style="font-size:9px;opacity:0.7;">Access Point (AP) mode</span>
            </button>
        </div>

        <!-- STA panel -->
        <div id="netPanelSta"
             class="net-form-panel ${currentMode === 'sta' ? 'visible' : ''}">
            <div class="form-group">
                <label class="form-label">WI-FI NETWORK (SSID)</label>
                <div class="input-row" style="margin-top:0;">
                    <select id="wifiSelect" class="input-field">
                        <option value="${escAttr(sysData.staSSID || '')}">
                            ${escHtml(sysData.staSSID || 'Select a network…')}
                            ${sysData.staSSID ? '(Current)' : ''}
                        </option>
                    </select>
                    <button class="btn btn-action" onclick="scanWifi(this)">
                        <!-- Refresh icon -->
                        <svg viewBox="0 0 24 24" width="12" height="12"
                             stroke="currentColor" stroke-width="2" fill="none">
                            <polyline points="23 4 23 10 17 10"/>
                            <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
                        </svg>
                        Scan
                    </button>
                </div>
            </div>
            <div class="form-group">
                <label class="form-label">PASSWORD</label>
                <input type="password" id="wifiPass" class="input-field"
                       placeholder="Wi-Fi password"
                       value="${escAttr(sysData.staPASS || '')}">
            </div>
            <div class="net-warning">
                ${ICON_WARN}
                You will be disconnected from the device's local Wi-Fi network.
                Reconnect to your enterprise network to regain access.
            </div>
        </div>

        <!-- AP panel -->
        <div id="netPanelAp"
             class="net-form-panel ${currentMode === 'ap' ? 'visible' : ''}">
            <div class="form-group">
                <label class="form-label">ACCESS POINT SSID</label>
                <input type="text" id="apSsidInput" class="input-field"
                       value="${escAttr(sysData.apSSID || 'BMS-Gateway')}">
            </div>
            <div class="form-group">
                <label class="form-label">ACCESS POINT PASSWORD</label>
                <input type="text" id="apPassInput" class="input-field"
                       value="${escAttr(sysData.apPASS || '')}">
            </div>
            <div class="net-warning">
                ${ICON_WARN}
                The device will create its own Wi-Fi network.
                You will need to connect to that network manually after applying.
            </div>
        </div>
    `;

    // Wire the confirm button to the currently-selected mode.
    confirm.onclick = () => saveAndSwitch(_netActiveMode());

    document.getElementById('netModal').classList.add('open');
}

/**
 * @brief Switches the network modal to display the selected mode panel.
 *
 * @param {'sta'|'ap'} mode
 */
function _netSelectMode(mode) {
    document.getElementById('netBtnSta').classList.toggle('active', mode === 'sta');
    document.getElementById('netBtnAp').classList.toggle('active',  mode === 'ap');
    document.getElementById('netPanelSta').classList.toggle('visible', mode === 'sta');
    document.getElementById('netPanelAp').classList.toggle('visible',  mode === 'ap');

    // Re-wire the confirm button.
    document.getElementById('netModalConfirmBtn').onclick = () => saveAndSwitch(mode);
}

/**
 * @brief Returns the currently-active network mode based on button state.
 * @returns {'sta'|'ap'}
 */
function _netActiveMode() {
    return document.getElementById('netBtnSta').classList.contains('active')
        ? 'sta'
        : 'ap';
}

/** @brief Closes the network configuration modal. */
function closeNetModal() {
    document.getElementById('netModal').classList.remove('open');
}

/**
 * @brief Triggers a Wi-Fi network scan and populates the SSID selector.
 *
 * @param {HTMLButtonElement} btn - The Scan button element (used for
 *                                  loading state feedback).
 */
function scanWifi(btn) {
    btn.disabled   = true;
    btn.innerHTML  = `
        <svg viewBox="0 0 24 24" width="12" height="12" stroke="currentColor"
             stroke-width="2" fill="none" class="spin">
            <polyline points="23 4 23 10 17 10"/>
            <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
        </svg>
        Scanning…`;

    fetch('/api/scan')
        .then(r => r.json())
        .then(nets => {
            const sel = document.getElementById('wifiSelect');
            if (!sel) return;
            sel.innerHTML = '';
            nets.forEach(n => {
                const opt = document.createElement('option');
                opt.value = n.ssid;
                opt.text  = `${n.ssid}  (${n.rssi} dBm)`;
                sel.add(opt);
            });
            btn.disabled  = false;
            btn.innerHTML = `
                <svg viewBox="0 0 24 24" width="12" height="12" stroke="currentColor"
                     stroke-width="2" fill="none">
                    <polyline points="23 4 23 10 17 10"/>
                    <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
                </svg>
                Scan`;
        })
        .catch(() => {
            btn.disabled  = false;
            btn.innerText = 'Scan Error';
        });
}

/**
 * @brief Builds the switch-network request URL and sends it; shows a
 *        rebooting message in the modal while the device restarts.
 *
 * @param {'wifi'|'local'} mode  - 'wifi' maps to STA; 'local' maps to AP.
 *                                  Note: this parameter naming is unchanged
 *                                  from the original to preserve the API.
 */
function saveAndSwitch(mode) {
    // Translate internal mode names to API parameters (unchanged API contract).
    let apiMode = mode;
    if (mode === 'sta') apiMode = 'wifi';
    if (mode === 'ap')  apiMode = 'local';

    let url = `/api/switch_network?mode=${apiMode}`;

    if (apiMode === 'wifi') {
        const ssid = document.getElementById('wifiSelect')?.value || '';
        const pass = document.getElementById('wifiPass')?.value   || '';
        url += `&ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`;
    } else {
        const apSsid = document.getElementById('apSsidInput')?.value || '';
        const apPass = document.getElementById('apPassInput')?.value  || '';
        url += `&ap_ssid=${encodeURIComponent(apSsid)}`;
        url += `&ap_pass=${encodeURIComponent(apPass)}`;
    }

    document.getElementById('netModalBody').innerHTML = `
        <div style="text-align:center;padding:24px 0;">
            <p style="font-size:14px;font-weight:700;color:var(--color-temp);">
                Applying network configuration…
            </p>
            <p style="font-size:12px;margin-top:8px;">
                The device is rebooting. Please wait, then reconnect
                to the correct network if necessary.
            </p>
        </div>`;
    document.getElementById('netModalActions').style.display = 'none';

    fetch(url).finally(() => {
        setTimeout(() => window.location.reload(), 12000);
    });
}

// ==============================================================================
// THEME MANAGEMENT
// ==============================================================================

/**
 * @brief Applies or removes dark mode and updates the theme toggle button.
 *
 * @param {boolean} isDark - True to enable dark mode.
 */
function applyTheme(isDark) {
    document.documentElement.setAttribute('data-theme', isDark ? 'dark' : 'light');
    const btn = document.getElementById('themeToggle');
    if (btn) btn.innerHTML = isDark ? SUN_SVG : MOON_SVG;
}

// ==============================================================================
// CLIPBOARD UTILITIES
// ==============================================================================

/**
 * @brief Copies text to the clipboard using a fallback textarea method
 *        for maximum browser compatibility.
 *
 * @param {string}      textToCopy  - The text to copy.
 * @param {HTMLElement} el          - The trigger element (for visual feedback).
 * @param {string}      originalHtml - Original innerHTML to restore after feedback.
 */
function forceCopy(textToCopy, el, originalHtml) {
    // Try the modern async Clipboard API first.
    if (navigator.clipboard) {
        navigator.clipboard.writeText(textToCopy).then(() => {
            _showCopiedFeedback(el, originalHtml);
        }).catch(() => _fallbackCopy(textToCopy, el, originalHtml));
    } else {
        _fallbackCopy(textToCopy, el, originalHtml);
    }
}

/**
 * @brief execCommand('copy') fallback for older browsers / non-HTTPS.
 * @private
 */
function _fallbackCopy(text, el, originalHtml) {
    const ta       = document.createElement('textarea');
    ta.value       = text;
    ta.style.position = 'fixed';
    ta.style.opacity  = '0';
    document.body.appendChild(ta);
    ta.select();
    try {
        document.execCommand('copy');
        _showCopiedFeedback(el, originalHtml);
    } catch (_) {}
    document.body.removeChild(ta);
}

/**
 * @brief Briefly highlights the clicked element to confirm a copy action.
 * @private
 */
function _showCopiedFeedback(el, originalHtml) {
    el.innerHTML   = '<b>COPIED!</b>';
    el.style.color = el.style.borderColor = 'var(--color-run)';
    setTimeout(() => {
        el.innerHTML   = originalHtml;
        el.style.color = el.style.borderColor = '';
    }, 1400);
}

/** @brief Click-to-copy handler for protocol port badges. */
function copyPort(el, port) {
    forceCopy(port, el, el.innerHTML);
}

/** @brief Click-to-copy handler for IP address spans. */
function copyIp(el, spanId) {
    const ip = document.getElementById(spanId)?.innerText;
    if (ip && ip !== '...') forceCopy(ip, el, el.innerHTML);
}

// ==============================================================================
// HTML ESCAPING UTILITIES
// ==============================================================================

/**
 * @brief Escapes a string for safe insertion as HTML text content.
 *
 * @param {string} str - Raw string.
 * @returns {string}   - HTML-escaped string.
 */
function escHtml(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

/**
 * @brief Escapes a string for safe use in an HTML attribute value.
 *
 * @param {string} str - Raw string.
 * @returns {string}   - Attribute-safe escaped string.
 */
function escAttr(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}
