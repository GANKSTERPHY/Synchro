// ===============================
// Global State
// ===============================
let songs = [];
let arduinoConnected = false;
let arduinoIP = "";
let currentlyPlayingIndex = -1;
let audioElements = new Map(); // Store audio elements for each song
let songTimers = new Map(); // Store timers for tracking song completion

// Built-in song folders
const builtInFolders = ["Last Stardust", "Dispersion-Relation"];

// ===============================
// Cookie Management
// ===============================
function setCookie(name, value, hours = 1) {
  const d = new Date();
  d.setTime(d.getTime() + (hours * 60 * 60 * 1000));
  document.cookie = `${name}=${encodeURIComponent(value)};expires=${d.toUTCString()};path=/`;
}

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

// ===============================
// Popup Management
// ===============================
let popup = document.getElementById("connectPopup");
let content = document.getElementById("mainContent");
let closeBtn = document.getElementById("closePopup");

function showPopup() {
  content.classList.add("blurred");
  popup.classList.add("show");
}

function hidePopup() {
  popup.classList.remove("show");
  content.classList.remove("blurred");
}

// ===============================
// Arduino Communication
// ===============================
async function isArduinoOnline(ip) {
  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2000);

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

async function connectArduino() {
  const ipInput = "synchro.local";
  arduinoIP = `http://${ipInput.replace(/^https?:\/\//, '')}`;

  try {
    const res = await fetch(arduinoIP + "/handshake", { mode: 'cors' });
    if (!res.ok) throw new Error("Handshake failed");

    arduinoConnected = true;
    setCookie("arduinoIP", ipInput);

    const data = await res.json().catch(() => null);
    console.log("Handshake data:", data);
    
    if (data && data.songs) {
      console.log("Arduino song list:", data.songs);
      // Load Arduino songs (will be merged with built-in songs)
      await loadArduinoSongs(data.songs);
    }
    
    hidePopup();
  } catch (err) {
    arduinoConnected = false;
    console.error("Failed to connect to Arduino:", err.message);
  }
}

// ===============================
// Song Loading
// ===============================
async function loadBuiltInSongs() {
  for (const folder of builtInFolders) {
    const jsonUrl = `res/${folder}/${folder}.json`;
    const mp3Url = `res/${folder}/${folder}.mp3`;
    const coverUrl = `res/${folder}/${folder}.jpg`;

    try {
      const json = await fetch(jsonUrl).then(r => r.json());
      
      // Check if song already exists
      if (!songs.find(s => s.folderName === folder)) {
        songs.push({
          name: json.songName || folder,
          folderName: folder,
          artist: json.artist || "Unknown Artist",
          length: json.length || 0,
          difficulty: json.difficulty || 3,
          tiles: json.tiles,
          audio: mp3Url,
          cover: coverUrl,
          isBuiltIn: true,
          isFromArduino: false
        });
      }
    } catch (err) {
      console.error("Error loading built-in song:", folder, err);
    }
  }
}

async function loadArduinoSongs(arduinoSongList) {
  // arduinoSongList is an array of song folder names from Arduino
  for (const folderName of arduinoSongList) {
    // Skip if already loaded
    if (songs.find(s => s.folderName === folderName)) {
      continue;
    }

    // Try to load JSON metadata from Arduino
    try {
      const jsonUrl = `${arduinoIP}/songs/${folderName}/${folderName}.json`;
      const json = await fetch(jsonUrl, { mode: 'cors' }).then(r => r.json());
      
      songs.push({
        name: json.songName || folderName,
        folderName: folderName,
        artist: json.artist || "Unknown Artist",
        length: json.length || 0,
        difficulty: json.difficulty || 3,
        tiles: json.tiles,
        audio: `${arduinoIP}/songs/${folderName}/${folderName}.mp3`,
        cover: `${arduinoIP}/songs/${folderName}/${folderName}.jpg`,
        isBuiltIn: false,
        isFromArduino: true
      });
    } catch (err) {
      console.error("Error loading Arduino song:", folderName, err);
    }
  }
}

// ===============================
// Game Control
// ===============================
async function startGameOnArduino(song, songIndex) {
  if (!arduinoConnected) {
    alert("Please connect to Arduino first!");
    return;
  }

  const baseName = song.folderName.replace(/[^a-zA-Z0-9_-]/g, '_');
  
  try {
    console.log(`Starting game with song: ${song.name}`);
    
    // Tell Arduino to start the game with this song
    const res = await fetch(`${arduinoIP}/play?song=${encodeURIComponent(baseName)}`, {
      mode: 'cors'
    });
    
    if (!res.ok) {
      throw new Error("Play request failed");
    }

    console.log(`Game started on Arduino with song: ${song.name}`);
    
    // Start tracking song completion
    trackSongCompletion(song, songIndex);
    
  } catch (err) {
    console.error(`Failed to start game: ${err.message}`);
    alert(`Failed to start game: ${err.message}`);
  }
}

