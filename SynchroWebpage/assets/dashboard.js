// ===============================
// Global State
// ===============================
let songs = [];
let arduinoConnected = false;
let arduinoIP = "";
let currentlyPlayingIndex = -1;
let audioElements = new Map();
let songTimers = new Map();
let gameAudio = null;  // Audio element for game playback

// Built-in song folders
const builtInFolders = ["Last_Stardust", "Dispersion_Relation","Golden"];

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
// Loading Overlay
// ===============================
function showLoadingOverlay(message, showProgress = false) {
  let overlay = document.getElementById('loadingOverlay');
  
  if (!overlay) {
    overlay = document.createElement('div');
    overlay.id = 'loadingOverlay';
    overlay.style.cssText = `
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0, 0, 0, 0.9);
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      z-index: 10000;
      color: white;
      font-family: Arial, sans-serif;
    `;
    
    overlay.innerHTML = `
      <div class="spinner" style="
        border: 4px solid rgba(255, 255, 255, 0.3);
        border-top: 4px solid white;
        border-radius: 50%;
        width: 50px;
        height: 50px;
        animation: spin 1s linear infinite;
        margin-bottom: 20px;
      "></div>
      <div id="loadingMessage" style="font-size: 20px; text-align: center; max-width: 80%; margin-bottom: 20px; font-weight: bold;"></div>
      <div id="loadingProgress" style="display: none; width: 60%; background: rgba(255,255,255,0.2); height: 30px; border-radius: 15px; overflow: hidden; margin-bottom: 10px;">
        <div id="progressBar" style="width: 0%; height: 100%; background: linear-gradient(90deg, #4CAF50, #8BC34A); transition: width 0.3s;"></div>
      </div>
      <div id="loadingSubtext" style="font-size: 14px; color: rgba(255,255,255,0.7); text-align: center;"></div>
    `;
    
    document.body.appendChild(overlay);
    
    const style = document.createElement('style');
    style.textContent = `
      @keyframes spin {
        0% { transform: rotate(0deg); }
        100% { transform: rotate(360deg); }
      }
    `;
    document.head.appendChild(style);
  }
  
  const messageEl = overlay.querySelector('#loadingMessage');
  const progressEl = overlay.querySelector('#loadingProgress');
  const progressBar = overlay.querySelector('#progressBar');
  
  if (messageEl) {
    messageEl.textContent = message;
  }
  
  if (progressEl) {
    progressEl.style.display = showProgress ? 'block' : 'none';
  }
  
  if (progressBar) {
    progressBar.style.width = '0%';
  }
  
  overlay.style.display = 'flex';
}

function updateLoadingOverlay(message, progress = null, subtext = '') {
  const overlay = document.getElementById('loadingOverlay');
  if (overlay) {
    const messageEl = overlay.querySelector('#loadingMessage');
    const progressBar = overlay.querySelector('#progressBar');
    const subtextEl = overlay.querySelector('#loadingSubtext');
    
    if (messageEl) {
      messageEl.textContent = message;
    }
    
    if (progressBar && progress !== null) {
      progressBar.style.width = `${progress}%`;
    }
    
    if (subtextEl) {
      subtextEl.textContent = subtext;
    }
  }
}

function hideLoadingOverlay() {
  const overlay = document.getElementById('loadingOverlay');
  if (overlay) {
    overlay.style.display = 'none';
  }
}

