// plant-dashboard.js

// ---------- Supabase setup ----------
const SUPABASE_URL = "https://lkehixwlfdqsdebixcap.supabase.co";
const SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImxrZWhpeHdsZmRxc2RlYml4Y2FwIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjQ2MTk1NDYsImV4cCI6MjA4MDE5NTU0Nn0.HTt0VPEUgbkSJZfvIkuec6P6-TlHKr37c1FLl2hs6Ak";

const client = supabase.createClient(SUPABASE_URL, SUPABASE_KEY);

// ---------- State ----------
let lastTimestamp = null;
let historyRows = [];
let moistureChart = null;

// ---------- Time helpers (ET) ----------
function formatTimestampEastern(ts) {
  if (!ts) return "--";
  const d = new Date(ts + "Z"); // treat Supabase timestamp as UTC
  return d.toLocaleString("en-US", {
    timeZone: "America/New_York",
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
    hour12: true,
  });
}

function formatTimeShortEastern(ts) {
  const d = new Date(ts + "Z"); // treat Supabase timestamp as UTC
  return d.toLocaleTimeString("en-US", {
    timeZone: "America/New_York",
    hour: "numeric",
    minute: "2-digit",
    hour12: true,
  });
}

// ---------- Connection status ----------
function updateConnectionStatus() {
  if (!lastTimestamp) return;

  const now = Date.now();
  const ts = new Date(lastTimestamp + "Z").getTime();
  const ageMs = now - ts;

  const statusDot = document.getElementById("connection-status");
  const statusText = document.getElementById("connection-text");
  const lastEl = document.getElementById("last-updated");

  if (!statusDot || !statusText || !lastEl) return;

  lastEl.textContent = `(Last updated: ${formatTimestampEastern(
    lastTimestamp
  )} ET)`;

  const STALE_MS = 30 * 1000; // 30 seconds
  const OFFLINE_MS = 2 * 60 * 1000; // 2 minutes

  statusDot.classList.remove("online", "stale", "offline");

  if (ageMs > OFFLINE_MS) {
    statusDot.classList.add("offline");
    statusText.textContent = "Disconnected – check plant connection";
  } else if (ageMs > STALE_MS) {
    statusDot.classList.add("stale");
    statusText.textContent = "Stale – data older than 30s";
  } else {
    statusDot.classList.add("online");
    statusText.textContent = "Live";
  }
}

// keep checking status even if no new data comes in
setInterval(updateConnectionStatus, 15000);

// ---------- History / chart ----------
async function loadHistory(plantId) {
  const { data, error } = await client
    .from("plant_data")
    .select("timestamp, soil")
    .eq("plant_id", plantId)
    .order("timestamp", { ascending: true })
    .limit(100);

  if (error) {
    console.error("History error", error);
    return;
  }

  const MAX_POINTS = 30;
  historyRows = (data || []).slice(-MAX_POINTS);
  renderHistory();
}

function renderHistory() {
  const canvas = document.getElementById("moistureChart");
  if (!canvas) return;

  if (!historyRows.length) {
    if (moistureChart) {
      moistureChart.destroy();
      moistureChart = null;
    }
    return;
  }

  const labels = historyRows.map((row) =>
    formatTimeShortEastern(row.timestamp)
  );
  const soilData = historyRows.map((row) => row.soil);

  const ctx = canvas.getContext("2d");

  if (!moistureChart) {
    moistureChart = new Chart(ctx, {
      type: "line",
      data: {
        labels,
        datasets: [
          {
            label: "Soil Moisture (raw)",
            data: soilData,
            tension: 0.3,
            pointRadius: 3,
            pointHoverRadius: 5,
          },
        ],
      },
      options: {
        animation: false,
        interaction: {
          mode: "index",
          intersect: false,
        },
        plugins: {
          legend: { display: true },
          tooltip: {
            enabled: true,
            callbacks: {
              label: (ctx) => `Soil moisture: ${ctx.parsed.y}`,
            },
          },
        },
        scales: {
          x: { ticks: { maxTicksLimit: 6 } },
          y: { title: { display: true, text: "ADC value" } },
        },
      },
    });
  } else {
    moistureChart.data.labels = labels;
    moistureChart.data.datasets[0].data = soilData;
    moistureChart.update("none");
  }
}

function pushHistoryRow(row) {
  const MAX_POINTS = 30;
  historyRows.push({ timestamp: row.timestamp, soil: row.soil });
  if (historyRows.length > MAX_POINTS) {
    historyRows = historyRows.slice(historyRows.length - MAX_POINTS);
  }
  renderHistory();
}

// ---------- UI update ----------
function updateUI(row) {
  const soilEl = document.getElementById("soil");
  const lightEl = document.getElementById("light");
  const tempEl = document.getElementById("temp");
  const humidityEl = document.getElementById("humidity");
  const pumpEl = document.getElementById("pump");
  const conditionEl = document.getElementById("condition");
  const aiLabelEl = document.getElementById("ai-label");
  const aiConfEl = document.getElementById("ai-conf");

  if (soilEl) soilEl.textContent = row.soil;
  if (lightEl) lightEl.textContent = row.light;
  if (tempEl) tempEl.textContent = row.temp;
  if (humidityEl) humidityEl.textContent = row.humidity;
  if (pumpEl)
    pumpEl.textContent = row.pump_state === 1 ? "ON" : "OFF";
  if (conditionEl) conditionEl.textContent = row.condition;

  const label = row.ai_label || row.condition || "--";
  if (aiLabelEl) aiLabelEl.textContent = label;

  if (aiConfEl) {
    if (typeof row.ai_conf === "number") {
      aiConfEl.textContent = row.ai_conf.toFixed(2);
    } else {
      aiConfEl.textContent = "--";
    }
  }

  lastTimestamp = row.timestamp;
  updateConnectionStatus();
}

// ---------- Data loaders ----------
async function loadLatest(plantId) {
  const { data, error } = await client
    .from("plant_data")
    .select("*")
    .eq("plant_id", plantId)
    .order("id", { ascending: false })
    .limit(1);

  if (error) {
    console.error("Latest error", error);
    return;
  }

  if (data && data.length > 0) {
    updateUI(data[0]);
    pushHistoryRow(data[0]);
  }
}

// ---------- Main init ----------
function initPlantDashboard(plantId) {
  if (!plantId) {
    console.error("No PLANT_ID provided for dashboard page");
    return;
  }

  loadLatest(plantId);
  loadHistory(plantId);

  client
    .channel("plant_updates")
    .on(
      "postgres_changes",
      { event: "INSERT", schema: "public", table: "plant_data" },
      (payload) => {
        if (payload.new.plant_id === plantId) {
          updateUI(payload.new);
          pushHistoryRow(payload.new);
        }
      }
    )
    .subscribe();
}

// Auto-init when DOM is ready, using global window.PLANT_ID
window.addEventListener("DOMContentLoaded", () => {
  if (window.PLANT_ID) {
    initPlantDashboard(window.PLANT_ID);
  }
});
