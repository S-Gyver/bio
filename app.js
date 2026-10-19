/**
 * ESP32 Smart Controller & IoT Dashboard
 * Supports: Supabase Cloud (GitHub Pages) + Direct Local IP + Demo Mode
 */

// --- Application State ---
const state = {
  // Connection Mode: 'supabase' (Cloud) or 'local' (WiFi Direct)
  connMode: localStorage.getItem('esp32_conn_mode') || 'supabase',
  
  // Supabase Credentials (ใช้โปรเจกต์เดิมเป็นค่าเริ่มต้น)
  supabaseUrl: localStorage.getItem('supabase_url') || 'https://igiihteeeprpcxxlldkd.supabase.co',
  supabaseKey: localStorage.getItem('supabase_key') || 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImlnaWlodGVlZXBycGN4eGxsZGtkIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODQ5ODkwNzksImV4cCI6MjEwMDU2NTA3OX0.fr8_ZAYKQ3D-JgEtAWGJnNvKjoUmYxs1T7tjzzsEltw',
  supabaseClient: null,
  realtimeChannel: null,

  // Local IP Config
  espIp: localStorage.getItem('esp32_ip') || '192.168.1.47',
  pollIntervalMs: parseInt(localStorage.getItem('esp32_interval') || '2000', 10),

  // Demo Mode
  isDemoMode: localStorage.getItem('esp32_demo') !== 'false',
  theme: localStorage.getItem('esp32_theme') || 'dark',

  // Status & Telemetry
  isConnected: false,
  isFetching: false,
  lastPingMs: 0,
  lastUpdated: null,

  // Device Telemetry (Device 1)
  relays: [false, false, false, false],
  dht: {
    temp: 28.5,
    hum: 62.0
  },
  color: {
    r: 59,
    g: 130,
    b: 246,
    c: 1200,
    hex: '#3B82F6'
  }
};

let pollTimer = null;
let consecutiveFailures = 0;

// --- DOM Elements ---
const elements = {
  html: document.documentElement,
  themeToggleBtn: document.getElementById('themeToggleBtn'),
  connectionStatusBadge: document.getElementById('connectionStatusBadge'),
  connectionStatusText: document.getElementById('connectionStatusText'),
  lastUpdated: document.getElementById('lastUpdated'),
  pingValue: document.getElementById('pingValue'),
  syncModeLabel: document.getElementById('syncModeLabel'),

  // Tabs & Settings
  tabSupabase: document.getElementById('tabSupabase'),
  tabLocal: document.getElementById('tabLocal'),
  panelSupabase: document.getElementById('panelSupabase'),
  panelLocal: document.getElementById('panelLocal'),
  demoModeToggle: document.getElementById('demoModeToggle'),

  // Supabase Fields
  supabaseUrlInput: document.getElementById('supabaseUrlInput'),
  supabaseKeyInput: document.getElementById('supabaseKeyInput'),
  saveSupabaseBtn: document.getElementById('saveSupabaseBtn'),
  cloudRefreshBtn: document.getElementById('cloudRefreshBtn'),

  // Local IP Fields
  espIpInput: document.getElementById('espIpInput'),
  saveConnectBtn: document.getElementById('saveConnectBtn'),
  manualRefreshBtn: document.getElementById('manualRefreshBtn'),

  // Relays
  masterAllOnBtn: document.getElementById('masterAllOnBtn'),
  masterAllOffBtn: document.getElementById('masterAllOffBtn'),
  relayCards: [
    document.getElementById('relayCard-1'),
    document.getElementById('relayCard-2'),
    document.getElementById('relayCard-3'),
    document.getElementById('relayCard-4')
  ],
  relaySwitches: [
    document.getElementById('relaySwitch-1'),
    document.getElementById('relaySwitch-2'),
    document.getElementById('relaySwitch-3'),
    document.getElementById('relaySwitch-4')
  ],
  relayStatusTexts: [
    document.getElementById('relayStatusText-1'),
    document.getElementById('relayStatusText-2'),
    document.getElementById('relayStatusText-3'),
    document.getElementById('relayStatusText-4')
  ],

  // DHT22
  tempValue: document.getElementById('tempValue'),
  tempStatusBadge: document.getElementById('tempStatusBadge'),
  tempBar: document.getElementById('tempBar'),
  humValue: document.getElementById('humValue'),
  humStatusBadge: document.getElementById('humStatusBadge'),
  humBar: document.getElementById('humBar'),
  randomDhtBtn: document.getElementById('randomDhtBtn'),

  // Color Sensor
  colorPreviewBox: document.getElementById('colorPreviewBox'),
  previewHexBadge: document.getElementById('previewHexBadge'),
  hexCodeDisplay: document.getElementById('hexCodeDisplay'),
  btnHexLabel: document.getElementById('btnHexLabel'),
  colorNameEstimate: document.getElementById('colorNameEstimate'),
  copyHexBtn: document.getElementById('copyHexBtn'),
  randomColorBtn: document.getElementById('randomColorBtn'),
  valRed: document.getElementById('valRed'),
  barRed: document.getElementById('barRed'),
  valGreen: document.getElementById('valGreen'),
  barGreen: document.getElementById('barGreen'),
  valBlue: document.getElementById('valBlue'),
  barBlue: document.getElementById('barBlue'),
  valClear: document.getElementById('valClear'),
  barClear: document.getElementById('barClear'),

  // Toast
  toastContainer: document.getElementById('toastContainer')
};

