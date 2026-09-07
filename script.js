import { db, authReady } from './firebase-config.js?v=20260906';
import { onValue, ref } from 'https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js';

const state = { temperature: 24.8, humidity: 61.2, ammonia: 8.4, readings: [] };
const history = { temperature: [], humidity: [], ammonia: [] };
const chartLabels = [];
const telemetryCharts = {};
const ESP32_TIMEOUT_MS = 2 * 60 * 1000;
let latestEsp32ReadingAt = 0;
let lastFarmStatus = '';
const $ = (selector) => document.querySelector(selector);
const numeric = (value, fallback) => Number.isFinite(Number(value)) ? Number(value) : fallback;
const timestampValue = (value) => value?.toDate ? value.toDate() : value?.seconds ? new Date(value.seconds * 1000) : typeof value === 'number' ? new Date(value < 1e12 ? value * 1000 : value) : new Date(value || Date.now());
const formatTime = (date = new Date()) => date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });

function setFirebaseStatus(message, online) {
  $('#firebaseConnectionLabel').textContent = online ? 'Firebase connected' : 'Firebase offline';
  $('#firebaseStatus').textContent = message;
  $('.device-status .pulse-dot').style.background = online ? '#51d88c' : '#d95a52';
}

function updateEsp32Status() {
  const online = latestEsp32ReadingAt > 0 && Date.now() - latestEsp32ReadingAt <= ESP32_TIMEOUT_MS;
  $('#systemConnectionLabel').textContent = online ? 'System online' : 'System offline';
  $('#lastSync').textContent = online ? `Synced ${formatTime(new Date(latestEsp32ReadingAt))}` : 'Waiting for ESP32 data';
  $('.online-dot').style.background = online ? '#51d88c' : '#d95a52';
}

function chartOptions(yMax) {
  return { responsive: true, maintainAspectRatio: false, interaction: { intersect: false, mode: 'index' }, plugins: { legend: { display: false }, tooltip: { backgroundColor: '#17372d', padding: 10 } }, scales: { x: { grid: { display: false }, ticks: { color: '#a1ada6', font: { size: 9 }, maxTicksLimit: 6 } }, y: { min: 0, max: yMax, grid: { color: '#edf2ef' }, ticks: { color: '#a1ada6', font: { size: 9 }, stepSize: yMax / 4 } } } };
}

function buildChart(id, datasets, yMax) {
  if (!window.Chart) return;
  telemetryCharts[id] = new Chart($(`#${id}`).getContext('2d'), { type: 'line', data: { labels: chartLabels, datasets }, options: chartOptions(yMax) });
}

function buildTelemetryCharts() {
  const definitions = [
    ['temperatureChart', [{ label: 'Temperature', data: history.temperature, borderColor: '#46a879', backgroundColor: 'rgba(70, 168, 121, .14)', fill: true }], 40],
    ['humidityChart', [{ label: 'Humidity', data: history.humidity, borderColor: '#54a5b8', backgroundColor: 'rgba(84, 165, 184, .14)', fill: true }], 100],
    ['ammoniaChart', [{ label: 'Ammonia', data: history.ammonia, borderColor: '#d3a146', backgroundColor: 'rgba(211, 161, 70, .14)', fill: true }], 40],
    ['combinedChart', [
      { label: 'Temperature', data: history.temperature, borderColor: '#46a879', fill: false },
      { label: 'Humidity', data: history.humidity, borderColor: '#54a5b8', fill: false },
      { label: 'Ammonia', data: history.ammonia, borderColor: '#d3a146', fill: false }
    ], 100]
  ];
  definitions.forEach(([id, datasets, yMax]) => buildChart(id, datasets.map((dataset) => ({ ...dataset, tension: .42, borderWidth: 2, pointRadius: 0 })), yMax));
}

function updateSensor(id, value) {
  $(`#${id}Value`).textContent = value.toFixed(1);
  const stateElement = $(`#${id}State`);
  const safe = id === 'ammonia' ? value < 20 : id === 'humidity' ? value >= 50 && value <= 70 : value >= 20 && value <= 28;
  stateElement.className = `state-pill ${safe ? 'good' : 'warning'}`;
  stateElement.innerHTML = `<i class="fa-solid ${safe ? 'fa-circle-check' : 'fa-triangle-exclamation'}"></i> ${safe ? (id === 'ammonia' ? 'Safe level' : 'Optimal range') : 'Needs attention'}`;
}

function classifyFarm(reportedStatus = '') {
  const dangerous = state.ammonia >= 20 || state.temperature >= 32 || state.temperature <= 15;
  const suboptimal = state.ammonia >= 12 || state.humidity >= 72 || state.humidity <= 42 || state.temperature >= 28;
  const calculatedStatus = dangerous ? 'Dangerous' : suboptimal ? 'Suboptimal' : 'Optimal';
  const status = ['Optimal', 'Suboptimal', 'Dangerous'].includes(reportedStatus) ? reportedStatus : calculatedStatus;
  if (status === lastFarmStatus) return;
  lastFarmStatus = status;
  $('.status-dot').className = `status-dot ${status.toLowerCase()}`;
  $('.status-card').className = `status-card ${status.toLowerCase()}`;
  $('#farmStatus').textContent = status;
  $('#statusSummary').textContent = status === 'Optimal' ? 'All environmental conditions are within safe limits.' : status === 'Suboptimal' ? 'Conditions need attention to maintain a healthy flock.' : 'Immediate action required. Review the latest farm data.';
}

