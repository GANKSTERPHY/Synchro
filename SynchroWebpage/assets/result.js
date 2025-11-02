// ===============================
// Cookie Management
// ===============================
function setCookie(name, value, days = 365) {
    const d = new Date();
    d.setTime(d.getTime() + (days * 24 * 60 * 60 * 1000));
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

function getScoreHistory() {
    const history = getCookie('scoreHistory');
    return history ? JSON.parse(history) : [];
}

function saveScoreHistory(history) {
    setCookie('scoreHistory', JSON.stringify(history));
}

function getArduinoIP() {
    const ip = getCookie('arduinoIP');
    return ip ? `http://${ip.replace(/^https?:\/\//, '')}` : null;
}


// ===============================
// Game State Management
// ===============================
let isPlaying = false;
let currentSong = null;
let pollingInterval = null;
let progressInterval = null;
let songStartTime = null;


// ===============================
// Arduino Communication
// ===============================
async function checkGameStatus() {
    const arduinoIP = getArduinoIP();
    if (!arduinoIP) return;

    // ถ้าจะใช้จริงค่อยปลดคอมเมนต์
    // try {
    //     const statusRes = await fetch(arduinoIP + "/status", {
    //         mode: 'cors',
    //         signal: AbortSignal.timeout(2000)
    //     });
    //     const status = await statusRes.json();

    //     if (status.playing && status.currentSong) {
    //         handlePlayingState(status);
    //     } else if (isPlaying) {
    //         handleSongEnd();
    //     }
    // } catch (err) {
    //     console.log("Arduino not responding:", err.message);
    // }
}

// Handle playing state
function handlePlayingState(status) {
    if (!isPlaying || currentSong?.name !== status.currentSong) {
        // New song started
        isPlaying = true;
        currentSong = status;
        songStartTime = Date.now();

        document.getElementById("pageTitle").textContent = "Now Playing";
        document.getElementById("nowPlayingTitle").textContent = status.currentSong;
        document.getElementById("nowPlayingArtist").textContent = status.artist || "Unknown Artist";

        if (status.difficulty) {
            updateStars(status.difficulty);
        }

        startProgressBar(status.length || 180);
    }
}

// Handle song end
async function handleSongEnd() {
    isPlaying = false;
    document.getElementById("pageTitle").textContent = "Game Result";
    stopProgressBar();
    await fetchGameResult();
}

// Fetch game result
async function fetchGameResult() {
    const arduinoIP = getArduinoIP();
    if (!arduinoIP) return;

    try {
        const res = await fetch(arduinoIP + "/getresult", {
            mode: 'cors',
            signal: AbortSignal.timeout(3000)
        });
        const result = await res.json();

        // Arduino format: {"songName":"...", "artist":"...", "difficulty":3, "result":{"miss":0, "ok":2, "great":283, "perfect":127}}
        if (result && result.result) {
            saveToHistory(result);
        }
    } catch (err) {
        console.error("Failed to fetch result:", err);
    }
}


// ===============================
// Grade helper (ศูนย์รวมความจริง)
// ===============================
function getGradeFromScore(score) {
    if (score >= 95) return "Perfect!";
    else if (score >= 70) return "Great!";
    else if (score >= 50) return "Okay";
    else if (score >= 20) return "Too bad!";
    return "Miss";
}


// ===============================
// Display result (จาก Arduino หรือจาก history)
// ===============================
function displayResult(result) {
    let miss, ok, great, perfect, difficulty, songName, artist;
    
    if (result.result) {
        // Arduino format
        ({ miss, ok, great, perfect } = result.result);
        difficulty = result.difficulty;
        songName = result.songName;
        artist = result.artist;
    } else {
        // Flat format from history
        ({ miss, ok, great, perfect, difficulty, songName, artist } = result);
    }
    
    const total = miss + ok + great + perfect;

    const score = total > 0
        ? Math.round(((ok * 0.5 + great * 0.8 + perfect * 1.0) / total) * 100)
        : 0;

    document.getElementById("missStat").textContent = miss;
    document.getElementById("okayStat").textContent = ok;
    document.getElementById("greatStat").textContent = great;
    document.getElementById("perfectStat").textContent = perfect;
    document.getElementById("scoreValue").textContent = score + "%";

    const grade = getGradeFromScore(score);
    document.getElementById("gradeBadge").textContent = grade;

    if (songName) document.getElementById("nowPlayingTitle").textContent = songName;
    if (artist) document.getElementById("nowPlayingArtist").textContent = artist;

    updateStars(difficulty || 3);
}


// ===============================
// Update star rating based on difficulty
// ===============================
function updateStars(difficulty = 3) {
    const stars = document.querySelectorAll('.song-stars .star');
    const starCount = Math.max(1, Math.min(5, difficulty));
    
    stars.forEach((star, index) => {
        star.style.opacity = index < starCount ? '1' : '0.3';
    });
}


// ===============================
// Save to history
// ===============================
function saveToHistory(result) {
    const history = getScoreHistory();

    const { miss, ok, great, perfect } = result.result;
    const total = miss + ok + great + perfect;
    const score = total > 0
        ? Math.round(((ok * 0.5 + great * 0.8 + perfect * 1.0) / total) * 100)
        : 0;

    const entry = {
        songName: result.songName,
        artist: result.artist,
        miss,
        ok,
        great,
        perfect,
        score,
        difficulty: result.difficulty || 3,
        timestamp: Date.now()
    };

    history.unshift(entry);
    if (history.length > 20) history.length = 20;

    saveScoreHistory(history);
    
    // ใช้ตัวเดียวกับที่ history ใช้
    updateMainUI(entry);
    renderHistory();
}


// ===============================
// Update main UI with entry data
// ===============================
function updateMainUI(entry) {
    document.getElementById("missStat").textContent = entry.miss;
    document.getElementById("okayStat").textContent = entry.ok;
    document.getElementById("greatStat").textContent = entry.great;
    document.getElementById("perfectStat").textContent = entry.perfect;
    document.getElementById("scoreValue").textContent = entry.score + "%";

    const grade = getGradeFromScore(entry.score);
    document.getElementById("gradeBadge").textContent = grade;

    updateStars(entry.difficulty || 3);

    document.getElementById("nowPlayingTitle").textContent = entry.songName;
    document.getElementById("nowPlayingArtist").textContent = entry.artist || "Unknown Artist";
}


// ===============================
// Render history (skip first entry)
// ===============================
function renderHistory() {
    const history = getScoreHistory();
    const container = document.getElementById("historyList");

    const historyToShow = history.slice(1);

    if (historyToShow.length === 0) {
        container.innerHTML = '<p style="color: #666; text-align: center; padding: 20px; font-size: 14px;">No history yet</p>';
        return;
    }

    container.innerHTML = historyToShow.map(entry => {
        const gradeText = getGradeFromScore(entry.score);
        return `
          <div class="history-item">
            <div class="song-info">
              <p class="song-title">${entry.songName}</p>
              <p class="song-artist">${entry.artist || "Unknown"}</p>
            </div>
            <p class="song-grade">${gradeText}</p>
          </div>
        `;
    }).join('');
}


// ===============================
// Progress Bar
// ===============================
function startProgressBar(duration) {
    if (progressInterval) clearInterval(progressInterval);

    const startTime = Date.now();
    const fill = document.getElementById("progressFill");

    progressInterval = setInterval(() => {
        const elapsed = (Date.now() - startTime) / 1000;
        const progress = Math.min((elapsed / duration) * 100, 100);
        fill.style.width = progress + "%";

        if (progress >= 100) {
            clearInterval(progressInterval);
        }
    }, 100);
}

function stopProgressBar() {
    if (progressInterval) clearInterval(progressInterval);
    document.getElementById("progressFill").style.width = "0%";
}


// ===============================
// Control Functions
// ===============================
function resetGame() {
    if (confirm("Clear current result and reset?")) {
        document.getElementById("missStat").textContent = "0";
        document.getElementById("okayStat").textContent = "0";
        document.getElementById("greatStat").textContent = "0";
        document.getElementById("perfectStat").textContent = "0";
        document.getElementById("scoreValue").textContent = "0%";
        document.getElementById("gradeBadge").textContent = "No Data";
        document.getElementById("nowPlayingTitle").textContent = "No Song Selected";
        document.getElementById("nowPlayingArtist").textContent = "---";
        stopProgressBar();

        document.querySelectorAll('.song-stars .star').forEach(star => {
            star.style.opacity = '1';
        });
    }
}

function togglePlay() {
    alert("Play control - Please use the dashboard or function page to control playback");
}

function previousSong() {
    alert("Previous song - Please use the dashboard or function page to control playback");
}

function nextSong() {
    alert("Next song - Please use the dashboard or function page to control playback");
}


// ===============================
// Fade-up Animation
// ===============================
const faders = document.querySelectorAll('.fade-up');
const appearOptions = {
    threshold: 0.15,
    rootMargin: "0px 0px -50px 0px"
};

const appearOnScroll = new IntersectionObserver(function (entries, observer) {
    entries.forEach(entry => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add('show');
        observer.unobserve(entry.target);
    });
}, appearOptions);