// --- Initialization ---
function initApp() {
  applyTheme(state.theme);

  // Restore inputs
  elements.supabaseUrlInput.value = state.supabaseUrl;
  elements.supabaseKeyInput.value = state.supabaseKey;
  elements.espIpInput.value = state.espIp;
  elements.demoModeToggle.checked = state.isDemoMode;

  // Set initial Tab
  setConnectionTab(state.connMode);

  // Attach Event Listeners
  setupEventListeners();

  // Render initial UI
  updateConnectionBadge();
  renderRelays();
  renderDHT();
  renderColorSensor();

  // Setup Connection
  if (state.isDemoMode) {
    setupPolling();
    showToast('กำลังใช้งานในโหมดจำลอง (Demo) คุณสามารถทดสอบปุ่มและเซนเซอร์ได้ทันที', 'info', 4000);
  } else if (state.connMode === 'supabase' && state.supabaseUrl && state.supabaseKey) {
    initSupabaseClient();
  } else {
    // If no credentials, stay in Demo Mode
    enableDemoMode();
  }
}

// --- Tabs Management ---
function setConnectionTab(mode) {
  state.connMode = mode;
  localStorage.setItem('esp32_conn_mode', mode);

  if (mode === 'supabase') {
    elements.tabSupabase.classList.add('active');
    elements.tabLocal.classList.remove('active');
    elements.panelSupabase.style.display = 'block';
    elements.panelLocal.style.display = 'none';
    elements.syncModeLabel.textContent = 'Supabase Realtime (WebSockets)';
  } else {
    elements.tabLocal.classList.add('active');
    elements.tabSupabase.classList.remove('active');
    elements.panelLocal.style.display = 'block';
    elements.panelSupabase.style.display = 'none';
    elements.syncModeLabel.textContent = 'HTTP Fetch (Local LAN)';
  }
}

