const FOLDERS_CACHE_KEY = "subjectsFoldersCache_v2";
const FOLDERS_CACHE_TTL_MS = 3600000; // 1 hour persist
const FETCH_TIMEOUT_MS = 15000;

let cachedFolders = [];
let foldersRequest = null;

document.addEventListener("DOMContentLoaded", function () {
  loadFolders();
});

function getFolderGrid() {
  return document.querySelector(".folder-grid");
}

function getEmptyCard() {
  return document.querySelector(".empty-card");
}

function showLoading() {
  const grid = getFolderGrid();
  const emptyCard = getEmptyCard();

  if (grid) {
    grid.innerHTML = "<div class=\"folder-card\">Loading...</div>";
  }
  if (emptyCard) {
    emptyCard.style.display = "none";
  }
}

function showError(message) {
  const grid = getFolderGrid();
  const emptyCard = getEmptyCard();

  if (grid) {
    grid.innerHTML = `<div class="folder-card">${escapeHtml(message)}</div>`;
  }
  if (emptyCard) {
    emptyCard.style.display = "none";
  }
}

function showEmpty() {
  const grid = getFolderGrid();
  const emptyCard = getEmptyCard();

  if (grid) {
    grid.innerHTML = "";
  }
  if (emptyCard) {
    emptyCard.style.display = "flex";
  }
}

function renderFolders(folders) {
  const grid = getFolderGrid();
  const emptyCard = getEmptyCard();
  const role = getCurrentUserRole();

  if (!grid) {
    return;
  }

  if (!folders.length) {
    showEmpty();
    return;
  }

  grid.innerHTML = "";
  if (emptyCard) {
    emptyCard.style.display = "none";
  }

  folders.forEach(function (folderName) {
    const normalizedFolder = String(folderName).trim();
    if (!normalizedFolder) {
      return;
    }

    const colorClass = getFolderColor(normalizedFolder);

    const card = document.createElement("a");
    card.className = "folder-card";
    card.href = `storage.html?folder=${encodeURIComponent(normalizedFolder)}`;
    card.innerHTML = `
      <div class="folder-card-top">
        <div class="folder-icon ${colorClass}">
          <svg viewBox="0 0 24 24">
            <path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"></path>
          </svg>
        </div>
        ${role === "faculty" ? `<button type="button" class="folder-delete-btn" aria-label="Delete ${escapeHtml(normalizedFolder)}">Delete</button>` : ""}
      </div>
      <div class="folder-name"></div>
      <div class="folder-meta">Subject folder</div>
    `;

    const nameNode = card.querySelector(".folder-name");
    if (nameNode) {
      nameNode.textContent = normalizedFolder;
    }

    const deleteBtn = card.querySelector(".folder-delete-btn");
    if (deleteBtn) {
      deleteBtn.addEventListener("click", function (event) {
        event.preventDefault();
        event.stopPropagation();
        deleteFolder(normalizedFolder);
      });
    }

    grid.appendChild(card);
  });
}

function getFolderColor(folderStr) {
  const FOLDER_COLORS = ['blue', 'indigo', 'purple', 'rose', 'teal', 'green', 'amber', 'orange', 'red'];
  let hash = 0;
  for (let i = 0; i < folderStr.length; i++) {
    hash = folderStr.charCodeAt(i) + ((hash << 5) - hash);
  }
  hash = Math.abs(hash);
  return FOLDER_COLORS[hash % FOLDER_COLORS.length];
}

function readFoldersCache() {
  try {
    const raw = localStorage.getItem(FOLDERS_CACHE_KEY);
    if (!raw) {
      return null;
    }

    const parsed = JSON.parse(raw);
    if (!parsed || !Array.isArray(parsed.folders) || typeof parsed.savedAt !== "number") {
      return null;
    }

    if ((Date.now() - parsed.savedAt) > FOLDERS_CACHE_TTL_MS) {
      return null;
    }

    return parsed.folders;
  } catch (error) {
    return null;
  }
}

function writeFoldersCache(folders) {
  try {
    localStorage.setItem(FOLDERS_CACHE_KEY, JSON.stringify({
      folders: folders,
      savedAt: Date.now()
    }));
  } catch (error) {
  }
}

