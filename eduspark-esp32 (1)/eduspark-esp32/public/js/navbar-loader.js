(function () {
  function getLoggedInUser() {
    try {
      return JSON.parse(localStorage.getItem("loggedInUser") || "null");
    } catch (error) {
      console.error("Failed to parse loggedInUser:", error);
      return null;
    }
  }

  function logout() {
    localStorage.removeItem("loggedInUser");
    localStorage.removeItem("currentUser");
    localStorage.removeItem("edusparkSession");
    window.location.href = "index.html";
  }

  function ensureAuthenticated() {
    if (!getLoggedInUser()) {
      window.location.replace("index.html");
      return false;
    }
    return true;
  }

  async function loadNavbar() {
    if (!ensureAuthenticated()) {
      return;
    }

    const navbarHost = document.getElementById("navbar");
    if (!navbarHost) {
      return;
    }

    try {
      const response = await fetch("navbar.html");
      if (!response.ok) {
        throw new Error(`Navbar request failed: ${response.status}`);
      }

      navbarHost.innerHTML = await response.text();
      document.body.classList.add("has-navbar");

      const logoutButton = navbarHost.querySelector(".shared-navbar__logout");
      if (logoutButton) {
        logoutButton.addEventListener("click", logout);
      }

      const user = getLoggedInUser();
      if (!user || user.role !== "faculty") {
        navbarHost.querySelectorAll("[data-role-only='faculty']").forEach((link) => {
          link.style.display = "none";
        });
      }

      const currentPage = window.location.pathname.split("/").pop() || "dashboard.html";
      navbarHost.querySelectorAll("[data-page]").forEach((link) => {
        const isActive = link.getAttribute("data-page") === currentPage;
        link.classList.toggle("active", isActive);
        if (isActive) {
          link.setAttribute("aria-current", "page");
        } else {
          link.removeAttribute("aria-current");
        }
      });
    } catch (error) {
      console.error("Failed to load navbar:", error);
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", loadNavbar);
  } else {
    loadNavbar();
  }

  window.logout = window.logout || logout;
})();
