// public/js/filters.js

function getApiBaseUrl() {
  return "";
}

function apiPath(path) {
  const base = getApiBaseUrl();
  if (!base) return path;
  return base.endsWith("/") ? `${base}${path}` : `${base}/${path}`;
}

document.addEventListener("DOMContentLoaded", () => {
  const searchInput = document.getElementById("searchInput");
  const typeFilter = document.getElementById("typeFilter");
  const sortFilter = document.getElementById("sortFilter");

  let allFilesCache = null;
  let lastFetchAt = 0;
  const FRONTEND_CACHE_TTL = 3000; // 3 seconds

  async function fetchAllFiles() {
    const now = Date.now();
    if (allFilesCache && (now - lastFetchAt) < FRONTEND_CACHE_TTL) {
      return allFilesCache;
    }

    try {
      const res = await fetch(apiPath(`list-files?folder=all`), { cache: "no-store" });
      if (!res.ok) throw new Error("Failed to list files");
      
      const filesData = await res.json();
      allFilesCache = filesData.map(f => {
        if (f && typeof f === 'object') {
          return { name: f.file, folderName: f.folder, size: 0, date: "" };
        }
        return { name: f, folderName: "", size: 0, date: "" };
      });
      lastFetchAt = now;
      return allFilesCache;
    } catch {
      return allFilesCache || [];
    }
  }

  async function applyFilters() {
    let files = await fetchAllFiles();
    const search = searchInput?.value.trim().toLowerCase();
    const type = typeFilter?.value;
    const sort = sortFilter?.value;

    if (search) {
      files = files.filter(f => f.name.toLowerCase().includes(search) || (f.folderName && f.folderName.toLowerCase().includes(search)));
    }
    if (type && type !== "all") {
      const ext = "." + type.toLowerCase();
      files = files.filter(f => f.name.toLowerCase().endsWith(ext));
    }
    if (sort === "name") {
      files.sort((a, b) => a.name.localeCompare(b.name));
    }

    if (typeof renderRecentActivity === "function") {
      renderRecentActivity(files);
    }
  }

  let debounceTimer;
  const debouncedApplyFilters = () => {
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(applyFilters, 250);
  };

  searchInput?.addEventListener("input", debouncedApplyFilters);
  typeFilter?.addEventListener("change", applyFilters);
  sortFilter?.addEventListener("change", applyFilters);

  applyFilters();
});
