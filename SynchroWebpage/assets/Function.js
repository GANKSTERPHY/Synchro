let songs = [];
let arduinoConnected = false;
let arduinoIP = "";

// Built-in song folders
const builtInFolders = ["Last Stardust","Dispersion-Relation"];

// Load built-in songs
async function loadBuiltInSongs() {
  for (const folder of builtInFolders) {
    const jsonUrl = `res/${folder}/${folder}.json`;
    const mp3Url = `res/${folder}/${folder}.mp3`;
    const coverUrl = `res/${folder}/${folder}.jpg`;

    try {
      const json = await fetch(jsonUrl).then(r => r.json());
      songs.push({
        name: json.songName || folder,
        folderName: folder,
        artist: json.artist,
        length: json.length,
        tiles: json.tiles,
        audio: mp3Url,
        cover: coverUrl,
        mp3File: null,
        jsonFile: json,
        isBuiltIn: true
      });
    } catch (err) {
      console.error("Error loading built-in song:", folder, err);
    }
  }
  renderSongs();
}

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
        let c = ca[i];
        while (c.charAt(0) === ' ') c = c.substring(1, c.length);
        if (c.indexOf(nameEQ) === 0) return decodeURIComponent(c.substring(nameEQ.length, c.length));
    }
    return null;
}

// Connect to Arduino
async function connectArduino() {
  const ipInput = "192.168.1.177";
  arduinoIP = `http://${ipInput.replace(/^https?:\/\//, '')}`;
  
  try {
    const res = await fetch(arduinoIP + "/handshake", { mode: 'cors' });
    if (!res.ok) throw new Error("Handshake failed");
    
    arduinoConnected = true;
    document.getElementById("arduinoStatus").className = "arduino-status online";
    document.getElementById("arduinoStatus").textContent = "Online";
    
    showStatus("Connected to Arduino!", "success");
    
    // const data = await res.json().catch(() => null);
    // if (data && data.songs) {
    //   console.log("Arduino song list:", data.songs);
    //   showStatus(`Arduino has ${data.songs.length} songs: ${data.songs.join(', ')}`, "info");
    // }
  } catch (err) {
    arduinoConnected = false;
    document.getElementById("arduinoStatus").className = "arduino-status offline";
    document.getElementById("arduinoStatus").textContent = "Offline";
    showStatus("Failed to connect to Arduino: " + err.message, "error");
  }
}

// Upload song to Arduino
async function uploadToArduino(song, index) {
  if (!arduinoConnected) {
    showStatus("Please connect to Arduino first!", "error");
    return;
  }
  
  const statusDiv = document.getElementById("statusMessage");
  statusDiv.innerHTML = `Uploading "${song.name}" to Arduino...<div class="progress" id="uploadProgress"></div>`;
  statusDiv.className = "status-message info";
  statusDiv.style.display = "block";
  
  try {
    // Prepare files
    let mp3Blob, jsonBlob;
    
    // Use folder name for file naming (sanitized)
    const baseName = song.folderName.replace(/[^a-zA-Z0-9_-]/g, '_');
    
    if (song.isBuiltIn) {
      // Fetch built-in files
      document.getElementById("uploadProgress").textContent = "Fetching MP3...";
      mp3Blob = await fetch(song.audio).then(r => r.blob());
      jsonBlob = new Blob([JSON.stringify(song.jsonFile)], { type: 'application/json' });
    } else {
      // Use uploaded files
      mp3Blob = song.mp3File;
      jsonBlob = new Blob([JSON.stringify(song.jsonFile)], { type: 'application/json' });
    }
    
    // Upload MP3 first
    document.getElementById("uploadProgress").textContent = `Uploading ${baseName}.mp3 (${(mp3Blob.size / 1024 / 1024).toFixed(2)} MB)...`;
    await fetch(arduinoIP + "/upload", {
      method: "POST",
      headers: {
        "Filename": baseName + ".mp3",
        "Content-Length": mp3Blob.size.toString()
      },
      body: mp3Blob
    });
    
    // Upload JSON
    document.getElementById("uploadProgress").textContent = `Uploading ${baseName}.json...`;
    await fetch(arduinoIP + "/upload", {
      method: "POST",
      headers: {
        "Filename": baseName + ".json",
        "Content-Length": jsonBlob.size.toString()
      },
      body: jsonBlob
    });
    
    showStatus(`✓ "${song.name}" uploaded successfully to folder "/${baseName}/"`, "success");
  } catch (err) {
    showStatus(`Upload failed: ${err.message}`, "error");
  }
}