function trackSongCompletion(song, songIndex) {
  // Clear any existing timer for this song
  if (songTimers.has(songIndex)) {
    clearTimeout(songTimers.get(songIndex));
  }

  // Set timer for song duration (add 2 seconds buffer)
  const duration = (song.length || 180) * 1000 + 2000;
  
  const timer = setTimeout(() => {
    console.log(`Song "${song.name}" completed, redirecting to results...`);
    
    // Save song info to cookie for result page
    setCookie('lastPlayedSong', JSON.stringify({
      name: song.name,
      artist: song.artist,
      difficulty: song.difficulty,
      folderName: song.folderName
    }), 1);
    
    // Redirect to result page with completion flag
    window.location.href = 'result.html?completed=true';
  }, duration);

  songTimers.set(songIndex, timer);
}

// ===============================
// Audio Preview Control
// ===============================
function togglePreview(songIndex) {
  const song = songs[songIndex];
  
  // Stop all other playing songs
  audioElements.forEach((audio, idx) => {
    if (idx !== songIndex && !audio.paused) {
      audio.pause();
      audio.currentTime = 0;
      updatePlayButton(idx, false);
    }
  });

  // Get or create audio element
  let audio = audioElements.get(songIndex);
  if (!audio) {
    audio = new Audio(song.audio);
    audio.volume = 0.5;
    audioElements.set(songIndex, audio);
    
    // Update UI when song ends
    audio.addEventListener('ended', () => {
      updatePlayButton(songIndex, false);
    });
  }

  // Toggle play/pause
  if (audio.paused) {
    audio.play();
    currentlyPlayingIndex = songIndex;
    updatePlayButton(songIndex, true);
  } else {
    audio.pause();
    audio.currentTime = 0;
    currentlyPlayingIndex = -1;
    updatePlayButton(songIndex, false);
  }
}

function updatePlayButton(songIndex, isPlaying) {
  const card = document.querySelectorAll('.song-card')[songIndex];
  if (!card) return;
  
  const playBtn = card.querySelector('.ctrl-btn.play img');
  if (playBtn) {
    playBtn.src = isPlaying ? 'assets/Img/pause.svg' : 'assets/Img/play.svg';
    playBtn.alt = isPlaying ? 'Pause' : 'Play';
  }
}

function previousSong(songIndex) {
  const audio = audioElements.get(songIndex);
  if (audio && !audio.paused) {
    audio.currentTime = Math.max(0, audio.currentTime - 10);
  }
}

function nextSong(songIndex) {
  const audio = audioElements.get(songIndex);
  if (audio && !audio.paused) {
    audio.currentTime = Math.min(audio.duration, audio.currentTime + 10);
  }
}

function updateProgress(songIndex) {
  const audio = audioElements.get(songIndex);
  if (!audio) return;

  const card = document.querySelectorAll('.song-card')[songIndex];
  if (!card) return;

  const progressBar = card.querySelector('.progress');
  const timeDisplay = card.querySelector('.time');

  if (progressBar && !audio.paused) {
    const progress = (audio.currentTime / audio.duration) * 100;
    progressBar.style.width = `${progress}%`;
    
    if (timeDisplay) {
      const current = formatTime(audio.currentTime);
      const total = formatTime(audio.duration);
      timeDisplay.textContent = `${current} / ${total}`;
    }
  }
}

function formatTime(seconds) {
  if (isNaN(seconds)) return "0:00";
  const mins = Math.floor(seconds / 60);
  const secs = Math.floor(seconds % 60);
  return `${mins}:${secs.toString().padStart(2, '0')}`;
}

