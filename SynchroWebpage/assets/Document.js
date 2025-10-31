function revealOnScroll() {
      const reveals = document.querySelectorAll('.reveal');
      for (let i = 0; i < reveals.length; i++) {
        const windowHeight = window.innerHeight;
        const elementTop = reveals[i].getBoundingClientRect().top;
        const elementVisible = 100;
        if (elementTop < windowHeight - elementVisible) {
          reveals[i].classList.add('active');
        } else {
          reveals[i].classList.remove('active');
        }
      }
    }
    window.addEventListener('scroll', revealOnScroll);
    revealOnScroll();

const btn = document.getElementById("toggleCode");
  const codeBox = document.getElementById("codeBox");
  let expanded = false;

  btn.addEventListener("click", () => {
    expanded = !expanded;
    codeBox.classList.toggle("expanded");
    btn.textContent = expanded ? "Hide Code" : "See More";
  });