// --- Event Listeners ---
function setupEventListeners() {
  // Theme Toggle
  elements.themeToggleBtn.addEventListener('click', () => {
    const nextTheme = state.theme === 'dark' ? 'light' : 'dark';
    applyTheme(nextTheme);
  });

  // Tab Switching
  elements.tabSupabase.addEventListener('click', () => setConnectionTab('supabase'));
  elements.tabLocal.addEventListener('click', () => setConnectionTab('local'));

  // Demo Mode Switch
  elements.demoModeToggle.addEventListener('change', (e) => {
    state.isDemoMode = e.target.checked;
    localStorage.setItem('esp32_demo', state.isDemoMode);
    vibrate(30);

    if (state.isDemoMode) {
      enableDemoMode();
    } else {
      if (state.connMode === 'supabase') {
        if (!state.supabaseUrl || !state.supabaseKey) {
          showToast('กรุณากรอก Supabase URL และ Anon Key ก่อนเชื่อมต่อ', 'error');
          enableDemoMode();
          return;
        }
        initSupabaseClient();
      } else {
        showToast(`กำลังเชื่อมต่อ ESP32 Local: ${state.espIp}`, 'info');
        fetchLocalData();
      }
    }
  });

  // Clicking Status Badge when disconnected will activate Demo Mode
  elements.connectionStatusBadge.addEventListener('click', () => {
    if (!state.isDemoMode && !state.isConnected) {
      enableDemoMode();
    }
  });

  // Save Supabase & Connect
  elements.saveSupabaseBtn.addEventListener('click', async () => {
    const url = elements.supabaseUrlInput.value.trim().replace(/\/$/, '');
    const key = elements.supabaseKeyInput.value.trim();

    if (!url || !key) {
      showToast('กรุณากรอกทั้ง Supabase URL และ Anon Public Key', 'error');
      return;
    }

    state.supabaseUrl = url;
    state.supabaseKey = key;
    localStorage.setItem('supabase_url', url);
    localStorage.setItem('supabase_key', key);
    vibrate(40);

    // Visual button feedback
    const originalHtml = elements.saveSupabaseBtn.innerHTML;
    elements.saveSupabaseBtn.disabled = true;
    elements.saveSupabaseBtn.innerHTML = `<span>กำลังเชื่อมต่อ Cloud...</span>`;

    const success = await initSupabaseClient();
    elements.saveSupabaseBtn.disabled = false;
    elements.saveSupabaseBtn.innerHTML = originalHtml;

    if (success) {
      state.isDemoMode = false;
      elements.demoModeToggle.checked = false;
      localStorage.setItem('esp32_demo', 'false');
      showToast('✅ เชื่อมต่อ Supabase Cloud สำเร็จเรียบร้อยแล้ว!', 'success');
    }
  });

  // Cloud Refresh Button
  elements.cloudRefreshBtn.addEventListener('click', async () => {
    vibrate(25);
    const icon = elements.cloudRefreshBtn.querySelector('.refresh-icon');
    if (icon) icon.classList.add('spinning');
    await fetchSupabaseData();
    if (icon) icon.classList.remove('spinning');
  });

  // Local Save IP & Test Connection
  elements.saveConnectBtn.addEventListener('click', async () => {
    let rawIp = elements.espIpInput.value.trim().replace(/^https?:\/\//i, '').replace(/\/.*$/, '');
    if (!rawIp) {
      showToast('กรุณาระบุ IP Address ของ ESP32', 'error');
      return;
    }
    state.espIp = rawIp;
    localStorage.setItem('esp32_ip', rawIp);
    vibrate(40);

    const originalBtnHtml = elements.saveConnectBtn.innerHTML;
    elements.saveConnectBtn.disabled = true;
    elements.saveConnectBtn.innerHTML = `<span>กำลังทดสอบ...</span>`;

    try {
      const signal = (typeof AbortSignal !== 'undefined' && 'timeout' in AbortSignal)
        ? AbortSignal.timeout(2000)
        : null;

      const res = await fetch(`http://${state.espIp}/api/data`, {
        method: 'GET',
        signal: signal,
        headers: { 'Accept': 'application/json' },
        cache: 'no-store'
      });

      if (res.ok) {
        const json = await res.json();
        processLocalApiData(json);
        state.isDemoMode = false;
        elements.demoModeToggle.checked = false;
        localStorage.setItem('esp32_demo', 'false');
        consecutiveFailures = 0;
        state.isConnected = true;
        updateConnectionBadge('connected');
        showToast(`✅ เชื่อมต่อกับ ESP32 (${state.espIp}) สำเร็จแล้ว!`, 'success');
      } else {
        throw new Error('Response not OK');
      }
    } catch (e) {
      enableDemoMode();
      showToast(`⚠️ ไม่พบ ESP32 ที่ IP: ${state.espIp} — ระบบยังคงเปิดโหมดจำลอง (Demo Mode) ให้คุณทดสอบต่อได้`, 'info', 6000);
    } finally {
      elements.saveConnectBtn.disabled = false;
      elements.saveConnectBtn.innerHTML = originalBtnHtml;
    }
  });

  // Local Refresh Button
  elements.manualRefreshBtn.addEventListener('click', () => {
    vibrate(25);
    const icon = elements.manualRefreshBtn.querySelector('.refresh-icon');
    if (icon) icon.classList.add('spinning');
    if (state.isDemoMode) {
      simulateRandomFluctuation();
      renderDHT();
      renderColorSensor();
      updateLastUpdatedTime();
      setTimeout(() => { if (icon) icon.classList.remove('spinning'); }, 200);
    } else {
      fetchLocalData().finally(() => { if (icon) icon.classList.remove('spinning'); });
    }
  });

  // Individual Relay Switches
  elements.relaySwitches.forEach((toggleEl, idx) => {
    toggleEl.addEventListener('change', (e) => {
      const channel = idx + 1;
      const targetState = e.target.checked;
      vibrate(40);
      handleRelayToggle(channel, targetState);
    });
  });

  // Master Buttons
  elements.masterAllOnBtn.addEventListener('click', () => {
    vibrate([40, 30, 40]);
    handleMasterAll(true);
  });

  elements.masterAllOffBtn.addEventListener('click', () => {
    vibrate([40, 30, 40]);
    handleMasterAll(false);
  });

  // Copy HEX Color Code
  elements.copyHexBtn.addEventListener('click', copyHexToClipboard);

  // Random Color Button (Demo)
  elements.randomColorBtn.addEventListener('click', () => {
    vibrate(30);
    randomizeColor();
    showToast(`สุ่มสีใหม่: ${state.color.hex}`, 'info');
  });

  // Random/Test DHT22 Temperature & Humidity Button
  if (elements.randomDhtBtn) {
    elements.randomDhtBtn.addEventListener('click', async () => {
      vibrate(30);
      const newT = parseFloat((25.0 + Math.random() * 8.0).toFixed(1));
      const newH = parseFloat((50.0 + Math.random() * 30.0).toFixed(1));

      if (state.isDemoMode) {
        state.dht.temp = newT;
        state.dht.hum = newH;
        renderDHT();
        updateLastUpdatedTime();
        showToast(`อัปเดตอุณหภูมิ: ${newT} °C | ความชื้น: ${newH} %`, 'info');
      } else if (state.connMode === 'supabase' && state.supabaseClient) {
        showToast(`กำลังส่งค่าอุณหภูมิ ${newT} °C ไปยัง Supabase...`, 'info');
        try {
          const { error } = await state.supabaseClient
            .from('esp32_devices')
            .update({ temperature: newT, humidity: newH, updated_at: new Date().toISOString() })
            .eq('id', 1);

          if (!error) {
            showToast(`✅ อัปเดตไปยัง Cloud สำเร็จ: ${newT} °C`, 'success');
          }
        } catch (e) {
          console.error(e);
        }
      }
    });
  }
}

// --- Supabase Cloud Client & Realtime Subscription ---
async function initSupabaseClient() {
  if (typeof window.supabase === 'undefined' || !window.supabase.createClient) {
    showToast('กำลังโหลด Supabase SDK...', 'info');
    return false;
  }

  try {
    state.supabaseClient = window.supabase.createClient(state.supabaseUrl, state.supabaseKey);

    // Initial Fetch from Table
    const { data, error } = await state.supabaseClient
      .from('esp32_devices')
      .select('*')
      .eq('id', 1)
      .single();

    if (error) {
      console.error('Supabase query error:', error);
      showToast(`เชื่อมต่อ Supabase ไม่สำเร็จ: ${error.message}`, 'error', 6000);
      enableDemoMode();
      return false;
    }

    if (data) {
      applyDeviceDataFromSupabase(data);
      state.isConnected = true;
      updateConnectionBadge('cloud');
      elements.pingValue.textContent = 'Realtime (WSS)';
    }

    // Subscribe to Realtime Postgres Changes
    if (state.realtimeChannel) {
      state.supabaseClient.removeChannel(state.realtimeChannel);
    }

    state.realtimeChannel = state.supabaseClient
      .channel('esp32-realtime-room')
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'esp32_devices', filter: 'id=eq.1' },
        (payload) => {
          if (payload.new) {
            applyDeviceDataFromSupabase(payload.new);
          }
        }
      )
      .subscribe((status) => {
        if (status === 'SUBSCRIBED') {
          updateConnectionBadge('cloud');
          elements.pingValue.textContent = 'Realtime (WSS)';
        }
      });

    return true;
  } catch (err) {
    console.error('Init Supabase Client failed:', err);
    showToast('เกิดข้อผิดพลาดในการเริ่มต้น Supabase Client', 'error');
    enableDemoMode();
    return false;
  }
}