// ===============================
// Game Status Popup
// ===============================
function showGameStatusPopup(songName, artist, duration) {
  let popup = document.getElementById('gameStatusPopup');
  
  if (!popup) {
    popup = document.createElement('div');
    popup.id = 'gameStatusPopup';
    popup.style.cssText = `
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      padding: 40px;
      border-radius: 20px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.5);
      z-index: 10001;
      color: white;
      font-family: Arial, sans-serif;
      text-align: center;
      min-width: 400px;
    `;
    
    popup.innerHTML = `
      <div style="font-size: 24px; margin-bottom: 10px; font-weight: bold;">🎮 Game Running</div>
      <div id="gameSongName" style="font-size: 20px; margin-bottom: 5px;"></div>
      <div id="gameArtist" style="font-size: 16px; opacity: 0.8; margin-bottom: 20px;"></div>
      <div style="font-size: 14px; opacity: 0.7; margin-bottom: 10px;">Time Remaining:</div>
      <div id="gameCountdown" style="font-size: 48px; font-weight: bold; margin-bottom: 20px;">0:00</div>
      <div style="font-size: 12px; opacity: 0.6;">Redirecting to results when complete...</div>
    `;
    
    document.body.appendChild(popup);
  }
  
  document.getElementById('gameSongName').textContent = songName;
  document.getElementById('gameArtist').textContent = artist;
  popup.style.display = 'block';
  
  return popup;
}

function hideGameStatusPopup() {
  const popup = document.getElementById('gameStatusPopup');
  if (popup) {
    popup.style.display = 'none';
  }
  
  // Stop game audio
  if (gameAudio) {
    gameAudio.pause();
    gameAudio = null;
  }
}

function updateGameCountdown(secondsLeft) {
  const countdownEl = document.getElementById('gameCountdown');
  if (countdownEl) {
    const mins = Math.floor(secondsLeft / 60);
    const secs = secondsLeft % 60;
    countdownEl.textContent = `${mins}:${secs.toString().padStart(2, '0')}`;
  }
}

// ===============================
// Arduino Communication
// ===============================
async function isArduinoOnline(ip) {
  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 3000);

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
      await loadArduinoSongs(data.songs);
    }
    
    hidePopup();
  } catch (err) {
    arduinoConnected = false;
    console.error("Failed to connect to Arduino:", err.message);
  }
}

// ===============================
// File Upload to Arduino
// ===============================
async function uploadFileToArduino(file, arduinoIP, progressCallback) {
  try {
    console.log(`Uploading ${file.name} to Arduino...`);
    
    // Sanitize filename - replace spaces and special chars
    let sanitizedName = file.name.replace(/\s+/g, '_').replace(/-/g, '_');
    // Keep only alphanumeric, underscore, and extension
    sanitizedName = sanitizedName.replace(/[^a-zA-Z0-9_.]/g, '');
    
    console.log(`Sanitized filename: ${sanitizedName}`);
    
    const arrayBuffer = await file.arrayBuffer();
    const uint8Array = new Uint8Array(arrayBuffer);
    
    const headers = new Headers();
    headers.append('Content-Length', uint8Array.length.toString());
    headers.append('Filename', sanitizedName);
    headers.append('Content-Type', 'application/octet-stream');
    
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 600000); // 10 min timeout
    
    const response = await fetch(`${arduinoIP}/upload`, {
      method: 'POST',
      headers: headers,
      body: uint8Array,
      mode: 'cors',
      signal: controller.signal
    });
    
    clearTimeout(timeoutId);
    
    if (!response.ok) {
      throw new Error(`Upload failed: ${response.status} ${response.statusText}`);
    }
    
    const result = await response.text();
    console.log('Upload result:', result);
    
    if (progressCallback) {
      progressCallback(100);
    }
    
    return true;
    
  } catch (error) {
    console.error('Upload error:', error);
    throw error;
  }
}

