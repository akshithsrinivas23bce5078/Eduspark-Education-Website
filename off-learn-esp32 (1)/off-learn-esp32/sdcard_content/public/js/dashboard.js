// public/js/dashboard.js

document.addEventListener("DOMContentLoaded", async () => {
  if (typeof requireLogin === "function") requireLogin();

  await loadDashboard();
});

async function loadDashboard() {
  // 1. Update the Role metrics based on actual login state
  const user = (typeof getLoggedInUser === "function") ? getLoggedInUser() : null;
  const roleModeValue = document.getElementById("metricRoleMode");
  if (user && roleModeValue) {
    const role = String(user.role || "").toLowerCase();
    if (role === "faculty") {
      roleModeValue.textContent = "Admin / Faculty";
    } else if (role === "student") {
      roleModeValue.textContent = "Admin / Student";
    } else {
      roleModeValue.textContent = user.role || "User";
    }
  }

  // 2. Fetch and populate counts & storage
  try {
    // Get folder count first
    const folderRes = await fetch("folders", { cache: "no-store" });
    if (folderRes.ok) {
      const subjects = await folderRes.json();
      const subjectsArray = Array.isArray(subjects) ? subjects : [];
      const folderMetric = document.getElementById("metricFolders");
      if (folderMetric) folderMetric.innerText = subjectsArray.length;
      
      // Pre-cache for subjects.html to enable instant loading
      // ONLY update if we got a real list to avoid clearing valid cache with a transient error
      if (subjectsArray.length > 0) {
        try {
          localStorage.setItem("subjectsFoldersCache_v2", JSON.stringify({
            folders: subjectsArray,
            savedAt: Date.now()
          }));
        } catch(e) {}
      }
    }

    // Get file count, storage stats, and activity from aggregated library view
    const filesRes = await fetch("list-files?folder=library", { cache: "no-store" });
    if (filesRes.ok) {
      const allFiles = await filesRes.json();
      const fileMetric = document.getElementById("metricFiles");
      if (fileMetric) fileMetric.innerText = allFiles.length;

      // Calculate storage used
      let totalBytes = 0;
      allFiles.forEach(f => { totalBytes += (Number(f.size) || 0); });
      
      const totalMB = totalBytes / (1024 * 1024);
      const limitMB = 4096; // 4GB Capacity
      const usedPct = Math.min((totalMB / limitMB) * 100, 100);
      const remainingMB = Math.max(limitMB - totalMB, 0);

      // Update Top metric "Today Uploads" with Status
      const todayMetric = document.getElementById("metricToday");
      if (todayMetric) todayMetric.innerText = allFiles.length > 0 ? "Active" : "0";

      // Update Storage Pulse section
      const storageBar = document.getElementById("storageProgress");
      const storageText = document.getElementById("storageText");
      const storageTag = document.getElementById("storageTag");

      if (storageBar) storageBar.style.width = usedPct + "%";
      if (storageText) {
        storageText.textContent = `${usedPct.toFixed(1)}% (${totalMB.toFixed(1)} MB used, ${remainingMB.toFixed(1)} MB remaining)`;
      }
      if (storageTag) {
        storageTag.textContent = usedPct > 90 ? "Critical" : (usedPct > 70 ? "Warning" : "Healthy");
        storageTag.style.background = usedPct > 90 ? "#ef4444" : (usedPct > 70 ? "#f97316" : "#22c55e");
      }

      renderRecentActivity(allFiles);
    }

  } catch (err) {
    console.error("Dashboard population failed:", err);
    renderRecentActivity([]);
  }
}

function renderRecentActivity(files) {
  const table = document.getElementById("recentActivityBody");
  if (!table) return;

  if (!files || !files.length) {
    table.innerHTML = "<tr><td colspan='4' class=\"muted\" style=\"padding:16px;\">No recent activity</td></tr>";
    return;
  }

  table.innerHTML = "";
  // Show last 5 files added
  files.slice(-5).reverse().forEach(f => {
    const name = (f && typeof f === 'object') ? f.file : f;
    const folder = (f && typeof f === 'object') ? f.folder : "General";
    
    const row = document.createElement("tr");
    row.innerHTML = `
      <td>Recently</td>
      <td>Uploaded</td>
      <td>${escapeHtml(name)} <span class="muted">(${escapeHtml(folder)})</span></td>
      <td style="color:var(--accent);">Ready</td>
    `;
    table.appendChild(row);
  });
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}
