// Contro UI - page unique
(() => {
  "use strict";

  // ==============================
  // Helpers DOM
  // ==============================
  const $ = (id) => document.getElementById(id);

  // ==============================
  // Etat local UI
  // ==============================
  const state = {
    historySeq: 0,
    eventSeq: 0,
    eventsInitialized: false,
    samples: [],
    events: [],
    sessions: [],
    maxSamples: 800,
    newWarningCount: 0,
    newErrorCount: 0
  };

  let buzzerEnabled = true;

  // ==============================
  // Mapping codes alertes / erreurs
  // ==============================
  const warnText = {
    1: "DS18 absent",
    2: "BME absent",
    3: "ADC sature",
    4: "Cache utilise",
    5: "NTP echec",
    6: "RTC non calibre",
    7: "Auth echec",
    8: "Non autorise",
    9: "Client deconnecte"
  };

  const errText = {
    1: "OVC verrouille",
    2: "Surchauffe",
    3: "Ecriture NVS",
    4: "Ecriture SPIFFS",
    5: "Courant perdu",
    6: "Web indisponible"
  };

  const actionLabel = {
    start: "marche",
    stop: "arret",
    relay_on: "relais marche",
    relay_off: "relais arret",
    clear_fault: "acquittement",
    reset: "redemarrage"
  };

  // ==============================
  // Auth localStorage
  // ==============================
  function loadAuth() {
    try {
      const raw = localStorage.getItem("contro_auth");
      if (!raw) return null;
      return JSON.parse(raw);
    } catch {
      return null;
    }
  }

  function hasAuth() {
    const cfg = loadAuth();
    if (!cfg) return false;
    if (cfg.mode === "token") return !!cfg.token;
    return !!(cfg.user && cfg.pass);
  }

  function authHeaders() {
    const cfg = loadAuth();
    if (!cfg) return {};
    if (cfg.mode === "token" && cfg.token) {
      return { "X-Auth-Token": cfg.token };
    }
    if (cfg.user && cfg.pass) {
      const token = btoa(`${cfg.user}:${cfg.pass}`);
      return { Authorization: `Basic ${token}` };
    }
    return {};
  }

  function ensureAuth() {
    if (hasAuth()) return true;
    window.location.href = "/login.html";
    return false;
  }

  // ==============================
  // Fetch JSON avec gestion auth
  // ==============================
  async function fetchJson(url, options = {}) {
    const headers = { Accept: "application/json" };
    if (options.body) headers["Content-Type"] = "application/json";
    if (options.auth) Object.assign(headers, authHeaders());

    const res = await fetch(url, {
      method: options.method || "GET",
      headers,
      body: options.body ? JSON.stringify(options.body) : undefined,
      cache: "no-store"
    });

    if (res.status === 401 || res.status === 403) {
      window.location.href = "/login_failed.html";
      throw new Error("non_autorise");
    }
    if (!res.ok) {
      const text = await res.text();
      throw new Error(`${res.status} ${text}`);
    }
    return res.json();
  }

  // ==============================
  // Helpers affichage
  // ==============================
  function setText(id, text) {
    const el = $(id);
    if (el) el.textContent = text;
  }

  function formatNum(v, unit, digits) {
    if (typeof v !== "number" || !isFinite(v)) return `-- ${unit}`;
    return `${v.toFixed(digits)} ${unit}`;
  }

  function setLed(id, on) {
    const el = $(id);
    if (!el) return;
    el.classList.toggle("active", !!on);
  }

  function setStateDot(stateStr, fault) {
    const dot = $("stateDot");
    if (!dot) return;
    if (fault) {
      dot.style.backgroundColor = "#ff5252";
      dot.style.boxShadow = "0 0 6px #ff5252";
      return;
    }
    const s = (stateStr || "").toLowerCase();
    if (s === "running") {
      dot.style.backgroundColor = "#00ff80";
      dot.style.boxShadow = "0 0 6px #00ff80";
    } else if (s === "idle") {
      dot.style.backgroundColor = "#00c2ff";
      dot.style.boxShadow = "0 0 6px #00c2ff";
    } else {
      dot.style.backgroundColor = "#999";
      dot.style.boxShadow = "0 0 6px #444";
    }
  }

  function formatState(stateStr) {
    const s = (stateStr || "").toLowerCase();
    if (s === "running") return "Marche";
    if (s === "idle") return "Attente";
    if (s === "off") return "Arret";
    if (s === "fault") return "Defaut";
    if (s === "shutdown") return "Arret";
    return stateStr || "--";
  }

  function updateGauges(data) {
    document.querySelectorAll(".gauge-card").forEach((card) => {
      const field = card.dataset.field;
      const unit = card.dataset.unit || "";
      const max = parseFloat(card.dataset.max || "100");
      const value = Number(data[field]);
      const safe = Number.isFinite(value) ? value : NaN;
      const ratio = max > 0 && Number.isFinite(safe) ? Math.max(0, Math.min(1, safe / max)) : 0;
      const pct = ratio * 100;

      const path = card.querySelector(".gauge-fg");
      if (path) path.style.strokeDasharray = `${pct} 100`;

      const txt = card.querySelector(".gauge-value");
      if (txt) txt.textContent = Number.isFinite(safe) ? `${safe.toFixed(1)} ${unit}` : `-- ${unit}`;
    });
  }

  function updateMuteButton() {
    const btn = $("muteBtn");
    const label = $("muteLabel");
    if (!btn || !label) return;

    if (buzzerEnabled) {
      label.textContent = "Son";
      btn.classList.remove("muted");
      btn.title = "Couper le son";
    } else {
      label.textContent = "Muet";
      btn.classList.add("muted");
      btn.title = "Activer le son";
    }
  }

  // ==============================
  // Horloge (client)
  // ==============================
  function updateAnalogClock() {
    const h = $("clockHour");
    const m = $("clockMin");
    const s = $("clockSec");
    const d = $("clockDigital");
    if (!h || !m || !s || !d) return;

    const now = new Date();
    const hh = now.getHours();
    const mm = now.getMinutes();
    const ss = now.getSeconds();

    const min = mm + ss / 60;
    const hour = (hh % 12) + min / 60;

    const hourAngle = hour * 30;
    const minAngle = min * 6;
    const secAngle = ss * 6;

    // Rotation autour du centre (cx=60, cy=60)
    h.setAttribute("transform", `rotate(${hourAngle} 60 60)`);
    m.setAttribute("transform", `rotate(${minAngle} 60 60)`);
    s.setAttribute("transform", `rotate(${secAngle} 60 60)`);

    d.textContent = `${String(hh).padStart(2, "0")}:${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}`;
  }

  function startClock() {
    updateAnalogClock();
    setInterval(updateAnalogClock, 1000);
  }

  // ==============================
  // Infos device
  // ==============================
  async function loadInfo() {
    const data = await fetchJson("/api/info");
    const ip = data.ip || "-";
    const mdns = data.mdns || "-";
    const sw = data.sw || "-";
    const meta = `IP: ${ip} | mDNS: ${mdns} | Version: ${sw}`;
    setText("deviceMeta", meta);
    const el = $("deviceMeta");
    if (el) el.title = meta;
  }

  // ==============================
  // Snapshot live
  // ==============================
  async function pollStatus() {
    const data = await fetchJson("/api/status");

    setText("stateChip", formatState(data.state));
    setText("relayChip", `R: ${data.relay_on ? "marche" : "arret"}`);
    setText("faultChip", `F: ${data.fault_latched ? "verrouille" : "ok"}`);
    updateNotifBadges();

    setText("energyValue", formatNum(data.energy_wh, "Wh", 2));
    setText("boardValue", formatNum(data.board_c, "C", 1));
    setText("ambientValue", formatNum(data.ambient_c, "C", 1));

    // Barre de statut (toujours visible)
    setText("sbCurrent", formatNum(data.current_a, "A", 2));
    setText("sbMotorTemp", formatNum(data.motor_c, "C", 1));
    setText("sbBoardTemp", formatNum(data.board_c, "C", 1));

    setLed("ds18Led", !!data.ds18_ok);
    setLed("bmeLed", !!data.bme_ok);
    setLed("adcLed", !!data.adc_ok);

    const relayIndicator = $("relayIndicator");
    if (relayIndicator) relayIndicator.classList.toggle("on", !!data.relay_on);
    const relayStateText = $("relayStateText");
    if (relayStateText) relayStateText.textContent = data.relay_on ? "ON" : "OFF";

    setText("ageMs", `age: ${data.age_ms || 0} ms`);
    setStateDot(data.state, data.fault_latched);

    updateGauges(data);
  }

  function updateNotifBadges() {
    const wChip = $("warningChip");
    const eChip = $("errorChip");
    const wText = $("warningText");
    const eText = $("errorText");

    const wCount = Math.max(0, Number(state.newWarningCount) || 0);
    const eCount = Math.max(0, Number(state.newErrorCount) || 0);

    if (wText) wText.textContent = String(wCount);
    if (eText) eText.textContent = String(eCount);

    if (wChip) {
      wChip.classList.toggle("has-new", wCount > 0);
      wChip.classList.toggle("active", wCount > 0);
    }
    if (eChip) {
      eChip.classList.toggle("has-new", eCount > 0);
      eChip.classList.toggle("active", eCount > 0);
    }
  }

  function clearNotifBadges() {
    state.newWarningCount = 0;
    state.newErrorCount = 0;
    updateNotifBadges();
  }

  // ==============================
  // Historique mesures
  // ==============================
  async function pollHistory() {
    const url = `/api/history?since=${state.historySeq}&max=200`;
    const data = await fetchJson(url);
    const samples = data.samples || [];
    state.historySeq = data.seq_end || state.historySeq;

    if (samples.length) {
      samples.forEach((s) => {
        state.samples.push(s);
        if (state.samples.length > state.maxSamples) state.samples.shift();
      });
    }

    updateCharts();
  }

  // ==============================
  // Evenements warnings / errors
  // ==============================
  async function pollEvents() {
    const url = `/api/events?since=${state.eventSeq}&max=100`;
    const data = await fetchJson(url);
    const events = data.events || [];
    state.eventSeq = data.seq_end || state.eventSeq;

    if (events.length) {
      const eventsTab = $("eventsTab");
      const eventsActive = !!(eventsTab && eventsTab.classList.contains("active"));
      const shouldCount = state.eventsInitialized && !eventsActive;
      events.forEach((e) => {
        state.events.push(e);
        if (state.events.length > 200) state.events.shift();

        if (shouldCount) {
          if (e.level === 2) state.newErrorCount = Math.min(99, state.newErrorCount + 1);
          else state.newWarningCount = Math.min(99, state.newWarningCount + 1);
        }
      });

      if (state.eventsInitialized) {
        if (eventsActive) clearNotifBadges();
        else updateNotifBadges();
      }
    }

    if (!state.eventsInitialized) {
      state.eventsInitialized = true;
      updateNotifBadges();
    }

    renderEvents();
  }

  function renderEvents() {
    const list = $("eventList");
    if (!list) return;
    list.innerHTML = "";
    setText("eventMeta", `seq: ${state.eventSeq}`);

    const recent = state.events.slice(-30).reverse();
    if (!recent.length) {
      list.innerHTML = '<div class="event-item">Aucun evenement</div>';
      return;
    }

    recent.forEach((e) => {
      const level = e.level === 2 ? "error" : "warning";
      const label = e.level === 2
        ? `E${String(e.code || 0).padStart(2, "0")}`
        : `W${String(e.code || 0).padStart(2, "0")}`;
      const msg = (e.level === 2 ? errText[e.code] : warnText[e.code]) || e.message || "";
      const when = formatEventTime(e.ts_ms);
      const item = document.createElement("div");
      item.className = `event-item ${level}`;
      item.innerHTML = `
        <div class="event-kind">${label}</div>
        <div class="event-reason">${msg}</div>
        <div class="event-time mono">${when}</div>
      `;
      list.appendChild(item);
    });
  }

  function formatEventTime(tsMs) {
    const ms = Number(tsMs);
    if (!Number.isFinite(ms) || ms <= 0) return "--";

    // Timestamp epoch (ms)
    if (ms > 1000000000000) {
      const d = new Date(ms);
      const yyyy = d.getFullYear();
      const mo = String(d.getMonth() + 1).padStart(2, "0");
      const da = String(d.getDate()).padStart(2, "0");
      const hh = String(d.getHours()).padStart(2, "0");
      const mm = String(d.getMinutes()).padStart(2, "0");
      const ss = String(d.getSeconds()).padStart(2, "0");
      return `${yyyy}-${mo}-${da} ${hh}:${mm}:${ss}`;
    }

    // Sinon: ms depuis boot (ou equiv)
    return `+${formatClock(ms)}`;
  }

  // ==============================
  // Sessions
  // ==============================
  function formatEpochSec(epochSec) {
    const sec = Number(epochSec);
    if (!Number.isFinite(sec) || sec <= 0) return "--";
    const d = new Date(sec * 1000);
    const yyyy = d.getFullYear();
    const mm = String(d.getMonth() + 1).padStart(2, "0");
    const dd = String(d.getDate()).padStart(2, "0");
    const hh = String(d.getHours()).padStart(2, "0");
    const min = String(d.getMinutes()).padStart(2, "0");
    return `${yyyy}-${mm}-${dd} ${hh}:${min}`;
  }

  async function loadSessions() {
    const data = await fetchJson("/api/sessions");
    state.sessions = data.sessions || [];

    const body = $("sessionTableBody");
    if (!body) return;
    body.innerHTML = "";

    if (!state.sessions.length) {
      const row = document.createElement("tr");
      row.innerHTML = '<td colspan="7">Aucune session</td>';
      body.appendChild(row);
      return;
    }

    state.sessions.slice().reverse().forEach((s) => {
      const row = document.createElement("tr");
      row.innerHTML = `
        <td>${formatEpochSec(s.start_epoch)}</td>
        <td>${formatEpochSec(s.end_epoch)}</td>
        <td>${s.duration_s}</td>
        <td>${s.energy_wh}</td>
        <td>${s.peak_power_w}</td>
        <td>${s.peak_current_a}</td>
        <td>${s.success ? "oui" : "non"}</td>
      `;
      body.appendChild(row);
    });
  }

  // ==============================
  // Graphiques (SVG + axe Y fixe)
  // ==============================
  const charts = [];

  function clampValue(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  function formatClock(ms) {
    if (!Number.isFinite(ms)) return "--:--:--";
    // Si on recoit un timestamp epoch (mock ou futur backend), on affiche HH:MM:SS.
    if (ms > 1000000000000) {
      const d = new Date(ms);
      const hh = String(d.getHours()).padStart(2, "0");
      const mm = String(d.getMinutes()).padStart(2, "0");
      const ss = String(d.getSeconds()).padStart(2, "0");
      return `${hh}:${mm}:${ss}`;
    }
    const total = Math.max(0, Math.floor(ms / 1000));
    const hh = Math.floor(total / 3600);
    const mm = Math.floor((total % 3600) / 60);
    const ss = total % 60;
    return `${String(hh).padStart(2, "0")}:${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}`;
  }

  function createChart(opts) {
    const axisSvg = $(opts.axisId);
    const plotSvg = $(opts.plotId);
    const scrollWrap = $(opts.scrollId);
    const nowEl = $(opts.nowId);
    const timeEl = $(opts.timeId);
    const btnLatest = $(opts.latestBtnId);
    const btnPause = $(opts.pauseBtnId);

    if (!axisSvg || !plotSvg || !scrollWrap) return null;

    const unit = opts.unit || "";
    const min = opts.min ?? 0;
    const max = opts.max ?? 100;
    const tickStep = opts.tickStep ?? 10;
    const precision = opts.precision ?? 1;
    const height = opts.height ?? 320;
    const y0 = opts.y0 ?? 260;
    const y1 = opts.y1 ?? 30;
    const padLeft = opts.padLeft ?? 10;
    const rightPad = opts.rightPad ?? 24;
    const dx = opts.dx ?? 10;
    const tickEveryMs = opts.tickEveryMs ?? 5000;
    const dotRadius = opts.dotRadius ?? 6;
    const dotClass = opts.dotClass ?? "chart-end-dot";

    let paused = false;
    let data = [];

    function yFromValue(v) {
      const t = clampValue(v, min, max);
      return y0 - ((t - min) * (y0 - y1)) / (max - min);
    }

    function tag(x, y, text) {
      const padX = 6;
      const charW = 7;
      const w = Math.max(34, text.length * charW + padX * 2);
      const h = 20;
      return `
        <g>
          <rect class="chart-tag" x="${x}" y="${y - h / 2}" width="${w}" height="${h}" rx="9"></rect>
          <text class="chart-tag-text" x="${x + padX}" y="${y + 4}">${text}</text>
        </g>
      `;
    }

    function drawYAxis() {
      const w = 72;
      axisSvg.setAttribute("width", w);
      axisSvg.setAttribute("height", height);
      axisSvg.setAttribute("viewBox", `0 0 ${w} ${height}`);

      let s = `
        <line class="chart-axis-line" x1="${w - 1}" y1="${y1}" x2="${w - 1}" y2="${y0}"></line>
        <text class="chart-label" x="16" y="${(y1 + y0) / 2}" transform="rotate(-90 16 ${(y1 + y0) / 2})">${unit}</text>
      `;
      for (let t = min; t <= max + 0.0001; t += tickStep) {
        const y = yFromValue(t);
        s += `<line class="chart-y-tick" x1="${w - 8}" y1="${y}" x2="${w - 1}" y2="${y}"></line>`;
        s += `<text class="chart-y-label" x="${w - 12}" y="${y + 4}" text-anchor="end">${t.toFixed(0)}</text>`;
      }
      axisSvg.innerHTML = s;
    }

    function buildGrid(xMax) {
      let s = `<g>`;
      for (let t = min; t <= max + 0.0001; t += tickStep) {
        const y = yFromValue(t);
        s += `<line class="chart-grid-line" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
      }

      if (data.length > 1) {
        const span = data[data.length - 1].tsMs - data[0].tsMs;
        const avgMs = span > 0 ? span / Math.max(1, data.length - 1) : 1000;
        const gridEverySamples = Math.max(1, Math.round(1000 / Math.max(avgMs, 1)));
        for (let i = 0; i < data.length; i += gridEverySamples) {
          const x = padLeft + i * dx;
          s += `<line class="chart-grid-line" x1="${x}" y1="${y1}" x2="${x}" y2="${y0}"></line>`;
        }
      }

      s += `</g>`;
      return s;
    }

    function buildXAxis(xMax) {
      return `
        <line class="chart-axis-line" x1="0" y1="${y0}" x2="${xMax}" y2="${y0}"></line>
        <text class="chart-label" x="${Math.max(40, xMax / 2 - 20)}" y="${height - 10}">Temps</text>
      `;
    }

    function buildTimeTicks() {
      let s = `<g>`;
      if (!data.length) return s + `</g>`;

      let nextTick = data[0].tsMs + tickEveryMs;
      for (let i = 0; i < data.length; i += 1) {
        const ts = data[i].tsMs;
        if (ts < nextTick) continue;
        const x = padLeft + i * dx;
        const label = formatClock(ts);
        s += `<line class="chart-tick" x1="${x}" y1="${y0}" x2="${x}" y2="${y0 + 6}"></line>`;
        s += `<text class="chart-subtext" x="${x - 18}" y="${y0 + 22}">${label}</text>`;
        nextTick += tickEveryMs;
      }
      s += `</g>`;
      return s;
    }

    function buildPolyline() {
      if (data.length === 0) return "";
      const pts = data
        .map((p, i) => `${padLeft + i * dx},${yFromValue(p.value)}`)
        .join(" ");
      return `<polyline class="chart-line" points="${pts}"></polyline>`;
    }

    function buildLatestMarker(xMax) {
      if (!data.length) return "";
      const i = data.length - 1;
      const p = data[i];
      const x = padLeft + i * dx;
      const y = yFromValue(p.value);

      const timeLabel = formatClock(p.tsMs);
      const valueLabel = `${p.value.toFixed(precision)} ${unit}`;
      const timeTagX = clampValue(x + 8, 8, xMax - 110);

      return `
        <g>
          <line class="chart-crosshair" x1="${x}" y1="${y1}" x2="${x}" y2="${y0}"></line>
          <line class="chart-crosshair" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>
          <circle class="${dotClass}" cx="${x}" cy="${y}" r="${dotRadius}"></circle>
          ${tag(timeTagX, y0 + 26, timeLabel)}
          ${tag(8, clampValue(y, y1 + 12, y0 - 12), valueLabel)}
        </g>
      `;
    }

    function redraw() {
      const xMax = padLeft + Math.max(1, data.length - 1) * dx + rightPad;
      plotSvg.setAttribute("width", xMax);
      plotSvg.setAttribute("height", height);
      plotSvg.setAttribute("viewBox", `0 0 ${xMax} ${height}`);

      if (data.length === 0) {
        plotSvg.innerHTML = `
          ${buildGrid(xMax)}
          ${buildXAxis(xMax)}
          <text class="chart-subtext" x="8" y="18">Pas de donnees</text>
        `;
        return;
      }

      plotSvg.innerHTML = `
        ${buildGrid(xMax)}
        ${buildXAxis(xMax)}
        ${buildTimeTicks()}
        <text class="chart-subtext" x="8" y="18">Dernier point: point + guides</text>
        ${buildPolyline()}
        ${buildLatestMarker(xMax)}
      `;
    }

    function scrollToLatest() {
      scrollWrap.scrollLeft = scrollWrap.scrollWidth;
    }

    function updateFromSamples(samples, key) {
      if (paused) return;
      const wasNearEnd =
        scrollWrap.scrollLeft + scrollWrap.clientWidth >= scrollWrap.scrollWidth - 30;

      data = samples
        .map((s) => ({ tsMs: Number(s.ts_ms) || 0, value: Number(s[key]) }))
        .filter((p) => Number.isFinite(p.value));

      const last = data[data.length - 1];
      if (last && nowEl) nowEl.textContent = `${last.value.toFixed(precision)} ${unit}`;
      if (last && timeEl) timeEl.textContent = formatClock(last.tsMs);

      redraw();
      if (wasNearEnd) scrollToLatest();
    }

    // Drag-to-pan
    let dragging = false;
    let startX = 0;
    let startScrollLeft = 0;

    function dragStart(clientX) {
      dragging = true;
      scrollWrap.classList.add("dragging");
      startX = clientX;
      startScrollLeft = scrollWrap.scrollLeft;
    }

    function dragMove(clientX) {
      if (!dragging) return;
      const dxMove = clientX - startX;
      scrollWrap.scrollLeft = startScrollLeft - dxMove;
    }

    function dragEnd() {
      dragging = false;
      scrollWrap.classList.remove("dragging");
    }

    scrollWrap.addEventListener("mousedown", (e) => {
      if (e.button !== 0) return;
      dragStart(e.clientX);
    });
    window.addEventListener("mousemove", (e) => dragMove(e.clientX));
    window.addEventListener("mouseup", dragEnd);
    scrollWrap.addEventListener(
      "touchstart",
      (e) => {
        if (e.touches.length !== 1) return;
        dragStart(e.touches[0].clientX);
      },
      { passive: true }
    );
    scrollWrap.addEventListener(
      "touchmove",
      (e) => {
        if (e.touches.length !== 1) return;
        dragMove(e.touches[0].clientX);
      },
      { passive: true }
    );
    scrollWrap.addEventListener("touchend", dragEnd);

    btnLatest?.addEventListener("click", () => scrollToLatest());
    btnPause?.addEventListener("click", () => {
      paused = !paused;
      btnPause.textContent = paused ? "Reprendre" : "Pause";
    });

    drawYAxis();
    redraw();

    return {
      update: (samples) => updateFromSamples(samples, opts.valueKey),
      scrollToLatest
    };
  }

  function initCharts() {
    charts.length = 0;
    const current = createChart({
      axisId: "currentAxis",
      plotId: "currentPlot",
      scrollId: "currentScroll",
      nowId: "currentNow",
      timeId: "currentTime",
      latestBtnId: "currentLatest",
      pauseBtnId: "currentPause",
      valueKey: "current_a",
      unit: "A",
      min: 0,
      max: 20,
      tickStep: 5,
      precision: 2
    });

    const temp = createChart({
      axisId: "tempAxis",
      plotId: "tempPlot",
      scrollId: "tempScroll",
      nowId: "tempNow",
      timeId: "tempTime",
      latestBtnId: "tempLatest",
      pauseBtnId: "tempPause",
      valueKey: "motor_c",
      unit: "C",
      min: 0,
      max: 100,
      tickStep: 10,
      precision: 1,
      dotRadius: 7
    });

    if (current) charts.push(current);
    if (temp) charts.push(temp);
  }

  function updateCharts() {
    charts.forEach((chart) => chart.update(state.samples));
  }

  // ==============================
  // Configuration (GET/POST)
  // ==============================
  async function loadConfig() {
    const data = await fetchJson("/api/config");
    const form = $("configForm");
    if (!form) return;

    const map = {
      ovc_mode: data.ovc_mode === 1 ? "auto" : "latch",
      wifi_mode: data.wifi_mode === 1 ? "ap" : "sta"
    };

    Object.keys(data).forEach((k) => {
      const field = form.elements.namedItem(k);
      if (!field) return;
      if (k === "ovc_mode" || k === "wifi_mode") return;
      field.value = data[k];
    });

    const ovcEl = form.elements.namedItem("ovc_mode");
    if (ovcEl) ovcEl.value = map.ovc_mode;
    const wifiEl = form.elements.namedItem("wifi_mode");
    if (wifiEl) wifiEl.value = map.wifi_mode;

    const latch = form.elements.namedItem("latch_overtemp");
    if (latch) latch.value = data.latch_overtemp ? "true" : "false";

    const buz = form.elements.namedItem("buzzer_enabled");
    if (buz) buz.value = data.buzzer_enabled ? "true" : "false";

    const staPass = form.elements.namedItem("sta_pass");
    if (staPass) staPass.value = "";
    const apPass = form.elements.namedItem("ap_pass");
    if (apPass) apPass.value = "";

    buzzerEnabled = !!data.buzzer_enabled;
    updateMuteButton();

    const calZero = $("calZeroMv");
    if (calZero && data.current_zero_mv != null) calZero.value = data.current_zero_mv;
    const calSens = $("calSens");
    if (calSens && data.current_sens_mv_a != null) calSens.value = data.current_sens_mv_a;
    const calScale = $("calScale");
    if (calScale && data.current_input_scale != null) calScale.value = data.current_input_scale;
  }

  function buildConfigPayload(form) {
    const names = [
      "limit_current_a",
      "ovc_mode",
      "ovc_min_ms",
      "ovc_retry_ms",
      "temp_motor_c",
      "temp_board_c",
      "temp_ambient_c",
      "temp_hyst_c",
      "latch_overtemp",
      "motor_vcc_v",
      "sampling_hz",
      "buzzer_enabled",
      "wifi_mode",
      "sta_ssid",
      "sta_pass",
      "ap_ssid",
      "ap_pass"
    ];

    const payload = {};
    names.forEach((name) => {
      const field = form.elements.namedItem(name);
      if (!field) return;
      const raw = field.value;
      if (raw === "" || raw == null) return;

      if (name === "latch_overtemp" || name === "buzzer_enabled") {
        payload[name] = raw === "true";
        return;
      }

      if (name === "ovc_mode" || name === "wifi_mode") {
        payload[name] = String(raw).toLowerCase();
        return;
      }

      if (name.includes("ssid") || name.includes("pass")) {
        payload[name] = String(raw);
        return;
      }

      const num = Number(raw);
      payload[name] = Number.isFinite(num) ? num : raw;
    });

    return payload;
  }

  async function submitConfig(evt) {
    evt.preventDefault();
    if (!ensureAuth()) return;
    const form = $("configForm");
    const status = $("configStatus");
    const payload = buildConfigPayload(form);

    try {
      await fetchJson("/api/config", { method: "POST", auth: true, body: payload });
      await loadConfig().catch(() => {});
      await pollStatus().catch(() => {});
      await pollEvents().catch(() => {});
      if (status) status.textContent = "Configuration OK";
    } catch (err) {
      if (status) status.textContent = `Erreur configuration: ${err.message}`;
    }
  }

  // ==============================
  // Commandes
  // ==============================
  async function sendControl(action) {
    if (!ensureAuth()) return;
    const status = $("controlStatus");
    const label = actionLabel[action] || action;
    try {
      if (status) status.textContent = `Envoi: ${label}...`;
      await fetchJson("/api/control", { method: "POST", auth: true, body: { action } });
      await pollStatus().catch(() => {});
      await pollEvents().catch(() => {});
      if (status) status.textContent = `OK: ${label}`;
    } catch (err) {
      if (status) status.textContent = `Erreur commande: ${err.message}`;
    }
  }

  async function sendRunTimer() {
    if (!ensureAuth()) return;
    const seconds = parseInt($("runSeconds").value || "0", 10);
    const status = $("controlStatus");
    try {
      if (status) status.textContent = "Envoi: minuterie...";
      await fetchJson("/api/run_timer", { method: "POST", auth: true, body: { seconds } });
      await pollStatus().catch(() => {});
      await pollEvents().catch(() => {});
      if (status) status.textContent = "Minuterie OK";
    } catch (err) {
      if (status) status.textContent = `Erreur minuterie: ${err.message}`;
    }
  }

  async function toggleMute() {
    if (!ensureAuth()) return;
    const next = !buzzerEnabled;
    const status = $("controlStatus");

    try {
      await fetchJson("/api/config", { method: "POST", auth: true, body: { buzzer_enabled: next } });
      buzzerEnabled = next;
      updateMuteButton();

      const buz = document.querySelector('select[name="buzzer_enabled"]');
      if (buz) buz.value = buzzerEnabled ? "true" : "false";

      if (status) status.textContent = buzzerEnabled ? "Son actif" : "Son coupe";
    } catch (err) {
      if (status) status.textContent = `Erreur son: ${err.message}`;
    }
  }

  // ==============================
  // Calibration
  // ==============================
  async function sendCalZero() {
    if (!ensureAuth()) return;
    const status = $("calStatus");
    try {
      await fetchJson("/api/calibrate", { method: "POST", auth: true, body: { action: "current_zero" } });
      await loadConfig().catch(() => {});
      if (status) status.textContent = "Calib zero OK";
    } catch (err) {
      if (status) status.textContent = `Erreur calib: ${err.message}`;
    }
  }

  async function sendCalApply() {
    if (!ensureAuth()) return;
    const status = $("calStatus");
    const zero_mv = parseFloat($("calZeroMv").value || "0");
    const sens_mv_a = parseFloat($("calSens").value || "100");
    const input_scale = parseFloat($("calScale").value || "1");

    try {
      await fetchJson("/api/calibrate", {
        method: "POST",
        auth: true,
        body: { action: "current_sensitivity", zero_mv, sens_mv_a, input_scale }
      });
      await loadConfig().catch(() => {});
      if (status) status.textContent = "Calib appliquee";
    } catch (err) {
      if (status) status.textContent = `Erreur calib: ${err.message}`;
    }
  }

  // ==============================
  // RTC
  // ==============================
  async function syncRtcFromClient() {
    if (!ensureAuth()) return;
    const status = $("rtcStatus");
    const epoch = Math.floor(Date.now() / 1000);
    try {
      if (status) status.textContent = "Synchronisation...";
      await fetchJson("/api/rtc", { method: "POST", auth: true, body: { epoch } });
      await pollEvents().catch(() => {});
      if (status) status.textContent = "RTC synchronise (heure navigateur)";
    } catch (err) {
      if (status) status.textContent = `Erreur RTC: ${err.message}`;
    }
  }

  // ==============================
  // Liaison UI
  // ==============================
  function setActiveTab(index) {
    const tabs = Array.from(document.querySelectorAll(".tab"));
    const pages = Array.from(document.querySelectorAll(".content"));
    tabs.forEach((t, idx) => t.classList.toggle("active", idx === index));
    pages.forEach((c, idx) => c.classList.toggle("active", idx === index));

    const tab = tabs[index];
    if (tab && tab.dataset && tab.dataset.tab === "events") clearNotifBadges();
  }

  function setActiveTabByName(name) {
    const tabs = Array.from(document.querySelectorAll(".tab"));
    const index = tabs.findIndex((t) => (t.dataset ? t.dataset.tab : "") === name);
    if (index >= 0) setActiveTab(index);
  }

  function bindControls() {
    document.querySelectorAll(".tab").forEach((tab, i) => {
      tab.addEventListener("click", () => {
        setActiveTab(i);
      });
    });

    $("btnStart")?.addEventListener("click", () => sendControl("start"));
    $("btnStop")?.addEventListener("click", () => sendControl("stop"));
    $("btnRelayOn")?.addEventListener("click", () => sendControl("relay_on"));
    $("btnRelayOff")?.addEventListener("click", () => sendControl("relay_off"));
    $("btnClearFault")?.addEventListener("click", () => sendControl("clear_fault"));
    $("btnRunTimer")?.addEventListener("click", sendRunTimer);

    $("muteBtn")?.addEventListener("click", toggleMute);
    $("btnReset")?.addEventListener("click", () => sendControl("reset"));

    $("warningChip")?.addEventListener("click", () => setActiveTabByName("events"));
    $("errorChip")?.addEventListener("click", () => setActiveTabByName("events"));

    $("loginBtn")?.addEventListener("click", () => (window.location.href = "/login.html"));
    $("logoutBtn")?.addEventListener("click", () => {
      localStorage.removeItem("contro_auth");
      window.location.href = "/login.html";
    });

    $("configForm")?.addEventListener("submit", submitConfig);
    $("reloadConfig")?.addEventListener("click", () => loadConfig().catch(() => {}));

    $("calZeroBtn")?.addEventListener("click", sendCalZero);
    $("calApplyBtn")?.addEventListener("click", sendCalApply);

    $("rtcSyncBtn")?.addEventListener("click", syncRtcFromClient);

    $("sessionReloadBtn")?.addEventListener("click", () => loadSessions().catch(() => {}));
  }

  // ==============================
  // Demarrage UI
  // ==============================
  async function start() {
    bindControls();
    updateMuteButton();
    initCharts();
    startClock();

    await Promise.all([
      loadInfo().catch(() => {}),
      loadConfig().catch(() => {}),
      loadSessions().catch(() => {}),
      pollStatus().catch(() => {}),
      pollHistory().catch(() => {}),
      pollEvents().catch(() => {})
    ]);

    setInterval(() => pollStatus().catch(() => {}), 1000);
    setInterval(() => pollHistory().catch(() => {}), 1500);
    setInterval(() => pollEvents().catch(() => {}), 2000);
    setInterval(() => loadSessions().catch(() => {}), 15000);
  }

  window.addEventListener("load", () => {
    start().catch(() => {});
  });
})();
