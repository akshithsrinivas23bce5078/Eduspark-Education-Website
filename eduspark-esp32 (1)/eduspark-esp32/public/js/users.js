// public/js/users.js

// Predefined system users (demo mode)

const USERS = [
  {
    id: 1,
    name: "Arjun Mehta",
    role: "student",
    regNo: "UG2021001",
    email: "arjun@university.edu",
    password: "1234"
  },
  {
    id: 2,
    name: "Priya Sharma",
    role: "student",
    regNo: "UG2021002",
    email: "priya@university.edu",
    password: "1234"
  },
  {
    id: 3,
    name: "Rahul Verma",
    role: "student",
    regNo: "UG2021003",
    email: "rahul@university.edu",
    password: "1234"
  },
  {
    id: 4,
    name: "Sneha Iyer",
    role: "student",
    regNo: "UG2021004",
    email: "sneha@university.edu",
    password: "1234"
  },
  {
    id: 5,
    name: "Dr. Kavita Rao",
    role: "faculty",
    regNo: "FAC1001",
    email: "kavita@university.edu",
    password: "admin123"
  }
];

// Helper function to find user
function findUser(email, password, role) {
  return USERS.find(user =>
    user.email === email &&
    user.password === password &&
    user.role === role
  );
}