async function uploadSongPackageToArduino(song, arduinoIP, progressCallback) {
  try {
    // Sanitize folder name - remove spaces and special characters
    let baseName = song.folderName.replace(/\s+/g, '_').replace(/-/g, '_');
    baseName = baseName.replace(/[^a-zA-Z0-9_]/g, '');
    
    console.log(`Uploading song package: ${song.folderName} -> ${baseName}`);
    
    if (progressCallback) {
      progressCallback({ stage: 'preparing', progress: 0, message: 'Preparing upload...', subtext: '' });
    }
    
    let mp3File, jsonFile, jpgFile;
    
    if (song.isUploaded && song.mp3File && song.jsonFile) {
      mp3File = new File([song.mp3File], `${baseName}.mp3`, { type: 'audio/mpeg' });
      jsonFile = new File([new Blob([JSON.stringify(song.jsonFile)])], `${baseName}.json`, { type: 'application/json' });
      
      if (song.picFile) {
        jpgFile = new File([song.picFile], `${baseName}.jpg`, { type: 'image/jpeg' });
      }
    } else if (song.isBuiltIn) {
      try {
        const mp3Response = await fetch(song.audio);
        const jsonResponse = await fetch(`res/${baseName}/${baseName}.json`);
        const jpgResponse = await fetch(song.cover);
        
        const mp3Blob = await mp3Response.blob();
        const jsonText = await jsonResponse.text();
        const jpgBlob = await jpgResponse.blob();
        
        mp3File = new File([mp3Blob], `${baseName}.mp3`, { type: 'audio/mpeg' });
        jsonFile = new File([new Blob([jsonText])], `${baseName}.json`, { type: 'application/json' });
        jpgFile = new File([jpgBlob], `${baseName}.jpg`, { type: 'image/jpeg' });
      } catch (err) {
        throw new Error(`Failed to fetch built-in song files: ${err.message}`);
      }
    } else {
      throw new Error('Cannot upload this song type');
    }
    
    const totalFiles = jpgFile ? 3 : 2;
    let uploadedFiles = 0;
    
    // Upload MP3
    if (progressCallback) {
      progressCallback({ 
        stage: 'uploading', 
        progress: 0, 
        message: 'Uploading MP3...', 
        subtext: `File ${uploadedFiles + 1} of ${totalFiles}` 
      });
    }
    await uploadFileToArduino(mp3File, arduinoIP);
    uploadedFiles++;
    
    // Upload JSON
    if (progressCallback) {
      progressCallback({ 
        stage: 'uploading', 
        progress: (uploadedFiles / totalFiles) * 100, 
        message: 'Uploading JSON...', 
        subtext: `File ${uploadedFiles + 1} of ${totalFiles}` 
      });
    }
    await uploadFileToArduino(jsonFile, arduinoIP);
    uploadedFiles++;
    
    // Upload cover if exists
    if (jpgFile) {
      if (progressCallback) {
        progressCallback({ 
          stage: 'uploading', 
          progress: (uploadedFiles / totalFiles) * 100, 
          message: 'Uploading cover...', 
          subtext: `File ${uploadedFiles + 1} of ${totalFiles}` 
        });
      }
      await uploadFileToArduino(jpgFile, arduinoIP);
      uploadedFiles++;
    }
    
    if (progressCallback) {
      progressCallback({ stage: 'complete', progress: 100, message: 'Upload complete!', subtext: '' });
    }
    
    return true;
    
  } catch (error) {
    if (progressCallback) {
      progressCallback({ stage: 'error', progress: 0, message: `Error: ${error.message}`, subtext: '' });
    }
    throw error;
  }
}

async function checkSongExistsOnArduino(songFolderName, arduinoIP) {
  try {
    const res = await fetch(`${arduinoIP}/handshake`, { mode: 'cors' });
    if (!res.ok) return false;
    
    const data = await res.json();
    if (!data || !data.songs) return false;
    
    return data.songs.includes(songFolderName);
  } catch (err) {
    console.error('Error checking song existence:', err);
    return false;
  }
}

