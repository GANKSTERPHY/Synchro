window.addEventListener("load", () => {
      const popup = document.getElementById("connectPopup");
      const content = document.getElementById("mainContent");

      // ✅ แสดง popup + เบลอพื้นหลัง
      content.classList.add("blurred");
      popup.classList.add("show");

      // ✅ ปิด popup เมื่อคลิกกากบาท
      document.getElementById("closePopup").addEventListener("click", () => {
        popup.classList.remove("show");
        content.classList.remove("blurred");
      });
});