function renderReadings() {
  $('#readingsBody').innerHTML = state.readings.slice(0, 8).map((reading) => `<tr><td>${reading.timestamp}</td><td>${reading.temperature.toFixed(1)}°C</td><td>${reading.humidity.toFixed(1)}%</td><td>${reading.ammonia.toFixed(1)} PPM</td><td><strong>${reading.ai || 'Optimal'}</strong></td></tr>`).join('');
}

function applyReading(data, timestamp = new Date()) {
  state.temperature = numeric(data.temperature ?? data.temp, state.temperature);
  state.humidity = numeric(data.humidity ?? data.rh, state.humidity);
  state.ammonia = numeric(data.ammonia ?? data.nh3, state.ammonia);
  updateSensor('temperature', state.temperature); updateSensor('humidity', state.humidity); updateSensor('ammonia', state.ammonia); classifyFarm(data.state);
  const now = timestampValue(timestamp);
  $('#currentDate').textContent = now.toLocaleDateString([], { weekday: 'long', month: 'long', day: 'numeric', year: 'numeric' });
  $('#currentTime').textContent = formatTime(now); $('#checkedTime').textContent = formatTime(now);
  history.temperature.push(state.temperature); history.humidity.push(state.humidity); history.ammonia.push(state.ammonia); chartLabels.push(now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }));
  Object.values(history).forEach((series) => { while (series.length > 12) series.shift(); }); while (chartLabels.length > 12) chartLabels.shift();
  Object.values(telemetryCharts).forEach((chart) => { chart.data.labels = chartLabels; chart.update('none'); });
}

function showToast(message) { const toast = $('#toast'); toast.textContent = message; toast.classList.add('show'); window.clearTimeout(showToast.timeout); showToast.timeout = window.setTimeout(() => toast.classList.remove('show'), 2400); }

async function subscribeToFirebase() {
  try {
    await authReady;
    onValue(ref(db, 'pfms'), (snapshot) => {
      const data = snapshot.val() || {};
      const current = data.current || null;
      const historyRows = Object.entries(data.history || {}).map(([id, reading]) => ({ id, ...reading }));
      const seenReadings = new Set();
      const rows = (current ? [{ id: 'current', ...current }, ...historyRows] : historyRows)
        .sort((a, b) => timestampValue(b.timestamp) - timestampValue(a.timestamp))
        .filter((row) => {
          const timestamp = timestampValue(row.timestamp).getTime();
          const payloadKey = [row.temperature, row.humidity, row.ammonia, row.state].join('|');
          if (seenReadings.has(timestamp) || seenReadings.has(payloadKey)) return false;
          seenReadings.add(timestamp);
          seenReadings.add(payloadKey);
          return true;
        });
      state.readings = rows.slice(0, 8).map((row) => ({ timestamp: formatTime(timestampValue(row.timestamp)), temperature: numeric(row.temperature, state.temperature), humidity: numeric(row.humidity, state.humidity), ammonia: numeric(row.ammonia, state.ammonia), ai: row.state || 'Optimal' }));
      if (current) {
        const latestTimestamp = current.timestamp ? timestampValue(current.timestamp) : null;
        latestEsp32ReadingAt = latestTimestamp?.getTime() || 0;
        applyReading(current, latestTimestamp || Date.now());
      }
      renderReadings(); setFirebaseStatus('Live data connected', true); updateEsp32Status();
    }, (error) => { setFirebaseStatus('Realtime Database unavailable', false); updateEsp32Status(); showToast('Unable to read Firebase data'); console.error(error); });

    onValue(ref(db, 'pfms/actuators'), (snapshot) => Object.entries(snapshot.val() || {}).forEach(([id, data]) => {
      const toggle = [...document.querySelectorAll('.toggle')].find((button) => button.dataset.actuator.toLowerCase() === id.toLowerCase());
      if (!toggle) return;
      const isOn = typeof data === 'boolean' ? data : Boolean(data.enabled ?? data.on ?? data.active ?? data.status === 'ON');
      toggle.classList.toggle('is-on', isOn); toggle.querySelector('b').textContent = isOn ? 'ON' : 'OFF';
    }), (error) => console.error('Actuator data unavailable.', error));
  } catch (error) {
    const message = error?.code === 'auth/operation-not-allowed' ? 'Enable Anonymous sign-in in Firebase Console' : 'Authentication unavailable';
    setFirebaseStatus(message, false); updateEsp32Status(); showToast('Unable to connect to Firebase');
  }
}

function setupInteractions() {
  $('#menuButton').addEventListener('click', () => $('#sidebar').classList.toggle('open'));
  document.querySelectorAll('.nav-item').forEach((item) => item.addEventListener('click', () => $('#sidebar').classList.remove('open')));
  document.querySelectorAll('.toggle').forEach((toggle) => { toggle.disabled = true; toggle.setAttribute('aria-label', `${toggle.dataset.actuator} state from ESP32`); });
  $('#downloadBtn').addEventListener('click', () => { const csv = ['Timestamp,Temperature,Humidity,Ammonia,AI State', ...state.readings.map((row) => `${row.timestamp},${row.temperature.toFixed(1)},${row.humidity.toFixed(1)},${row.ammonia.toFixed(1)},${row.ai}`)].join('\n'); const link = document.createElement('a'); link.href = URL.createObjectURL(new Blob([csv], { type: 'text/csv' })); link.download = 'pfms-readings.csv'; link.click(); URL.revokeObjectURL(link.href); showToast('Readings exported successfully'); });
}

buildTelemetryCharts(); setupInteractions(); applyReading(state); renderReadings(); updateEsp32Status(); subscribeToFirebase();
window.setInterval(updateEsp32Status, 10000);
