// Contro UI - mock local (sans firmware)
// Active en ajoutant dans index.html:
//   <script src="js/mock.js"></script>
//
// Objectifs:
// - Simuler un device "vivant" (relais, OVC, surchauffe, run timer, energie)
// - Produire un historique (courant + temperature moteur) et un journal d'evenements
// - Generer des sessions (comme si elles etaient sauvegardees en SPIFFS)
// - Simuler les diagnostics capteurs (DS18/BME/ADC)
(() => {
  "use strict";

  const originalFetch = window.fetch ? window.fetch.bind(window) : null;

  // ------------------------------
  // Auth mock (simple)
  // ------------------------------
  const AUTH_KEY = "contro_auth";
  const DEMO_USER = "demo";
  const DEMO_PASS = "demo";

  // Si aucune auth n'est definie, on en injecte une pour eviter la redirection login.
  try {
    if (!localStorage.getItem(AUTH_KEY)) {
      localStorage.setItem(
        AUTH_KEY,
        JSON.stringify({ mode: "basic", user: DEMO_USER, pass: DEMO_PASS, token: "" })
      );
    }
  } catch {
    // Rien
  }

  function parseBasicAuth(headerValue) {
    if (!headerValue || typeof headerValue !== "string") return null;
    const m = headerValue.match(/^Basic\s+(.+)$/i);
    if (!m) return null;
    try {
      const decoded = atob(m[1]);
      const idx = decoded.indexOf(":");
      if (idx < 0) return null;
      return { user: decoded.slice(0, idx), pass: decoded.slice(idx + 1) };
    } catch {
      return null;
    }
  }

  function isAuthorized(init) {
    const headers = (init && init.headers) || {};
    const auth = headers.Authorization || headers.authorization || "";
    const token = headers["X-Auth-Token"] || headers["x-auth-token"] || "";

    if (token) return true;
    const parsed = parseBasicAuth(auth);
    if (!parsed) return false;
    return parsed.user === DEMO_USER && parsed.pass === DEMO_PASS;
  }

  // ------------------------------
  // Helpers
  // ------------------------------
  function clampValue(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  function rnd(min, max) {
    return min + Math.random() * (max - min);
  }

  // ------------------------------
  // Config mock (equivalent NVS)
  // ------------------------------
  const config = {
    limit_current_a: 18.0,
    ovc_mode: 0, // 0=latch, 1=auto
    ovc_min_ms: 40,
    ovc_retry_ms: 5000,
    temp_motor_c: 85,
    temp_board_c: 70,
    temp_ambient_c: 60,
    temp_hyst_c: 5,
    latch_overtemp: true,
    motor_vcc_v: 12.0,
    sampling_hz: 50,
    buzzer_enabled: true,
    wifi_mode: 0, // 0=sta, 1=ap
    sta_ssid: "demo",
    sta_pass: "",
    ap_ssid: "contro",
    ap_pass: ""
  };

  // Calibration courant (ACS712) - ici c'est juste stocke, sans ADC reel.
  const calibration = {
    zero_mv: 2500.0,
    sens_mv_a: 100.0,
    input_scale: 1.0
  };

  // ------------------------------
  // RTC mock
  // ------------------------------
  let rtcEpochBaseSec = Math.floor(Date.now() / 1000);
  let rtcSetAtMs = Date.now();

  function nowRtcEpochSec() {
    const dt = Math.floor((Date.now() - rtcSetAtMs) / 1000);
    return rtcEpochBaseSec + Math.max(0, dt);
  }

  // ------------------------------
  // Etat runtime
  // ------------------------------
  const device = {
    // Etats
    state: "Idle", // Idle | Running | Fault
    desired_on: false,
    relay_on: false,
    run_until_ms: 0,

    // Fault
    fault_latched: false,
    fault_code: 0,
    trip_ms: 0,
    ovc_over_ms: 0,
    adc_fail_ms: 0,

    // Diagnostics
    ds18_ok: true,
    bme_ok: true,
    adc_ok: true,
    ds18_drop_until_ms: 0,
    ds18_next_glitch_ms: Date.now() + 45000,
    bme_drop_until_ms: 0,
    bme_next_glitch_ms: Date.now() + 90000,
    adc_fail_until_ms: 0,

    // Mesures (cachees)
    energy_wh: 0,
    last_current: 0,
    last_power: 0,
    motor_c: 35,
    board_c: 30,
    ambient_c: 28,

    // Session en cours
    session_active: false,
    session_start_epoch: 0,
    session_start_ms: 0,
    session_peak_current_a: 0,
    session_peak_power_w: 0,

    // Alertes / erreurs "dernier code"
    last_warning: 0,
    last_error: 0,

    // Simulation thermique
    heat: 0,
    last_ts_ms: Date.now(),

    // NTP mock (en mode STA)
    rtc_calibrated: false,
    ntp_next_ms: Date.now() + 15000
  };

  // ------------------------------
  // Journal evenements / sessions
  // ------------------------------
  const history = [];
  let historySeq = 0;

  const eventLog = [];
  let eventSeq = 0;

  const sessions = [];

  function pushEvent(level, code, message, source) {
    eventSeq += 1;
    eventLog.push({
      seq: eventSeq,
      ts_ms: Date.now(),
      level,
      code,
      message: message || "",
      source: source || "mock"
    });
    if (eventLog.length > 250) eventLog.shift();

    if (level === 2) device.last_error = Number(code) || 0;
    else device.last_warning = Number(code) || 0;
  }

  // RTC pas calibre au boot (feature demande)
  pushEvent(1, 6, "RTC non calibre", "rtc");

  function beginSessionIfNeeded() {
    if (device.session_active) return;
    device.session_active = true;
    device.session_start_epoch = nowRtcEpochSec();
    device.session_start_ms = Date.now();
    device.session_peak_current_a = 0;
    device.session_peak_power_w = 0;
    device.energy_wh = 0;
  }

  function endSessionIfNeeded(success) {
    if (!device.session_active) return;
    const end_epoch = nowRtcEpochSec();
    const duration_s = Math.max(0, Math.floor((Date.now() - device.session_start_ms) / 1000));
    sessions.push({
      start_epoch: device.session_start_epoch,
      end_epoch,
      duration_s,
      energy_wh: Number(device.energy_wh.toFixed(2)),
      peak_power_w: Number(device.session_peak_power_w.toFixed(1)),
      peak_current_a: Number(device.session_peak_current_a.toFixed(2)),
      success: !!success
    });
    if (sessions.length > 50) sessions.shift();
    device.session_active = false;
  }

  function setRelay(on, successWhenStopping) {
    const next = !!on;
    if (next === device.relay_on) return;
    device.relay_on = next;

    if (device.relay_on) beginSessionIfNeeded();
    else endSessionIfNeeded(!!successWhenStopping);
  }

  function clearFault() {
    device.fault_latched = false;
    device.fault_code = 0;
    device.trip_ms = 0;
    device.ovc_over_ms = 0;
    device.adc_fail_ms = 0;
    device.last_error = 0;
  }

  function tripFault(code) {
    if (device.fault_latched) return;
    device.fault_latched = true;
    device.fault_code = Number(code) || 0;
    device.trip_ms = Date.now();

    // En latch: l'utilisateur doit appuyer sur On a nouveau.
    // En auto (OVC): on conserve la demande, et on tente un redemarrage.
    if (device.fault_code === 1 && config.ovc_mode === 1) device.desired_on = true;
    else device.desired_on = false;

    setRelay(false, false);
    device.run_until_ms = 0;

    if (device.fault_code === 1) pushEvent(2, 1, "OVC verrouille", "protection");
    else if (device.fault_code === 2) pushEvent(2, 2, "Surchauffe", "protection");
    else if (device.fault_code === 5) pushEvent(2, 5, "Courant perdu", "protection");
    else pushEvent(2, device.fault_code, "Defaut", "protection");
  }

  // Seed sessions visibles dans l'UI
  (() => {
    const now = nowRtcEpochSec();
    sessions.push(
      {
        start_epoch: now - 3600,
        end_epoch: now - 3300,
        duration_s: 300,
        energy_wh: 26.2,
        peak_power_w: 185.6,
        peak_current_a: 14.9,
        success: true
      },
      {
        start_epoch: now - 2400,
        end_epoch: now - 2310,
        duration_s: 90,
        energy_wh: 9.8,
        peak_power_w: 162.1,
        peak_current_a: 12.7,
        success: false
      }
    );
  })();

  // ------------------------------
  // Echantillonnage (historique)
  // ------------------------------
  let simTick = 0;
  let lastSampleTimeMs = Date.now();

  function computePeriodMs() {
    const hz = Number(config.sampling_hz) || 50;
    const safeHz = clampValue(hz, 1, 200);
    return Math.round(1000 / safeHz);
  }

  function simulateNtpIfNeeded(nowMs) {
    if (config.wifi_mode !== 0) return; // uniquement STA
    if (nowMs < device.ntp_next_ms) return;

    device.ntp_next_ms = nowMs + 30000;

    // 75% de chances de reussite
    if (Math.random() < 0.75) {
      rtcEpochBaseSec = Math.floor(Date.now() / 1000);
      rtcSetAtMs = Date.now();
      device.rtc_calibrated = true;
      if (device.last_warning === 6) device.last_warning = 0;
    } else {
      pushEvent(1, 5, "NTP echec", "wifi");
    }
  }

  function simulateSensorGlitches(nowMs) {
    // DS18 (recuperation automatique)
    if (device.ds18_ok && nowMs >= device.ds18_next_glitch_ms) {
      device.ds18_ok = false;
      device.ds18_drop_until_ms = nowMs + rnd(8000, 16000);
      device.ds18_next_glitch_ms = nowMs + rnd(45000, 120000);
      pushEvent(1, 1, "DS18 absent", "capteur");
      pushEvent(1, 4, "Cache utilise", "capteur");
    }
    if (!device.ds18_ok && nowMs >= device.ds18_drop_until_ms) {
      device.ds18_ok = true;
      device.ds18_drop_until_ms = 0;
    }

    // BME (petites coupures)
    if (device.bme_ok && nowMs >= device.bme_next_glitch_ms) {
      device.bme_ok = false;
      device.bme_drop_until_ms = nowMs + rnd(5000, 12000);
      device.bme_next_glitch_ms = nowMs + rnd(70000, 160000);
      pushEvent(1, 2, "BME absent", "capteur");
      pushEvent(1, 4, "Cache utilise", "capteur");
    }
    if (!device.bme_ok && nowMs >= device.bme_drop_until_ms) {
      device.bme_ok = true;
      device.bme_drop_until_ms = 0;
    }
  }

  function generateSample(tsMs, periodMs) {
    simTick += 1;
    const t = simTick * 0.035;
    const dt = periodMs / 1000;

    simulateNtpIfNeeded(tsMs);
    simulateSensorGlitches(tsMs);

    // Chauffe/refroidissement
    const heatRate = device.relay_on ? 0.85 : -1.2;
    device.heat = clampValue(device.heat + dt * heatRate, 0, 120);

    // Courant (avec pics occasionnels pour declencher OVC/ADC)
    const base = device.relay_on ? 10.5 : 0.2;
    let current = base + 4.5 * Math.sin(t * 0.9) + (Math.random() - 0.5) * 0.8;
    if (device.relay_on && Math.random() < 0.006) current += rnd(8, 14);
    current = clampValue(current, 0, 26);

    // ADC: saturation -> warning; trop long -> erreur courant perdu
    if (device.adc_ok && current > 19.6) {
      device.adc_ok = false;
      device.adc_fail_until_ms = tsMs + 2800;
      device.adc_fail_ms = 0;
      pushEvent(1, 3, "ADC sature", "adc");
    }
    if (!device.adc_ok) {
      device.adc_fail_ms += periodMs;
      if (tsMs >= device.adc_fail_until_ms) {
        device.adc_ok = true;
        device.adc_fail_ms = 0;
      }
    }

    // Temperatures "cibles"
    const motorTarget = 28 + device.heat * 0.7 + 2.8 * Math.sin(t * 0.2) + (Math.random() - 0.5) * 0.6;
    const boardTarget = 26 + device.heat * 0.25 + 1.2 * Math.sin(t * 0.08) + (Math.random() - 0.5) * 0.25;
    const ambTarget = boardTarget - 2 + (Math.random() - 0.5) * 0.2;
    const pressure = 101325 + Math.sin(t * 0.04) * 220;

    // Mise a jour mesures (cache)
    if (device.adc_ok) {
      device.last_current = current;
      device.last_power = current * Number(config.motor_vcc_v || 0);
    }

    if (device.ds18_ok) device.motor_c = motorTarget;
    if (device.bme_ok) {
      device.board_c = boardTarget;
      device.ambient_c = ambTarget;
    }

    // Energie + pics session
    if (device.relay_on && !device.fault_latched) {
      device.energy_wh += (device.last_power * dt) / 3600;
      device.session_peak_current_a = Math.max(device.session_peak_current_a, device.last_current);
      device.session_peak_power_w = Math.max(device.session_peak_power_w, device.last_power);
    }

    // Protections (OVC / surchauffe / courant perdu)
    if (device.relay_on && !device.fault_latched) {
      const limitA = Number(config.limit_current_a) || 0;
      if (limitA > 0 && device.last_current > limitA) device.ovc_over_ms += periodMs;
      else device.ovc_over_ms = 0;

      if (device.ovc_over_ms >= (Number(config.ovc_min_ms) || 0)) tripFault(1);

      const motorMax = Number(config.temp_motor_c) || 999;
      const boardMax = Number(config.temp_board_c) || 999;
      if (device.motor_c >= motorMax || device.board_c >= boardMax) tripFault(2);

      if (!device.adc_ok && device.adc_fail_ms >= 2500) tripFault(5);
    }

    device.last_ts_ms = tsMs;

    historySeq += 1;
    history.push({
      seq: historySeq,
      ts_ms: tsMs,
      current_a: device.last_current,
      motor_c: device.motor_c,
      bme_c: device.board_c,
      bme_pa: pressure
    });
    if (history.length > 800) history.shift();
  }

  function seedHistory(count) {
    const periodMs = computePeriodMs();
    const now = Date.now();
    lastSampleTimeMs = now - count * periodMs;
    for (let i = 0; i < count; i += 1) {
      lastSampleTimeMs += periodMs;
      generateSample(lastSampleTimeMs, periodMs);
    }
  }

  function generateSamples(maxOut) {
    const periodMs = computePeriodMs();
    const now = Date.now();
    let count = Math.floor((now - lastSampleTimeMs) / periodMs);
    if (count < 1) count = 1;
    if (count > maxOut) count = maxOut;

    for (let i = 0; i < count; i += 1) {
      lastSampleTimeMs += periodMs;
      generateSample(lastSampleTimeMs, periodMs);
    }
  }

  seedHistory(140);

  function buildStatus() {
    const now = Date.now();

    // Fin du run timer
    if (device.run_until_ms && now >= device.run_until_ms) {
      device.run_until_ms = 0;
      device.desired_on = false;
      setRelay(false, true);
    }

    // Auto recovery (OVC auto)
    if (device.fault_latched && device.fault_code === 1 && config.ovc_mode === 1) {
      const retryMs = Number(config.ovc_retry_ms) || 5000;
      if (now - device.trip_ms >= retryMs) {
        clearFault();
        device.desired_on = true;
      }
    }

    // Auto recovery (overtemp si latch_overtemp=false)
    if (device.fault_latched && device.fault_code === 2 && !config.latch_overtemp) {
      const hyst = Number(config.temp_hyst_c) || 5;
      const motorOk = device.motor_c <= (Number(config.temp_motor_c) || 999) - hyst;
      const boardOk = device.board_c <= (Number(config.temp_board_c) || 999) - hyst;
      if (motorOk && boardOk) clearFault();
    }

    // Etat/relay
    if (device.fault_latched) {
      device.state = "Fault";
      setRelay(false, false);
    } else if (device.desired_on) {
      device.state = "Running";
      setRelay(true, true);
    } else {
      device.state = "Idle";
      setRelay(false, true);
    }

    return {
      seq: historySeq,
      ts_ms: device.last_ts_ms,
      age_ms: now - device.last_ts_ms,
      state: device.state,
      fault_latched: device.fault_latched,
      relay_on: device.relay_on,
      current_a: device.last_current,
      power_w: device.last_power,
      energy_wh: device.energy_wh,
      motor_c: device.motor_c,
      board_c: device.board_c,
      ambient_c: device.ambient_c,
      ds18_ok: device.ds18_ok,
      bme_ok: device.bme_ok,
      adc_ok: device.adc_ok,
      last_warning: device.last_warning,
      last_error: device.last_error
    };
  }

  // ------------------------------
  // HTTP helpers
  // ------------------------------
  function jsonResponse(data, status = 200) {
    return Promise.resolve(
      new Response(JSON.stringify(data), {
        status,
        headers: { "Content-Type": "application/json" }
      })
    );
  }

  function parseBody(init) {
    if (!init || !init.body) return {};
    try {
      return JSON.parse(init.body);
    } catch {
      return {};
    }
  }

  function requireAuthOr401(init) {
    if (isAuthorized(init)) return null;
    // Differencier: header absent -> non autorise / header invalide -> auth echec
    const headers = (init && init.headers) || {};
    const hasAny =
      !!(headers.Authorization || headers.authorization || headers["X-Auth-Token"] || headers["x-auth-token"]);
    pushEvent(1, hasAny ? 7 : 8, hasAny ? "Auth echec" : "Non autorise", "web");
    return jsonResponse({ error: "unauthorized" }, 401);
  }

  function handleConfigPost(body) {
    if (body.limit_current_a !== undefined) config.limit_current_a = Number(body.limit_current_a);
    if (body.ovc_min_ms !== undefined) config.ovc_min_ms = Number(body.ovc_min_ms);
    if (body.ovc_retry_ms !== undefined) config.ovc_retry_ms = Number(body.ovc_retry_ms);
    if (body.temp_motor_c !== undefined) config.temp_motor_c = Number(body.temp_motor_c);
    if (body.temp_board_c !== undefined) config.temp_board_c = Number(body.temp_board_c);
    if (body.temp_ambient_c !== undefined) config.temp_ambient_c = Number(body.temp_ambient_c);
    if (body.temp_hyst_c !== undefined) config.temp_hyst_c = Number(body.temp_hyst_c);
    if (body.latch_overtemp !== undefined) config.latch_overtemp = !!body.latch_overtemp;
    if (body.motor_vcc_v !== undefined) config.motor_vcc_v = Number(body.motor_vcc_v);
    if (body.sampling_hz !== undefined) config.sampling_hz = Number(body.sampling_hz);
    if (body.buzzer_enabled !== undefined) config.buzzer_enabled = !!body.buzzer_enabled;
    if (body.sta_ssid !== undefined) config.sta_ssid = String(body.sta_ssid);
    if (body.sta_pass !== undefined) config.sta_pass = String(body.sta_pass);
    if (body.ap_ssid !== undefined) config.ap_ssid = String(body.ap_ssid);
    if (body.ap_pass !== undefined) config.ap_pass = String(body.ap_pass);

    if (body.ovc_mode !== undefined) {
      if (typeof body.ovc_mode === "string") config.ovc_mode = body.ovc_mode.toLowerCase() === "auto" ? 1 : 0;
      else config.ovc_mode = Number(body.ovc_mode) ? 1 : 0;
    }

    if (body.wifi_mode !== undefined) {
      if (typeof body.wifi_mode === "string") config.wifi_mode = body.wifi_mode.toLowerCase() === "ap" ? 1 : 0;
      else config.wifi_mode = Number(body.wifi_mode) ? 1 : 0;
    }
  }

  function handleControl(body) {
    const action = String(body.action || "").toLowerCase();

    if (action === "start") {
      // Start peut aussi "deverrouiller" si latch
      if (device.fault_latched && config.ovc_mode === 0) clearFault();
      if (device.fault_latched && config.latch_overtemp) clearFault();
      device.desired_on = true;
    } else if (action === "stop") {
      device.desired_on = false;
      device.run_until_ms = 0;
      setRelay(false, true);
    } else if (action === "relay_on") {
      if (device.fault_latched && config.ovc_mode === 0) clearFault();
      device.desired_on = true;
    } else if (action === "relay_off") {
      device.desired_on = false;
      device.run_until_ms = 0;
      setRelay(false, true);
    } else if (action === "clear_fault") {
      clearFault();
      device.desired_on = false;
    }

    return action;
  }

  function handleRunTimer(body) {
    const seconds = Number(body.seconds) || 0;
    if (seconds > 0) {
      if (device.fault_latched) clearFault();
      device.desired_on = true;
      device.run_until_ms = Date.now() + seconds * 1000;
    }
  }

  function handleCalibrate(body) {
    const action = String(body.action || "").toLowerCase();
    if (action === "current_zero") {
      calibration.zero_mv = 2500 + rnd(-25, 25);
    } else if (action === "current_sensitivity") {
      if (body.zero_mv !== undefined) calibration.zero_mv = Number(body.zero_mv);
      if (body.sens_mv_a !== undefined) calibration.sens_mv_a = Number(body.sens_mv_a);
      if (body.input_scale !== undefined) calibration.input_scale = Number(body.input_scale);
    }
  }

  // ------------------------------
  // Interception fetch /api/*
  // ------------------------------
  window.fetch = function mockFetch(input, init = {}) {
    const url = typeof input === "string" ? input : input.url;
    const method = (init.method || (typeof input !== "string" ? input.method : "GET") || "GET").toUpperCase();
    const parsed = new URL(url, window.location.href);

    if (!parsed.pathname.startsWith("/api/")) {
      if (originalFetch) return originalFetch(input, init);
      return Promise.reject(new Error("fetch non disponible"));
    }

    const body = parseBody(init);

    // Toujours generer quelques echantillons pour garder l'UI "vivante".
    generateSamples(3);

    if ((parsed.pathname === "/api/noop" || parsed.pathname === "/api/control") && method === "GET") {
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/info" && method === "GET") {
      const ap = config.wifi_mode === 1;
      return jsonResponse({
        device_id: "MOCK-001",
        device_name: "contro-demo",
        sw: "0.2.0-mock",
        hw: "esp32-s3-mock",
        mdns: "contro.local",
        ip: ap ? "192.168.4.1" : "192.168.1.123"
      });
    }

    if (parsed.pathname === "/api/status" && method === "GET") {
      return jsonResponse(buildStatus());
    }

    if (parsed.pathname === "/api/history" && method === "GET") {
      const since = Number(parsed.searchParams.get("since") || 0);
      const max = clampValue(Number(parsed.searchParams.get("max") || 50), 1, 200);
      generateSamples(max);

      const batch = history.filter((s) => s.seq > since).slice(0, max);
      const seq_end = batch.length ? batch[batch.length - 1].seq : since;
      const samples = batch.map(({ seq, ...rest }) => rest);
      return jsonResponse({ samples, seq_end });
    }

    if (parsed.pathname === "/api/events" && method === "GET") {
      const since = Number(parsed.searchParams.get("since") || 0);
      const max = clampValue(Number(parsed.searchParams.get("max") || 50), 1, 200);
      const batch = eventLog.filter((e) => e.seq > since).slice(0, max);
      const seq_end = batch.length ? batch[batch.length - 1].seq : since;
      return jsonResponse({ events: batch, seq_end });
    }

    if (parsed.pathname === "/api/config" && method === "GET") {
      return jsonResponse({
        limit_current_a: config.limit_current_a,
        ovc_mode: config.ovc_mode,
        ovc_min_ms: config.ovc_min_ms,
        ovc_retry_ms: config.ovc_retry_ms,
        temp_motor_c: config.temp_motor_c,
        temp_board_c: config.temp_board_c,
        temp_ambient_c: config.temp_ambient_c,
        temp_hyst_c: config.temp_hyst_c,
        latch_overtemp: config.latch_overtemp,
        motor_vcc_v: config.motor_vcc_v,
        sampling_hz: config.sampling_hz,
        buzzer_enabled: config.buzzer_enabled,
        wifi_mode: config.wifi_mode,
        sta_ssid: config.sta_ssid,
        ap_ssid: config.ap_ssid
      });
    }

    if (parsed.pathname === "/api/config" && method === "POST") {
      const denied = requireAuthOr401(init);
      if (denied) return denied;
      handleConfigPost(body);
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/control" && method === "POST") {
      const denied = requireAuthOr401(init);
      if (denied) return denied;
      handleControl(body);
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/run_timer" && method === "POST") {
      const denied = requireAuthOr401(init);
      if (denied) return denied;
      handleRunTimer(body);
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/calibrate" && method === "POST") {
      const denied = requireAuthOr401(init);
      if (denied) return denied;
      handleCalibrate(body);
      return jsonResponse({ ok: true, calibration });
    }

    if (parsed.pathname === "/api/rtc" && method === "POST") {
      const denied = requireAuthOr401(init);
      if (denied) return denied;

      const epoch = Number(body.epoch);
      if (Number.isFinite(epoch) && epoch > 0) {
        rtcEpochBaseSec = Math.floor(epoch);
        rtcSetAtMs = Date.now();
        device.rtc_calibrated = true;
        if (device.last_warning === 6) device.last_warning = 0;
      }
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/sessions" && method === "GET") {
      return jsonResponse({ sessions });
    }

    return jsonResponse({ error: "not_found" }, 404);
  };
})();