async function fetchSupabaseData() {
  if (!state.supabaseClient) return;
  try {
    const { data, error } = await state.supabaseClient
      .from('esp32_devices')
      .select('*')
      .eq('id', 1)
      .single();

    if (data && !error) {
      applyDeviceDataFromSupabase(data);
      showToast('อัปเดตข้อมูลจาก Supabase Cloud เรียบร้อย', 'info');
    }
  } catch (err) {
    console.warn('Cloud fetch error:', err);
  }
}

function applyDeviceDataFromSupabase(dev) {
  state.relays = [
    Boolean(dev.relay1),
    Boolean(dev.relay2),
    Boolean(dev.relay3),
    Boolean(dev.relay4)
  ];
  renderRelays();

  if (typeof dev.temperature === 'number') state.dht.temp = Number(dev.temperature);
  if (typeof dev.humidity === 'number') state.dht.hum = Number(dev.humidity);
  renderDHT();

  if (dev.color_r !== undefined) {
    state.color.r = dev.color_r;
    state.color.g = dev.color_g;
    state.color.b = dev.color_b;
    state.color.c = dev.color_c || 1200;
    state.color.hex = dev.color_hex || rgbToHex(dev.color_r, dev.color_g, dev.color_b);
    renderColorSensor();
  }

  updateLastUpdatedTime();
}

