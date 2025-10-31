function getCookie(name) {
  const nameEQ = name + "=";
  const ca = document.cookie.split(';');
  for (let i = 0; i < ca.length; i++) {
     let c = ca[i].trim();
     if (c.indexOf(nameEQ) === 0) return decodeURIComponent(c.substring(nameEQ.length));
   }
  return null;
}

function getArduinoIP() {
  const ip = getCookie('arduinoIP');
  return ip ? `http://${ip.replace(/^https?:\/\//, '')}` : null;
}

window.addEventListener("load", async () => {
  const popup = document.getElementById("connectPopup");
  const content = document.getElementById("mainContent");
  const closeBtn = document.getElementById("closePopup");

  function showPopup() {
    content.classList.add("blurred");
    popup.classList.add("show");
  }

  function hidePopup() {
    popup.classList.remove("show");
    content.classList.remove("blurred");
  }

  // กดปิดได้เหมือนเดิม
  closeBtn.addEventListener("click", () => {
    hidePopup();
  });

  // 1) ลองเอา IP จากคุกกี้ก่อน (อันเดียวกับ result.js)
  const ip = typeof getArduinoIP === "function" ? getArduinoIP() : null;

  if (!ip) {
    // ไม่มี IP ในคุกกี้ ก็แสดง popup ไป
    showPopup();
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