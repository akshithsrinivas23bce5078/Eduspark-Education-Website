function getCurrentPageName() {
  const path = window.location.pathname || "/";
  const page = path.split("/").pop();
  return page || "index.html";
}

function getLoggedInUser() {
  const storedUser =
    localStorage.getItem("loggedInUser") ||
    localStorage.getItem("currentUser") ||
    localStorage.getItem("edusparkSession");

  if (!storedUser) {
    return null;
  }

  try {
    return JSON.parse(storedUser);
  } catch (error) {
    console.error("Failed to parse stored session:", error);
    localStorage.removeItem("loggedInUser");
    localStorage.removeItem("currentUser");
    localStorage.removeItem("edusparkSession");
    return null;
  }
}

function isLoginPage() {
  const page = getCurrentPageName().toLowerCase();
  return page === "index.html";
}

function updateLoginStatus(message, isError) {
  const status = document.getElementById("login-status");
  if (!status) {
    if (isError) {
      alert(message);
    }
    return;
  }

  status.textContent = message;
  status.classList.toggle("error", Boolean(isError));
  status.hidden = !message;
}

function requireLogin() {
  if (!getLoggedInUser()) {
    window.location.replace("index.html");
    return false;
  }

  return true;
}

document.addEventListener("DOMContentLoaded", function () {
  const form = document.getElementById("login-form");
  const emailField = document.getElementById("email");
  const passwordField = document.getElementById("password");

  if (form && emailField && passwordField) {
    const existingUser = getLoggedInUser();
    if (existingUser && isLoginPage()) {
      window.location.replace("dashboard.html");
      return;
    }

    form.addEventListener("submit", function (e) {
      e.preventDefault();

      const email = emailField.value.trim().toLowerCase();
      const password = passwordField.value;
      const selectedRole = document.querySelector('input[name="role"]:checked');
      const role = selectedRole ? selectedRole.value : "";

      let user = null;
      if (typeof findUser === "function") {
        user = findUser(email, password, role);
      } else if (typeof USERS !== "undefined" && Array.isArray(USERS)) {
        user = USERS.find(u => u.email === email && u.password === password && u.role === role);
      } else if (typeof users !== "undefined" && Array.isArray(users)) {
        user = users.find(u => u.email === email && u.password === password && u.role === role);
      }

      if (!user) {
        updateLoginStatus("Invalid credentials. Please check your email, password, and role.", true);
        passwordField.focus();
        passwordField.select();
        return;
      }

      localStorage.setItem("loggedInUser", JSON.stringify(user));
      updateLoginStatus("Login successful. Redirecting to dashboard...", false);
      window.location.replace("dashboard.html");
    });
  }

  if (!isLoginPage()) {
    requireLogin();
  }
});

function logout() {
  localStorage.removeItem("loggedInUser");
  localStorage.removeItem("currentUser");
  localStorage.removeItem("edusparkSession");
  window.location.href = "index.html";
}

window.getLoggedInUser = getLoggedInUser;
window.requireLogin = requireLogin;
window.logout = logout;

document.addEventListener("DOMContentLoaded", function () {
  document.querySelectorAll(".btn-logout, .shared-navbar__logout, [data-logout]").forEach((button) => {
    button.addEventListener("click", function (event) {
      event.preventDefault();
      logout();
    });
  });
});