// --- Relay Control (Handles Cloud, Local & Demo) ---
async function handleRelayToggle(channel, targetState) {
  const index = channel - 1;
  const previousState = state.relays[index];

  // Optimistic UI Update
  state.relays[index] = targetState;
  renderRelays();

  if (state.isDemoMode) {
    showToast(`Relay ${channel}: ${targetState ? 'เปิด (ON)' : 'ปิด (OFF)'}`, targetState ? 'success' : 'info');
    return;
  }

  // If in Supabase Cloud Mode
  if (state.connMode === 'supabase' && state.supabaseClient) {
    try {
      const fieldName = `relay${channel}`;
      const { error } = await state.supabaseClient
        .from('esp32_devices')
        .update({ [fieldName]: targetState, updated_at: new Date().toISOString() })
        .eq('id', 1);

      if (error) throw error;
      showToast(`[Cloud] Relay ${channel}: ${targetState ? 'เปิด (ON)' : 'ปิด (OFF)'}`, 'success');
    } catch (err) {
      console.error('Supabase update failed:', err);
      state.relays[index] = previousState;
      renderRelays();
      showToast(`อัปเดตสถานะ Relay ${channel} ไปยัง Cloud ไม่สำเร็จ`, 'error');
    }
    return;
  }

  // If in Local IP Mode
  try {
    const url = `http://${state.espIp}/toggle?ch=${channel}&state=${targetState ? 1 : 0}`;
    const signal = (typeof AbortSignal !== 'undefined' && 'timeout' in AbortSignal)
      ? AbortSignal.timeout(2500)
      : null;

    const res = await fetch(url, { method: 'GET', signal });
    if (!res.ok) throw new Error('Switch request rejected');
    showToast(`Relay ${channel}: ${targetState ? 'เปิด (ON)' : 'ปิด (OFF)'}`, 'success');
  } catch (err) {
    state.relays[index] = previousState;
    renderRelays();
    showToast(`สั่งงาน Relay ${channel} ไม่สำเร็จ (ตรวจสอบบอร์ด)`, 'error');
  }
}

