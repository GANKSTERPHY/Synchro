let popup = document.getElementById("connectPopup");
let content = document.getElementById("mainContent");
let closeBtn = document.getElementById("closePopup");

function getCookie(name) {
  const nameEQ = name + "=";
  const ca = document.cookie.split(';');
  for (let i = 0; i < ca.length; i++) {
    let c = ca[i].trim();
    if (c.indexOf(nameEQ) === 0) return decodeURIComponent(c.substring(nameEQ.length));
  }
  return null;
}

function setCookie(name, value, hours = 1) {
  const d = new Date();
  d.setTime(d.getTime() + (hours * 60 * 60 * 1000));
  document.cookie = `${name}=${encodeURIComponent(value)};expires=${d.toUTCString()};path=/`;
}

function getArduinoIP() {
  const ip = getCookie('arduinoIP');
  return ip ? `http://${ip.replace(/^https?:\/\//, '')}` : null;
}

function showPopup() {
  content.classList.add("blurred");
  popup.classList.add("show");
}

function hidePopup() {
  popup.classList.remove("show");
  content.classList.remove("blurred");
}

window.addEventListener("load", async () => {
  popup = document.getElementById("connectPopup");
  content = document.getElementById("mainContent");
  closeBtn = document.getElementById("closePopup");



  // กดปิดได้เหมือนเดิม
  closeBtn.addEventListener("click", () => {
    hidePopup();
  });

  // 1) ลองเอา IP จากคุกกี้ก่อน (อันเดียวกับ result.js)
  const ip = typeof getArduinoIP === "function" ? getArduinoIP() : null;

  if (!ip) {
    // ไม่มี IP ในคุกกี้ ก็แสดง popup ไป
    showPopup();
    connectArduino();
    return;
  }

  // 2) มี IP แล้วก็ลองเรียกที่บอร์ดดู
  const online = await isArduinoOnline(ip);

  if (online) {
    // ต่อได้แล้ว ไม่ต้องเด้ง
    hidePopup();
    console.log("Arduino online at", ip);
  } else {
    // IP มีแต่บอร์ดไม่ตอบ ก็ให้เด้ง
    showPopup();
  }
});

// Connect to Arduino
async function connectArduino() {
  const ipInput = "synchro.local";
  arduinoIP = `http://${ipInput.replace(/^https?:\/\//, '')}`;

  try {
    const res = await fetch(arduinoIP + "/handshake", { mode: 'cors' });
    if (!res.ok) throw new Error("Handshake failed");

    arduinoConnected = true;

    setCookie("arduinoIP", ipInput);

    const data = await res.json().catch(() => null);
    console.log(data);
    if (data && data.songs) {
      console.log("Arduino song list:", data.songs);
      //showStatus(`Arduino has ${data.songs.length} songs: ${data.songs.join(', ')}`, "info");
    }
    hidePopup();
  } catch (err) {
    arduinoConnected = false;
    //showStatus("Failed to connect to Arduino: " + err.message, "error");
  }
}

// ฟังก์ชันเช็กว่า Arduino ยังหายใจอยู่มั้ย
async function isArduinoOnline(ip) {
  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2000);

    // ถ้า ESP32 มี endpoint /handshake ก็ใช้แบบนี้
    const res = await fetch(ip + "/handshake", {
      mode: "cors",
      signal: controller.signal
    });

    clearTimeout(timeout);
    return res.ok;
  } catch (err) {
    console.warn("Arduino not reachable:", err.message);
    return false;
  }
}