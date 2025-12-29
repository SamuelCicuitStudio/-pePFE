// Contro UI - mock local pour tester la page sans backend
(() => {
  "use strict";

  const originalFetch = window.fetch ? window.fetch.bind(window) : null;

  // Stockage auth local pour eviter la redirection login.
  const AUTH_KEY = "contro_auth";
  if (!localStorage.getItem(AUTH_KEY)) {
    localStorage.setItem(
      AUTH_KEY,
      JSON.stringify({ mode: "basic", user: "demo", pass: "demo", token: "" })
    );
  }

  // Etat mock centralise.
  const config = {
    limit_current_a: 18.0,
    ovc_mode: 0,
    ovc_min_ms: 20,
    ovc_retry_ms: 5000,
    temp_motor_c: 85,
    temp_board_c: 70,
    temp_ambient_c: 60,
    temp_hyst_c: 5,
    latch_overtemp: true,
    motor_vcc_v: 12.0,
    sampling_hz: 50,
    buzzer_enabled: true,
    wifi_mode: 0,
    sta_ssid: "demo",
    sta_pass: "",
    ap_ssid: "contro",
    ap_pass: ""
  };

  const device = {
    state: "Idle",
    relay_on: false,
    fault_latched: false,
    last_warning: 0,
    last_error: 0,
    ds18_ok: true,
    bme_ok: true,
    adc_ok: true,
    energy_wh: 0,
    last_current: 0,
    last_power: 0,
    motor_c: 35,
    board_c: 30,
    ambient_c: 28,
    run_until_ms: 0,
    last_ts_ms: Date.now()
  };

  const history = [];
  let historySeq = 0;
  let eventSeq = 0;
  const eventLog = [];
  const nowEpoch = Math.floor(Date.now() / 1000);
  const sessions = [
    {
      start_epoch: nowEpoch - 3600,
      end_epoch: nowEpoch - 3000,
      duration_s: 600,
      energy_wh: 42.5,
      peak_power_w: 180.2,
      peak_current_a: 12.4,
      success: true
    },
    {
      start_epoch: nowEpoch - 2400,
      end_epoch: nowEpoch - 1800,
      duration_s: 600,
      energy_wh: 31.1,
      peak_power_w: 150.7,
      peak_current_a: 10.1,
      success: true
    }
  ];

  // RTC mock : epoch de reference (sec) + moment de synchronisation (ms).
  let rtcEpochBaseSec = nowEpoch;
  let rtcSetAtMs = Date.now();

  let simTick = 0;
  let lastSampleTimeMs = Date.now();

  function clampValue(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  function pushEvent(level, code, message, source) {
    eventSeq += 1;
    eventLog.push({
      seq: eventSeq,
      ts_ms: Date.now(),
      level,
      code,
      message,
      source
    });
    if (eventLog.length > 200) eventLog.shift();
  }

  // Events de demo (1 warning + 1 erreur)
  pushEvent(1, 7, "Auth echec", "mock");
  pushEvent(2, 1, "OVC verrouille", "mock");

  function computePeriodMs() {
    const hz = Number(config.sampling_hz) || 50;
    const safeHz = clampValue(hz, 1, 200);
    return Math.round(1000 / safeHz);
  }

  function generateSample(tsMs, periodMs) {
    simTick += 1;
    const t = simTick * 0.05;
    const load = device.relay_on ? 1 : 0.1;
    const current = (6 + 3 * Math.sin(t * 0.7) + (Math.random() - 0.5) * 0.6) * load;
    const motor = 45 + 12 * Math.sin(t * 0.18) + (Math.random() - 0.5) * 0.8;
    const board = 35 + 4 * Math.sin(t * 0.08) + (Math.random() - 0.5) * 0.3;
    const ambient = board - 2;
    const pressure = 101325 + Math.sin(t * 0.04) * 220;

    const power = current * config.motor_vcc_v;
    if (device.state === "Running") {
      device.energy_wh += (power * (periodMs / 1000)) / 3600;
    }

    device.last_current = current;
    device.last_power = power;
    device.motor_c = motor;
    device.board_c = board;
    device.ambient_c = ambient;
    device.last_ts_ms = tsMs;

    historySeq += 1;
    history.push({
      seq: historySeq,
      ts_ms: tsMs,
      current_a: current,
      motor_c: motor,
      bme_c: board,
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

  seedHistory(120);

  function buildStatus() {
    const now = Date.now();

    if (device.run_until_ms && now >= device.run_until_ms) {
      device.run_until_ms = 0;
      device.relay_on = false;
      device.state = "Idle";
    }

    if (device.fault_latched) {
      device.state = "Fault";
    } else if (device.relay_on) {
      device.state = "Running";
    } else if (device.state !== "Idle") {
      device.state = "Idle";
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
      if (typeof body.ovc_mode === "string") {
        config.ovc_mode = body.ovc_mode.toLowerCase() === "auto" ? 1 : 0;
      } else {
        config.ovc_mode = Number(body.ovc_mode) ? 1 : 0;
      }
    }

    if (body.wifi_mode !== undefined) {
      if (typeof body.wifi_mode === "string") {
        config.wifi_mode = body.wifi_mode.toLowerCase() === "ap" ? 1 : 0;
      } else {
        config.wifi_mode = Number(body.wifi_mode) ? 1 : 0;
      }
    }
  }

  function handleControl(body) {
    const action = String(body.action || "").toLowerCase();
    if (action === "start") {
      device.relay_on = true;
      device.state = "Running";
    } else if (action === "stop") {
      device.relay_on = false;
      device.state = "Idle";
      device.run_until_ms = 0;
    } else if (action === "relay_on") {
      device.relay_on = true;
    } else if (action === "relay_off") {
      device.relay_on = false;
      device.state = "Idle";
    } else if (action === "clear_fault") {
      device.fault_latched = false;
      device.state = "Idle";
    }
    return action;
  }

  function handleRunTimer(body) {
    const seconds = Number(body.seconds) || 0;
    if (seconds > 0) {
      device.relay_on = true;
      device.state = "Running";
      device.run_until_ms = Date.now() + seconds * 1000;
    }
  }

  window.fetch = function mockFetch(input, init = {}) {
    const url = typeof input === "string" ? input : input.url;
    const method = (init.method || (typeof input !== "string" ? input.method : "GET") || "GET").toUpperCase();
    const parsed = new URL(url, window.location.href);

    if (!parsed.pathname.startsWith("/api/")) {
      if (originalFetch) return originalFetch(input, init);
      return Promise.reject(new Error("fetch non disponible"));
    }

    const body = parseBody(init);

    if (parsed.pathname === "/api/info" && method === "GET") {
      return jsonResponse({
        device_id: "MOCK-001",
        device_name: "contro-demo",
        sw: "0.1.0-mock",
        hw: "1.0.0",
        mdns: "contro.local",
        ip: "192.168.4.1"
      });
    }

    if (parsed.pathname === "/api/status" && method === "GET") {
      if (!history.length) generateSamples(10);
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
      handleConfigPost(body);
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/control" && method === "POST") {
      handleControl(body);
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/run_timer" && method === "POST") {
      handleRunTimer(body);
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/calibrate" && method === "POST") {
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/rtc" && method === "POST") {
      const epoch = Number(body.epoch);
      if (Number.isFinite(epoch) && epoch > 0) {
        rtcEpochBaseSec = Math.floor(epoch);
        rtcSetAtMs = Date.now();
      }
      return jsonResponse({ ok: true });
    }

    if (parsed.pathname === "/api/sessions" && method === "GET") {
      return jsonResponse({ sessions });
    }

    return jsonResponse({ error: "not_found" }, 404);
  };
})();