async function handleMasterAll(targetState) {
  state.relays = [targetState, targetState, targetState, targetState];
  renderRelays();

  if (state.isDemoMode) {
    showToast(`สั่ง${targetState ? 'เปิด' : 'ปิด'}ทั้งหมด 4 ช่อง เรียบร้อย`, targetState ? 'success' : 'info');
    return;
  }

  // Cloud Mode
  if (state.connMode === 'supabase' && state.supabaseClient) {
    try {
      const { error } = await state.supabaseClient
        .from('esp32_devices')
        .update({
          relay1: targetState,
          relay2: targetState,
          relay3: targetState,
          relay4: targetState,
          updated_at: new Date().toISOString()
        })
        .eq('id', 1);

      if (error) throw error;
      showToast(`[Cloud] สั่ง${targetState ? 'เปิด' : 'ปิด'}รีเลย์ทั้งหมดเรียบร้อยแล้ว`, 'success');
    } catch (err) {
      showToast('สั่งงานผ่าน Cloud ไม่สำเร็จ', 'error');
    }
    return;
  }

  // Local Mode
  try {
    await fetch(`http://${state.espIp}/master?state=${targetState ? 1 : 0}`);
    showToast(`สั่ง${targetState ? 'เปิด' : 'ปิด'}รีเลย์ทั้งหมดเรียบร้อยแล้ว`, 'success');
  } catch (err) {
    showToast('สั่งงานไปยังบอร์ดไม่สำเร็จ', 'error');
  }
}

// --- Local IP Fetch Operations ---
async function fetchLocalData() {
  if (state.isDemoMode || state.isFetching) return;
  state.isFetching = true;
  updateConnectionBadge('fetching');

  const startTime = performance.now();
  try {
    const signal = (typeof AbortSignal !== 'undefined' && 'timeout' in AbortSignal)
      ? AbortSignal.timeout(2500)
      : null;

    const res = await fetch(`http://${state.espIp}/api/data`, {
      method: 'GET',
      signal: signal,
      headers: { 'Accept': 'application/json' },
      cache: 'no-store'
    });

    const latency = Math.round(performance.now() - startTime);
    state.lastPingMs = latency;
    elements.pingValue.textContent = `${latency} ms`;

    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const json = await res.json();
    processLocalApiData(json);

    consecutiveFailures = 0;
    state.isConnected = true;
    updateConnectionBadge('connected');
    updateLastUpdatedTime();
  } catch (err) {
    consecutiveFailures++;
    state.isConnected = false;
    updateConnectionBadge('disconnected');
    elements.pingValue.textContent = 'Offline';

    if (consecutiveFailures >= 2) {
      showToast(`⚠️ เชื่อมต่อ ${state.espIp} ไม่สำเร็จ คลิกที่นี่เพื่อเปิดโหมดจำลอง`, 'error', 7000, enableDemoMode);
    }
  } finally {
    state.isFetching = false;
  }
}

function processLocalApiData(data) {
  if (Array.isArray(data.relays)) {
    state.relays = data.relays.map(v => Boolean(v));
    renderRelays();
  }
  if (data.dht) {
    if (typeof data.dht.temp === 'number') state.dht.temp = data.dht.temp;
    if (typeof data.dht.hum === 'number') state.dht.hum = data.dht.hum;
    renderDHT();
  }
  if (data.color) {
    state.color.r = data.color.r ?? state.color.r;
    state.color.g = data.color.g ?? state.color.g;
    state.color.b = data.color.b ?? state.color.b;
    state.color.c = data.color.c ?? state.color.c;
    state.color.hex = data.color.hex || rgbToHex(state.color.r, state.color.g, state.color.b);
    renderColorSensor();
  }
}

// --- Render Functions ---
function renderRelays() {
  state.relays.forEach((isOn, idx) => {
    const card = elements.relayCards[idx];
    const sw = elements.relaySwitches[idx];
    const txt = elements.relayStatusTexts[idx];

    if (!card || !sw || !txt) return;

    sw.checked = isOn;
    if (isOn) {
      card.classList.add('is-on');
      txt.textContent = 'ON';
    } else {
      card.classList.remove('is-on');
      txt.textContent = 'OFF';
    }
  });
}

