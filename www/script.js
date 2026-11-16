document.getElementById("btn").onclick = () => {
  document.getElementById("out").textContent =
    "JavaScript loaded from the server at " + new Date().toLocaleTimeString();
};