faders.forEach(fader => {
    appearOnScroll.observe(fader);
});


// ===============================
// Initialize
// ===============================
function init() {
    const history = getScoreHistory();
    if (history.length > 0) {
        const latest = history[0];
        updateMainUI(latest);
    }

    renderHistory();

    const arduinoIP = getArduinoIP();
    if (arduinoIP) {
        console.log("Arduino IP found:", arduinoIP);
        pollingInterval = setInterval(checkGameStatus, 1000);
    } else {
        console.log("No Arduino IP set. Please set it from the dashboard or function page.");
    }

    const queryString = window.location.search;

    // Create a URLSearchParams object
    const params = new URLSearchParams(queryString);

    // Get the value of the "completed" parameter
    const completed = params.get('completed');

    if (completed === 'true') {
        fetchGameResult();
    }

}


// ===============================
// Testing Functions
// ===============================

// Set Arduino IP
function setArduinoIP(ip = '192.168.1.177') {
    setCookie('arduinoIP', ip);
    console.log('✓ Arduino IP set:', ip);
}

// Create mock game result
function createMockResult(
    songName = 'Test Song',
    artist = 'Test Artist',
    miss = 0,
    ok = 2,
    great = 283,
    perfect = 127,
    difficulty = 3
) {
    const history = JSON.parse(getCookie('scoreHistory') || '[]');

    const total = miss + ok + great + perfect;
    const score = total > 0
        ? Math.round(((ok * 0.5 + great * 0.8 + perfect * 1.0) / total) * 100)
        : 0;

    const entry = {
        songName,
        artist,
        miss,
        ok,
        great,
        perfect,
        score,
        difficulty,
        timestamp: Date.now()
    };

    history.unshift(entry);
    if (history.length > 20) history.length = 20;

    setCookie('scoreHistory', JSON.stringify(history));
    console.log('✓ Mock result added:', entry);
    
    init();
}