function renderDHT() {
  const t = state.dht.temp;
  const h = state.dht.hum;

  elements.tempValue.textContent = t.toFixed(1);
  elements.humValue.textContent = h.toFixed(1);

  const tempPercent = Math.min(Math.max((t / 50) * 100, 0), 100);
  elements.tempBar.style.width = `${tempPercent}%`;

  if (t < 22) {
    elements.tempStatusBadge.textContent = 'เย็นสบาย (Cool)';
    elements.tempStatusBadge.className = 'sensor-status-badge badge-hum-normal';
  } else if (t <= 31) {
    elements.tempStatusBadge.textContent = 'สบายตัว (Comfort)';
    elements.tempStatusBadge.className = 'sensor-status-badge badge-temp-warm';
  } else {
    elements.tempStatusBadge.textContent = 'อากาศร้อน (Warm)';
    elements.tempStatusBadge.className = 'sensor-status-badge badge-danger';
  }

  const humPercent = Math.min(Math.max(h, 0), 100);
  elements.humBar.style.width = `${humPercent}%`;

  if (h < 40) elements.humStatusBadge.textContent = 'อากาศแห้ง (Dry)';
  else if (h <= 70) elements.humStatusBadge.textContent = 'เหมาะสม (Normal)';
  else elements.humStatusBadge.textContent = 'ความชื้นสูง (High)';
}

function renderColorSensor() {
  const { r, g, b, c, hex } = state.color;

  elements.colorPreviewBox.style.backgroundColor = hex;
  elements.colorPreviewBox.style.boxShadow = `0 12px 30px ${hex}55`;

  elements.previewHexBadge.textContent = hex.toUpperCase();
  elements.hexCodeDisplay.textContent = hex.toUpperCase();
  elements.btnHexLabel.textContent = hex.toUpperCase();

  elements.valRed.textContent = r;
  elements.barRed.style.width = `${(r / 255) * 100}%`;

  elements.valGreen.textContent = g;
  elements.barGreen.style.width = `${(g / 255) * 100}%`;

  elements.valBlue.textContent = b;
  elements.barBlue.style.width = `${(b / 255) * 100}%`;

  elements.valClear.textContent = c.toLocaleString();
  elements.barClear.style.width = `${Math.min((c / 3000) * 100, 100)}%`;

  elements.colorNameEstimate.textContent = estimateColorName(r, g, b);
}

// --- Helpers: Color Calculation ---
function rgbToHex(r, g, b) {
  const toHex = (n) => Math.min(255, Math.max(0, Math.round(n))).toString(16).padStart(2, '0');
  return `#${toHex(r)}${toHex(g)}${toHex(b)}`.toUpperCase();
}

function estimateColorName(r, g, b) {
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);

  if (max - min < 25) {
    if (max > 210) return 'สีขาว / White';
    if (max < 50) return 'สีดำ / Black';
    return 'สีเทา / Gray';
  }

  if (r > 180 && g < 90 && b < 90) return 'สีแดง / Red';
  if (r > 200 && g > 110 && b < 60) return 'สีส้ม / Orange';
  if (r > 190 && g > 190 && b < 100) return 'สีเหลือง / Yellow';
  if (g > 150 && r < 120 && b < 140) return 'สีเขียว / Green';
  if (b > 180 && r < 110 && g > 130) return 'สีฟ้า / Cyan';
  if (b > 170 && r < 130 && g < 130) return 'สีน้ำเงิน / Blue';
  if (r > 140 && b > 140 && g < 110) return 'สีม่วง / Purple';
  if (r > 200 && b > 140 && g < 130) return 'สีชมพู / Pink';

  return 'เฉดสีผสม / Mixed';
}

function copyHexToClipboard() {
  const hex = state.color.hex.toUpperCase();
  navigator.clipboard.writeText(hex).then(() => {
    vibrate(30);
    showToast(`คัดลอกรหัสสี ${hex} เรียบร้อยแล้ว`, 'success');
  }).catch(() => {
    showToast(`ไม่สามารถคัดลอกได้: ${hex}`, 'error');
  });
}

