// public/js/downloads.js

function getApiBaseUrl() {
  return "";
}

function apiPath(path) {
  const base = getApiBaseUrl();
  if (!base) return path;
  return base.endsWith("/") ? `${base}${path}` : `${base}/${path}`;
}

// Download file + log it
async function downloadFile(folder, fileName) {
  try {
    const folderParam = encodeURIComponent(String(folder || ""));
    const fileParam = encodeURIComponent(String(fileName || ""));

    // Trigger file download from backend (ESP32 SD card)
    window.open(apiPath(`download?folder=${folderParam}&file=${fileParam}`));

    // Log download to backend
    await fetch(apiPath("logDownload"), {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify({
        file: fileName,
        folder: folder,
        time: new Date().toISOString()
      })
    });
  } catch (err) {
    console.error("Download failed:", err);
  }
}

// OPTIONAL: Fetch download analytics from backend
async function updateDownloadAnalytics() {
  try {
    const res = await fetch("analytics");
    const data = await res.json();

    const totalEl = document.getElementById("totalDownloads");
    const popularEl = document.getElementById("popularFile");

    if (totalEl) {
      totalEl.innerText = data.total || 0;
    }

    if (popularEl) {
      if (data.popular) {
        popularEl.innerText = `${data.popular} (${data.count} downloads)`;
      } else {
        popularEl.innerText = "No downloads yet";
      }
    }
  } catch (err) {
    console.error("Analytics fetch failed:", err);
  }
}

// Run analytics on page load (only if section exists)
document.addEventListener("DOMContentLoaded", () => {
  if (document.getElementById("totalDownloads")) {
    updateDownloadAnalytics();
  }
});