function clearFoldersCache() {
  try {
    localStorage.removeItem(FOLDERS_CACHE_KEY);
  } catch (error) {
  }
}

function getCurrentUserRole() {
  try {
    const user = JSON.parse(localStorage.getItem("loggedInUser") || "{}");
    return String(user.role || "").toLowerCase();
  } catch (error) {
    return "";
  }
}

function sanitizeFolders(data) {
  if (!Array.isArray(data)) {
    return [];
  }

  return data
    .filter(function (item) {
      return typeof item === "string" && item.trim().length > 0;
    })
    .slice(0, 20);
}

function fetchFoldersFromApi() {
  const controller = typeof AbortController !== "undefined" ? new AbortController() : null;
  let timeoutId = null;
  const request = fetch("folders", controller ? { signal: controller.signal } : {})
    .then(function (response) {
      if (!response.ok) {
        throw new Error("Request failed");
      }
      return response.json();
    })
    .then(function (data) {
      return sanitizeFolders(data);
    });

  const timeout = new Promise(function (_, reject) {
    timeoutId = setTimeout(function () {
      if (controller) {
        controller.abort();
      }
      reject(new Error("Request timeout"));
    }, FETCH_TIMEOUT_MS);
  });

  return Promise.race([request, timeout])
    .then(function (folders) {
      return folders;
    })
    .finally(function () {
      if (timeoutId) {
        clearTimeout(timeoutId);
      }
    });
}

async function loadFolders(options) {
  const settings = options || {};
  const useCache = settings.useCache !== false;

  if (foldersRequest) {
    return foldersRequest;
  }

  if (useCache) {
    const storedFolders = readFoldersCache();
    if (storedFolders && Array.isArray(storedFolders) && storedFolders.length > 0) {
      cachedFolders = storedFolders;
      renderFolders(cachedFolders);
      
      // Background refresh to keep everything updated after instant render
      fetchFoldersFromApi()
        .then(function (folders) {
          if (folders && folders.length > 0) {
            const same = JSON.stringify(folders) === JSON.stringify(cachedFolders);
            if (!same) {
              cachedFolders = folders;
              writeFoldersCache(folders);
              renderFolders(folders);
            }
          }
        })
        .catch(function (err) {
          console.warn("Background refresh failed", err);
        });

      return Promise.resolve(cachedFolders);
    }
  }

  showLoading();

  foldersRequest = fetchFoldersFromApi()
    .then(function (folders) {
      cachedFolders = folders;
      writeFoldersCache(folders);
      renderFolders(folders);
      return folders;
    })
    .catch(function () {
      const storedFolders = readFoldersCache();
      if (storedFolders) {
        cachedFolders = storedFolders;
        renderFolders(storedFolders);
        return storedFolders;
      }

      cachedFolders = [];
      showError("Failed to load folders");
      return [];
    })
    .finally(function () {
      foldersRequest = null;
    });

  return foldersRequest;
}

async function createFolder() {
  if (cachedFolders.length >= 20) {
    alert("Max 20 folders allowed");
    return;
  }

  const name = prompt("Enter folder name");
  if (!name) {
    return;
  }

  try {
    const response = await fetch("createFolder", {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        "X-User-Role": "faculty"
      },
      body: `name=${encodeURIComponent(name)}`
    });

    const data = await response.json();
    alert(data.message || "Folder request completed");

    if (!response.ok) {
      return;
    }

    clearFoldersCache();
    await loadFolders({ useCache: false });
  } catch (error) {
    alert("Failed to create folder");
  }
}

async function deleteFolder(folderName) {
  if (getCurrentUserRole() !== "faculty") {
    alert("Only faculty can delete folders");
    return;
  }

  if (!confirm(`Delete folder "${folderName}" and all files inside it?`)) {
    return;
  }

  try {
    const response = await fetch("deleteFolder", {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        "X-User-Role": "faculty"
      },
      body: `folder=${encodeURIComponent(folderName)}`
    });

    const data = await response.json();
    alert(data.message || "Folder request completed");

    if (!response.ok) {
      return;
    }

    clearFoldersCache();
    await loadFolders({ useCache: false });
  } catch (error) {
    alert("Failed to delete folder");
  }
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