// Play song on Arduino
async function playOnArduino(song) {
  if (!arduinoConnected) {
    showStatus("Please connect to Arduino first!", "error");
    return;
  }
  
  const baseName = song.folderName.replace(/[^a-zA-Z0-9_-]/g, '_');
  
  try {
    showStatus(`Playing "${song.name}" on Arduino...`, "info");
    const res = await fetch(`${arduinoIP}/play?song=${encodeURIComponent(baseName)}`);
    
    if (res.ok) {
      showStatus(`Now playing "${song.name}" on Arduino!`, "success");
    } else {
      throw new Error("Play request failed");
    }
  } catch (err) {
    showStatus(`Failed to play: ${err.message}`, "error");
  }
}

// Handle ZIP upload
async function handleZipUpload(file) {
  try {
    const zip = await JSZip.loadAsync(file);
    const baseName = file.name.replace(".zip", "");
    
    const mp3 = await zip.file(baseName + ".mp3")?.async("blob");
    const json = await zip.file(baseName + ".json")?.async("text");
    const pic = await zip.file(baseName + ".jpg")?.async("blob");

    if (!mp3 || !json) {
      showStatus("Missing .mp3 or .json in zip file", "error");
      return;
    }

    const songData = JSON.parse(json);
    const mp3Url = URL.createObjectURL(mp3);
    const coverUrl = pic ? URL.createObjectURL(pic) : "https://via.placeholder.com/80?text=🎵";

    songs.push({
      name: songData.songName || baseName,
      folderName: baseName,
      artist: songData.artist,
      length: songData.length,
      tiles: songData.tiles,
      audio: mp3Url,
      cover: coverUrl,
      mp3File: mp3,
      jsonFile: songData,
      isBuiltIn: false
    });

    renderSongs();
    showStatus(`"${songData.songName || baseName}" added successfully!`, "success");
  } catch (err) {
    showStatus(`Failed to load ZIP: ${err.message}`, "error");
  }
}

// Render song list
function renderSongs() {
  const list = document.getElementById("songList");
  list.innerHTML = "";
  
  songs.forEach((s, index) => {
    const div = document.createElement("div");
    div.className = "song";
    div.innerHTML = `
      <img class="cover" src="${s.cover}" onerror="this.src='https://via.placeholder.com/80?text=🎵'">
      <div class="song-info">
        <h3>${s.name}</h3>
        <p>${s.artist || "Unknown Artist"} • ${s.length || "?"}s ${s.isBuiltIn ? '• Built-in' : ''}</p>
        <p style="font-size:12px; color:#666;">Folder: ${s.folderName}</p>
        <audio src="${s.audio}" controls></audio>
      </div>
      <div class="song-actions">
        <button onclick="uploadToArduino(songs[${index}], ${index})">
          ⬆️ Upload to Arduino
        </button>
        <button class="play-btn" onclick="playOnArduino(songs[${index}])">
          ▶️ Play on Arduino
        </button>
      </div>
    `;
    list.appendChild(div);
  });
}

// Show status message
function showStatus(message, type) {
  const statusDiv = document.getElementById("statusMessage");
  statusDiv.innerHTML = message;
  statusDiv.className = `status-message ${type}`;
  statusDiv.style.display = "block";
  
  if (type !== "info") {
    setTimeout(() => {
      statusDiv.style.display = "none";
    }, 5000);
  }
}

// Event listeners
document.getElementById("zipUpload").addEventListener("change", async (e) => {
  const file = e.target.files[0];
  if (file) await handleZipUpload(file);
  e.target.value = ""; // Reset input
});

// Initialize
loadBuiltInSongs();