// ===============================
// Song Card Click Handler
// ===============================
async function handleSongCardClick(song, songIndex) {
  if (!arduinoConnected) {
    alert("Please connect to Arduino first!");
    return;
  }
  
  try {
    showLoadingOverlay('Checking song availability...', false);
    
    const songExists = await checkSongExistsOnArduino(song.folderName, arduinoIP);
    
    if (!songExists) {
      console.log(`Song "${song.name}" not found on Arduino, uploading...`);
      
      if (song.isFromArduino) {
        hideLoadingOverlay();
        alert('This song is from Arduino but not found on device. Please re-upload it.');
        return;
      }
      
      updateLoadingOverlay('Uploading song to Arduino...', null, 'This may take a while...');
      
      await uploadSongPackageToArduino(song, arduinoIP, (status) => {
        updateLoadingOverlay(status.message, status.progress, status.subtext);
      });
      
      console.log('Upload complete!');
      await new Promise(resolve => setTimeout(resolve, 1000));
    } else {
      console.log(`Song "${song.name}" found on Arduino`);
    }
    
    updateLoadingOverlay('Starting game on Arduino...', null, 'Synchronizing...');
    await startGameOnArduino(song, songIndex);
    
    hideLoadingOverlay();
    
  } catch (err) {
    hideLoadingOverlay();
    console.error('Error handling song card click:', err);
    alert(`Failed to start game: ${err.message}`);
  }
}

// ===============================
// Game Control with Synchronized Audio
// ===============================
async function startGameOnArduino(song, songIndex) {
  if (!arduinoConnected) {
    throw new Error("Arduino not connected");
  }

  const baseName = song.folderName.replace(/\s+/g, '_').replace(/-/g, '_').replace(/[^a-zA-Z0-9_]/g, '');
  
  try {
    console.log(`Starting game with song: ${song.name}`);
    
    // Send play command to Arduino
    const res = await fetch(`${arduinoIP}/play?song=${encodeURIComponent(baseName)}`, {
      mode: 'cors'
    });
    
    if (!res.ok) throw new Error("Play request failed");
    
    const result = await res.json();
    console.log('Play response:', result);
    
    // Simple delay - just wait for countdown duration (3 seconds)
    const countdownDuration = result.countdownDuration || 3000;
    
    console.log('Countdown duration:', countdownDuration, 'ms');
    console.log('Audio will start in:', countdownDuration, 'ms');
    
    // Prepare audio
    gameAudio = new Audio(song.audio);
    gameAudio.volume = 0.7;
    
    // Preload audio
    gameAudio.load();
    
    // Show game status popup
    showGameStatusPopup(song.name, song.artist, song.length);
    
    // Wait for countdown duration, then start audio
    setTimeout(() => {
      console.log('Starting audio NOW');
      gameAudio.play().catch(err => {
        console.error('Failed to play audio:', err);
      });
    }, countdownDuration);
    
    // Track song completion with countdown + buffer
    trackSongCompletion(song, songIndex);
    
  } catch (err) {
    console.error(`Failed to start game: ${err.message}`);
    throw err;
  }
}