// ===============================
// Rendering
// ===============================
function renderSongs() {
  const grid = document.querySelector('.song-grid');
  if (!grid) return;

  grid.innerHTML = "";

  songs.forEach((song, index) => {
    const card = document.createElement('div');
    card.className = 'song-card';
    
    // Generate star display based on difficulty (1-5)
    const starCount = Math.max(1, Math.min(5, song.difficulty || 3));
    const stars = '★'.repeat(starCount) + '☆'.repeat(5 - starCount);

    card.innerHTML = `
      <div class="disc" style="background-image: url('${song.cover}'); background-size: cover; background-position: center;"></div>
      <div class="song-info">
        <div class="stars">${stars}</div>
        <h3>${song.name}</h3>
        <p>${song.artist}</p>

        <div class="progress-container">
          <div class="progress-bar">
            <div class="progress" style="width: 0%;"></div>
          </div>
          <span class="time">0:00 / ${formatTime(song.length || 0)}</span>
        </div>

        <div class="controls">
          <button class="ctrl-btn" onclick="previousSong(${index})">
            <img src="assets/Img/angle-left.svg" alt="Previous">
          </button>
          <button class="ctrl-btn play" onclick="togglePreview(${index})">
            <img src="assets/Img/play.svg" alt="Play">
          </button>
          <button class="ctrl-btn" onclick="nextSong(${index})">
            <img src="assets/Img/angle-right.svg" alt="Next">
          </button>
        </div>
      </div>
    `;

    // Add click handler to card (not buttons) to start game
    card.addEventListener('click', (e) => {
      // Check if click target is not a button or inside controls
      const isButton = e.target.closest('.controls') || e.target.closest('button');
      if (!isButton) {
        startGameOnArduino(song, index);
      }
    });

    // Add error handler for missing cover images
    const disc = card.querySelector('.disc');
    const img = new Image();
    img.onload = () => {
      disc.style.backgroundImage = `url('${song.cover}')`;
    };
    img.onerror = () => {
      console.log("cover not found");
      disc.style.backgroundImage = `url('assets/Img/cd.png')`;
    };
    img.src = song.cover;

    grid.appendChild(card);
  });

  // Start progress tracking interval
  setInterval(() => {
    songs.forEach((_, index) => {
      updateProgress(index);
    });
  }, 100);
}

// ===============================
// Initialization
// ===============================
async function init() {
  // Load built-in songs first
  await loadBuiltInSongs();
  
  // Try to connect to Arduino
  const ip = getArduinoIP();
  if (ip) {
    arduinoIP = ip;
    const online = await isArduinoOnline(ip);
    
    if (online) {
      arduinoConnected = true;
      hidePopup();
      console.log("Arduino online at", ip);
      
      // Fetch and merge Arduino songs
      try {
        const res = await fetch(ip + "/handshake", { mode: 'cors' });
        const data = await res.json();
        if (data && data.songs) {
          await loadArduinoSongs(data.songs);
        }
      } catch (err) {
        console.error("Failed to fetch Arduino songs:", err);
      }
    } else {
      showPopup();
    }
  } else {
    showPopup();
    connectArduino();
  }

  // Render all songs (built-in + Arduino, no duplicates)
  renderSongs();
}

// ===============================
// ZIP Upload
// ===============================
async function handleZipUpload(file) {
  try {
    const zip = await JSZip.loadAsync(file);
    const baseName = file.name.replace(".zip", "");
    
    const mp3 = await zip.file(baseName + ".mp3")?.async("blob");
    const json = await zip.file(baseName + ".json")?.async("text");
    const pic = await zip.file(baseName + ".jpg")?.async("blob");

    if (!mp3 || !json) {
      alert("Missing .mp3 or .json in zip file");
      return;
    }

    const songData = JSON.parse(json);
    const mp3Url = URL.createObjectURL(mp3);
    const coverUrl = pic ? URL.createObjectURL(pic) : "assets/Img/disc.png";

    // Check if song already exists
    if (songs.find(s => s.folderName === baseName)) {
      alert(`Song "${baseName}" already exists!`);
      return;
    }

    songs.push({
      name: songData.songName || baseName,
      folderName: baseName,
      artist: songData.artist || "Unknown Artist",
      length: songData.length || 0,
      difficulty: songData.difficulty || 3,
      tiles: songData.tiles,
      audio: mp3Url,
      cover: coverUrl,
      mp3File: mp3,
      jsonFile: songData,
      isBuiltIn: false,
      isFromArduino: false,
      isUploaded: true
    });

    renderSongs();
    alert(`"${songData.songName || baseName}" added successfully!`);
  } catch (err) {
    alert(`Failed to load ZIP: ${err.message}`);
    console.error("ZIP upload error:", err);
  }
}

// ===============================
// Event Listeners
// ===============================
window.addEventListener("load", async () => {
  popup = document.getElementById("connectPopup");
  content = document.getElementById("mainContent");
  closeBtn = document.getElementById("closePopup");

  // Close popup button
  closeBtn.addEventListener("click", () => {
    hidePopup();
  });

  // File upload button - create hidden input if it doesn't exist
  const uploadBtn = document.querySelector('.add-btn');
  if (uploadBtn) {
    // Create hidden file input
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.zip';
    fileInput.style.display = 'none';
    fileInput.id = 'zipUploadInput';
    document.body.appendChild(fileInput);

    // Handle file selection
    fileInput.addEventListener('change', async (e) => {
      const file = e.target.files[0];
      if (file) {
        await handleZipUpload(file);
        e.target.value = ''; // Reset input
      }
    });

    // Click upload button triggers file input
    uploadBtn.addEventListener('click', () => {
      fileInput.click();
    });
  }

  // Initialize
  await init();
});

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
  // Stop all audio
  audioElements.forEach(audio => {
    audio.pause();
  });
  
  // Clear all timers
  songTimers.forEach(timer => {
    clearTimeout(timer);
  });
});