// dashboard.js

// ---- Supabase setup ----
const SUPABASE_URL = "https://lkehixwlfdqsdebixcap.supabase.co";
const SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImxrZWhpeHdsZmRxc2RlYml4Y2FwIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjQ2MTk1NDYsImV4cCI6MjA4MDE5NTU0Nn0.HTt0VPEUgbkSJZfvIkuec6P6-TlHKr37c1FLl2hs6Ak";

const client = supabase.createClient(SUPABASE_URL, SUPABASE_KEY);

const STALE_MS = 30 * 1000;       // 30 seconds
const OFFLINE_MS = 2 * 60 * 1000; // 2 minutes

let lastAnyTimestamp = null;

// ---- Time helper (ET) ----
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

// ---- Plant card update ----
function setBadge(el, text, bg, fg) {
  if (!el) return;
  el.textContent = text;
  el.style.background = bg;
  el.style.color = fg;
}

// Set live status badge on plant card
function setLiveStatus(el, state) {
  if (!el) return;

  if (state === "live") {
    el.textContent = "🟢";
    el.style.background = "#e8ffe7";
    el.style.color = "#2d6a33";
  } else if (state === "stale") {
    el.textContent = "🟡";
    el.style.background = "#fff4e0";
    el.style.color = "#b36d00";
  } else if (state === "offline") {
    el.textContent = "🔴";
    el.style.background = "#ffe5e5";
    el.style.color = "#b00020";
  } else {
    // "nodata" or anything else
    el.textContent = "⚪";
    el.style.background = "#eee";
    el.style.color = "#333";
  }
}

async function loadLatestCondition(plantId, elementId) {
  const el = document.getElementById(elementId);
  if (!el) return;

  const liveEl = document.getElementById(
    elementId.replace("-status", "-live-status")
  );

  try {
    const { data, error } = await client
      .from("plant_data")
      .select("condition, timestamp")
      .eq("plant_id", plantId)
      .order("id", { ascending: false })
      .limit(1);

    if (error) {
      console.error("Supabase error for", plantId, error);
      setBadge(el, "Server error", "#ffe5e5", "#b00020");
      setLiveStatus(liveEl, "offline");
      return;
    }

    if (!data || data.length === 0) {
      setBadge(el, "No data yet", "#eee", "#333");
      setLiveStatus(liveEl, "nodata"); // falls into "No Data" style
      return;
    }

    const row = data[0];
    const ts = row.timestamp;
    const now = Date.now();
    const rowTime = new Date(ts + "Z").getTime();
    const ageMs = now - rowTime;

    // Offline / stale overrides condition text
    if (ageMs > OFFLINE_MS) {
      // offline overrides condition
      setBadge(el, "Disconnected", "#ffe5e5", "#b00020");
      setLiveStatus(liveEl, "offline");
    } else if (ageMs > STALE_MS) {
      setBadge(el, "Stale data", "#fff4e0", "#b36d00");
      setLiveStatus(liveEl, "stale");
    } else {
      const condition = row.condition;

      setLiveStatus(liveEl, "live");

      if (condition === "fine") {
        setBadge(el, "🌿 Healthy", "#e8ffe7", "#2d6a33");
      } else if (condition === "needs_water") {
        setBadge(el, "💧 Needs Water", "#fff4e0", "#b36d00");
      } else {
        setBadge(el, "Unknown", "#eee", "#333");
      }
    }

    // hover tooltip on the big badge
    el.title = `Last updated: ${formatTimestampEastern(ts)} ET`;

  } catch (err) {
    console.error("Unexpected error for", plantId, err);
    setBadge(el, "Server error", "#ffe5e5", "#b00020");
    setLiveStatus(liveEl, "offline");
  }
}

function refreshAll() {
  loadLatestCondition("haworthia", "h-status");
  loadLatestCondition("peperomia", "p-status");
  loadLatestCondition("fittonia", "f-status");
}

// initial + periodic refresh
refreshAll();
setInterval(refreshAll, 3000);