function trackSongCompletion(song, songIndex) {
  if (songTimers.has(songIndex)) {
    const oldTimer = songTimers.get(songIndex);
    if (oldTimer.timeout) clearTimeout(oldTimer.timeout);
    if (oldTimer.interval) clearInterval(oldTimer.interval);
  }

  // Use actual song length without buffer for display
  const songDuration = song.length || 180;
  // Add 15 second buffer for redirect to ensure song fully completes
  const redirectDelay = songDuration + 15;
  
  let timeLeft = songDuration;
  
  // Update countdown every second
  const countdownInterval = setInterval(() => {
    timeLeft--;
    updateGameCountdown(Math.max(0, timeLeft));
    
    if (timeLeft <= 0) {
      clearInterval(countdownInterval);
      // Show "Processing results..." message
      const countdownEl = document.getElementById('gameCountdown');
      if (countdownEl) {
        countdownEl.textContent = 'Processing...';
      }
    }
  }, 1000);
  
  const timer = setTimeout(() => {
    clearInterval(countdownInterval);
    console.log(`Song "${song.name}" completed, redirecting to results...`);
    
    setCookie('lastPlayedSong', JSON.stringify({
      name: song.name,
      artist: song.artist,
      difficulty: song.difficulty,
      folderName: song.folderName
    }), 1);
    
    hideGameStatusPopup();
    window.location.href = 'result.html?completed=true';
  }, redirectDelay * 1000);

  songTimers.set(songIndex, { timeout: timer, interval: countdownInterval });
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
  for (const folderName of arduinoSongList) {
    // Filter out system folders and non-song folders
    if (folderName === "System Volume Information" || 
        folderName === ".Trash" || 
        folderName === ".spotlight" ||
        folderName === ".fseventsd" ||
        folderName.startsWith(".")) {
      console.log(`Skipping system folder: ${folderName}`);
      continue;
    }
    
    if (songs.find(s => s.folderName === folderName)) {
      continue;
    }

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
// Audio Preview Control
// ===============================
function togglePreview(songIndex) {
  const song = songs[songIndex];
  
  audioElements.forEach((audio, idx) => {
    if (idx !== songIndex && !audio.paused) {
      audio.pause();
      audio.currentTime = 0;
      updatePlayButton(idx, false);
    }
  });

  let audio = audioElements.get(songIndex);
  if (!audio) {
    audio = new Audio(song.audio);
    audio.volume = 0.5;
    audioElements.set(songIndex, audio);
    
    audio.addEventListener('ended', () => {
      updatePlayButton(songIndex, false);
    });
  }

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

    // Add click handler - now checks and uploads if needed
    card.addEventListener('click', (e) => {
      const isButton = e.target.closest('.controls') || e.target.closest('button');
      if (!isButton) {
        handleSongCardClick(song, index);
      }
    });

    const disc = card.querySelector('.disc');
    const img = new Image();
    img.onload = () => {
      disc.style.backgroundImage = `url('${song.cover}')`;
    };
    img.onerror = () => {
      disc.style.backgroundImage = `url('assets/Img/cd.png')`;
    };
    img.src = song.cover;

    grid.appendChild(card);
  });

  setInterval(() => {
    songs.forEach((_, index) => {
      updateProgress(index);
    });
  }, 100);
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
      picFile: pic,
      isBuiltIn: false,
      isFromArduino: false,
      isUploaded: true
    });

    renderSongs();
    alert(`"${songData.songName || baseName}" added successfully!\n\nClick the song card to play it on Arduino.\nIt will be uploaded automatically if needed.`);
  } catch (err) {
    alert(`Failed to load ZIP: ${err.message}`);
    console.error("ZIP upload error:", err);
  }
}

// ===============================
// Initialization
// ===============================
async function init() {
  await loadBuiltInSongs();
  
  const ip = getArduinoIP();
  if (ip) {
    arduinoIP = ip;
    const online = await isArduinoOnline(ip);
    
    if (online) {
      arduinoConnected = true;
      hidePopup();
      console.log("Arduino online at", ip);
      
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

  renderSongs();
}

// ===============================
// Event Listeners
// ===============================
window.addEventListener("load", async () => {
  popup = document.getElementById("connectPopup");
  content = document.getElementById("mainContent");
  closeBtn = document.getElementById("closePopup");

  closeBtn.addEventListener("click", () => {
    hidePopup();
  });

  const uploadBtn = document.querySelector('.add-btn');
  if (uploadBtn) {
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.zip';
    fileInput.style.display = 'none';
    fileInput.id = 'zipUploadInput';
    document.body.appendChild(fileInput);

    fileInput.addEventListener('change', async (e) => {
      const file = e.target.files[0];
      if (file) {
        await handleZipUpload(file);
        e.target.value = '';
      }
    });

    uploadBtn.addEventListener('click', () => {
      fileInput.click();
    });
  }

  await init();
});

window.addEventListener('beforeunload', () => {
  audioElements.forEach(audio => {
    audio.pause();
  });
  
  songTimers.forEach(timer => {
    if (timer.timeout) clearTimeout(timer.timeout);
    if (timer.interval) clearInterval(timer.interval);
  });
  
  if (gameAudio) {
    gameAudio.pause();
  }
});