function randomizeColor() {
  const r = Math.floor(Math.random() * 256);
  const g = Math.floor(Math.random() * 256);
  const b = Math.floor(Math.random() * 256);
  const c = Math.floor(Math.random() * 2000) + 400;

  state.color = { r, g, b, c, hex: rgbToHex(r, g, b) };
  renderColorSensor();
}

// --- Simulation Engine for Demo Mode ---
function simulateRandomFluctuation() {
  const tDelta = (Math.random() - 0.5) * 0.8;
  state.dht.temp = parseFloat(Math.min(Math.max(state.dht.temp + tDelta, 25.0), 34.0).toFixed(1));

  const hDelta = (Math.random() - 0.5) * 1.6;
  state.dht.hum = parseFloat(Math.min(Math.max(state.dht.hum + hDelta, 45.0), 85.0).toFixed(1));

  state.color.c = Math.max(200, state.color.c + Math.floor((Math.random() - 0.5) * 50));
}

function setupPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(() => {
    if (state.isDemoMode) {
      simulateRandomFluctuation();
      renderDHT();
      updateLastUpdatedTime();
    } else if (state.connMode === 'local') {
      fetchLocalData();
    }
  }, 2000);
}

// Helper to switch cleanly into Demo Mode
function enableDemoMode() {
  state.isDemoMode = true;
  elements.demoModeToggle.checked = true;
  localStorage.setItem('esp32_demo', 'true');
  consecutiveFailures = 0;
  state.isConnected = true;
  updateConnectionBadge('demo');
  setupPolling(); // Ensure polling interval is running
  simulateRandomFluctuation();
  renderDHT();
  renderColorSensor();
  updateLastUpdatedTime();

  try {
    console.clear();
    console.info('%c[ESP32 Smart Controller] โหมดจำลอง (Demo Mode) เปิดใช้งานเรียบร้อยแล้ว', 'color: #10b981; font-weight: bold;');
  } catch (e) {}
}

// --- UI Indicators & Timestamp ---
function updateConnectionBadge(overrideStatus) {
  const badge = elements.connectionStatusBadge;
  const text = elements.connectionStatusText;

  badge.className = 'status-pill';

  if (state.isDemoMode) {
    badge.classList.add('status-demo');
    text.textContent = 'โหมดจำลอง (Demo)';
    elements.pingValue.textContent = '0 ms (Local)';
    return;
  }

  const status = overrideStatus || (state.isConnected ? 'connected' : 'disconnected');

  if (status === 'cloud' || status === 'connected') {
    badge.classList.add('status-connected');
    text.textContent = state.connMode === 'supabase' ? 'Cloud เชื่อมต่อแล้ว (Online)' : 'เชื่อมต่อ IP แล้ว';
  } else if (status === 'fetching') {
    badge.classList.add('status-fetching');
    text.textContent = 'กำลังดึงข้อมูล...';
  } else {
    badge.classList.add('status-disconnected');
    text.textContent = 'ไม่พบการเชื่อมต่อ';
  }
}

function updateLastUpdatedTime() {
  const now = new Date();
  const timeStr = now.toLocaleTimeString('th-TH', { hour12: false });
  elements.lastUpdated.textContent = `อัปเดตล่าสุด: ${timeStr}`;
}

// --- Theme Management ---
function applyTheme(theme) {
  state.theme = theme;
  elements.html.setAttribute('data-theme', theme);
  localStorage.setItem('esp32_theme', theme);
}

// --- Haptic Feedback Helper ---
function vibrate(pattern) {
  if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
    try { navigator.vibrate(pattern); } catch (e) {}
  }
}

// --- Toast Notifications ---
function showToast(message, type = 'info', duration = 3000, onClick = null) {
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;
  toast.textContent = message;

  if (typeof onClick === 'function') {
    toast.style.cursor = 'pointer';
    toast.title = 'คลิกเพื่อดำเนินการ';
    toast.addEventListener('click', () => {
      onClick();
      toast.remove();
    });
  }

  elements.toastContainer.appendChild(toast);

  setTimeout(() => {
    toast.classList.add('toast-out');
    toast.addEventListener('animationend', () => {
      toast.remove();
    });
  }, duration);
}

// Start application when DOM is ready
document.addEventListener('DOMContentLoaded', initApp);