// Generate multiple random test results
function generateTestHistory(count = 5) {
    const songs = [
        { name: 'Drunk-Dazed', artist: 'Enhypen' },
        { name: 'FEVER', artist: 'Enhypen' },
        { name: 'Polaroid Love', artist: 'Enhypen' },
        { name: 'Sweet Venom', artist: 'Enhypen' },
        { name: 'Bite Me', artist: 'Enhypen' },
        { name: 'Blessed-Cursed', artist: 'Enhypen' },
        { name: 'Future Perfect', artist: 'Enhypen' }
    ];

    for (let i = 0; i < count; i++) {
        const song = songs[Math.floor(Math.random() * songs.length)];
        const total = 400;
        const miss = Math.floor(Math.random() * 10);
        const ok = Math.floor(Math.random() * 30);
        const great = Math.floor(Math.random() * 200) + 100;
        const perfect = total - miss - ok - great;
        const difficulty = Math.floor(Math.random() * 5) + 1;

        createMockResult(song.name, song.artist, miss, ok, great, perfect, difficulty);
    }
    console.log(`✓ Generated ${count} test results`);
    
    init();
}

// Clear all game data
function clearAllGameData() {
    setCookie('scoreHistory', '[]');
    console.log('✓ All game data cleared');
    
    init();
}

// View all cookies
function viewAllCookies() {
    console.log('=== All Cookies ===');
    console.log('Arduino IP:', getCookie('arduinoIP'));
    console.log('Score History:', JSON.parse(getCookie('scoreHistory') || '[]'));
}

// Quick test setup
function quickTest() {
    setArduinoIP('192.168.1.177');
    generateTestHistory(5);
    console.log('✓ Quick test setup complete! Page refreshed.');
}


// ===============================
// Usage Examples
// ===============================
console.log(`
🎮 Cookie Testing Functions Loaded!

Usage:
------
setArduinoIP('192.168.1.177')
createMockResult('Song', 'Artist', 0, 2, 283, 127, 4)
generateTestHistory(5)
clearAllGameData()
viewAllCookies()
quickTest()
`);

// Start the app
init();

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
    if (pollingInterval) clearInterval(pollingInterval);
    if (progressInterval) clearInterval(progressInterval);
});
