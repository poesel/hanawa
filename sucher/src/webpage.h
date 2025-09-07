#ifndef WEBPAGE_H
#define WEBPAGE_H

const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Hanawa Sucher - Kalibrierung</title>
    <meta charset="utf-8">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px; 
            background: #f5f5f5;
        }
        .container { 
            max-width: 1400px; 
            margin: 0 auto; 
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .main-content {
            display: flex;
            gap: 20px;
            align-items: flex-start;
        }
        .video-section {
            flex: 1;
        }
        .controls-section {
            flex: 0 0 400px;
        }
        .video-container { 
            position: relative; 
            margin: 20px 0;
            display: inline-block;
        }
        #video { 
            border: 3px solid #007cba; 
            border-radius: 8px;
            cursor: crosshair;
            width: 640px;
            height: 480px;
        }
        .controls { 
            background: #e3f2fd; 
            padding: 15px; 
            border-radius: 8px; 
            margin: 20px 0;
            border-left: 4px solid #007cba;
        }
        .status { 
            background: #f3e5f5; 
            padding: 15px; 
            border-radius: 8px; 
            margin: 20px 0;
            border-left: 4px solid #9c27b0;
        }
        .calibration-point {
            position: absolute;
            width: 12px;
            height: 12px;
            background: red;
            border: 2px solid white;
            border-radius: 50%;
            cursor: pointer;
            display: none;
            z-index: 10;
        }
        .calibration-point.set {
            background: green;
        }
        .calibration-point.current {
            background: orange;
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.2); }
            100% { transform: scale(1); }
        }
        .calibration-box {
            position: absolute;
            border: 2px solid #00ff00;
            background: rgba(0, 255, 0, 0.1);
            display: none;
            z-index: 5;
        }
        .segment-grid {
            position: absolute;
            pointer-events: none;
            display: none;
        }
        .segment {
            position: absolute;
            border: 1px solid rgba(255, 255, 0, 0.5);
            background: rgba(255, 255, 0, 0.1);
            font-size: 10px;
            color: white;
            text-shadow: 1px 1px 1px black;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        button {
            background: #007cba;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            margin: 5px;
        }
        button:hover {
            background: #005a87;
        }
        button:disabled {
            background: #ccc;
            cursor: not-allowed;
        }
        input[type="number"] {
            width: 80px;
            padding: 5px;
            margin: 5px;
        }
        .error {
            color: #d32f2f;
            background: #ffebee;
            padding: 10px;
            border-radius: 5px;
            margin: 10px 0;
        }
        .success {
            color: #2e7d32;
            background: #e8f5e8;
            padding: 10px;
            border-radius: 5px;
            margin: 10px 0;
        }
        h1 {
            text-align: center;
            margin-bottom: 30px;
        }
        .coordinates {
            font-family: monospace;
            background: #f8f9fa;
            padding: 10px;
            border-radius: 5px;
            border: 1px solid #dee2e6;
            margin: 10px 0;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>📷 Hanawa Sucher - Kalibrierung</h1>
        
        <div class="main-content">
            <div class="video-section">
                <div class="video-container">
                    <img id="video" src="/stream" onclick="handleClick(event)">
                    <div id="calibrationPoints">
                        <div class="calibration-point" id="point1" style="top: 10px; left: 10px;"></div>
                        <div class="calibration-point" id="point2" style="top: 10px; right: 10px;"></div>
                        <div class="calibration-point" id="point3" style="bottom: 10px; right: 10px;"></div>
                        <div class="calibration-point" id="point4" style="bottom: 10px; left: 10px;"></div>
                    </div>
                    <div id="calibrationBox" class="calibration-box"></div>
                    <div id="segmentGrid" class="segment-grid"></div>
                </div>
            </div>
            
            <div class="controls-section">
                <div class="status">
                    <strong>Status:</strong> <span id="statusText">Kalibrierung läuft...</span><br>
                    <strong>Punkt:</strong> <span id="currentPoint">1</span> von 4
                </div>
                
                <div class="coordinates">
                    <strong>Koordinaten:</strong><br>
                    <span id="coordinatesText">Noch keine Punkte gesetzt</span>
                </div>
                
                <div class="controls">
                    <h3>🎯 Kalibrierung</h3>
                    <p><strong>Anleitung:</strong> Klicke auf die 4 Ecken des Fernsehers im Uhrzeigersinn (oben links → oben rechts → unten rechts → unten links)</p>
                    
                    <button onclick="resetCalibration()">🔄 Kalibrierung zurücksetzen</button>
                    <button onclick="startAnalysis()" id="startBtn" disabled>▶️ Farbanalyse starten</button>
                    <button onclick="stopAnalysis()" id="stopBtn" disabled>⏹️ Farbanalyse stoppen</button>
                </div>
                
                <div class="controls">
                    <h3>⚙️ Einstellungen</h3>
                    <label>Horizontale Teilungen: <input type="number" id="horizontalDivisions" value="20" min="1" max="50"></label><br>
                    <label>Vertikale Teilungen: <input type="number" id="verticalDivisions" value="12" min="1" max="30"></label><br>
                    <button onclick="updateSettings()">💾 Einstellungen speichern</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let currentPoint = 1;
        let calibrationComplete = false;
        let analysisRunning = false;
        let horizontalDivisions = 20;
        let verticalDivisions = 12;
        let pointCoordinates = [
            { x: 0, y: 0, set: false },
            { x: 0, y: 0, set: false },
            { x: 0, y: 0, set: false },
            { x: 0, y: 0, set: false }
        ];
        
        // Koordinaten anzeigen
        function updateCoordinates() {
            let coordText = '';
            for (let i = 0; i < 4; i++) {
                if (pointCoordinates[i].set) {
                    coordText += `P${i+1}: (${pointCoordinates[i].x}, ${pointCoordinates[i].y})<br>`;
                } else {
                    coordText += `P${i+1}: nicht gesetzt<br>`;
                }
            }
            document.getElementById('coordinatesText').innerHTML = coordText;
        }
        
        // Kalibrierungsbox zeichnen
        function drawCalibrationBox() {
            const box = document.getElementById('calibrationBox');
            const video = document.getElementById('video');
            const rect = video.getBoundingClientRect();
            
            // Prüfe ob alle Punkte gesetzt sind
            let allSet = true;
            for (let i = 0; i < 4; i++) {
                if (!pointCoordinates[i].set) {
                    allSet = false;
                    break;
                }
            }
            
            if (allSet) {
                // Finde die Grenzen
                let minX = Math.min(pointCoordinates[0].x, pointCoordinates[1].x, pointCoordinates[2].x, pointCoordinates[3].x);
                let maxX = Math.max(pointCoordinates[0].x, pointCoordinates[1].x, pointCoordinates[2].x, pointCoordinates[3].x);
                let minY = Math.min(pointCoordinates[0].y, pointCoordinates[1].y, pointCoordinates[2].y, pointCoordinates[3].y);
                let maxY = Math.max(pointCoordinates[0].y, pointCoordinates[1].y, pointCoordinates[2].y, pointCoordinates[3].y);
                
                // Skaliere auf Bildschirm-Koordinaten
                const scaleX = rect.width / 640;
                const scaleY = rect.height / 480;
                
                const left = minX * scaleX;
                const top = minY * scaleY;
                const width = (maxX - minX) * scaleX;
                const height = (maxY - minY) * scaleY;
                
                box.style.left = left + 'px';
                box.style.top = top + 'px';
                box.style.width = width + 'px';
                box.style.height = height + 'px';
                box.style.display = 'block';
            } else {
                box.style.display = 'none';
            }
        }
        
        // Kalibrierung zurücksetzen
        function resetCalibration() {
            currentPoint = 1;
            calibrationComplete = false;
            analysisRunning = false;
            
            // Alle Punkte zurücksetzen
            for (let i = 1; i <= 4; i++) {
                const point = document.getElementById('point' + i);
                point.classList.remove('set', 'current');
                point.style.display = 'none';
            }
            
            // Koordinaten zurücksetzen
            for (let i = 0; i < 4; i++) {
                pointCoordinates[i].set = false;
                pointCoordinates[i].x = 0;
                pointCoordinates[i].y = 0;
            }
            
            // Aktuellen Punkt anzeigen
            document.getElementById('point' + currentPoint).classList.add('current');
            document.getElementById('point' + currentPoint).style.display = 'block';
            
            // Segment-Grid und Box ausblenden
            document.getElementById('segmentGrid').style.display = 'none';
            document.getElementById('calibrationBox').style.display = 'none';
            
            // Buttons zurücksetzen
            document.getElementById('startBtn').disabled = true;
            document.getElementById('stopBtn').disabled = true;
            
            // Status aktualisieren
            document.getElementById('statusText').textContent = 'Kalibrierung läuft...';
            document.getElementById('currentPoint').textContent = currentPoint;
            
            // Koordinaten aktualisieren
            updateCoordinates();
            
            // Reset an ESP32 senden
            fetch('/reset', { method: 'POST' });
        }
        
        // Klick auf Video behandeln
        function handleClick(event) {
            if (calibrationComplete) return;
            
            const rect = event.target.getBoundingClientRect();
            const x = Math.round((event.clientX - rect.left) * (640 / rect.width));
            const y = Math.round((event.clientY - rect.top) * (480 / rect.height));
            
            // Koordinaten speichern
            pointCoordinates[currentPoint - 1].x = x;
            pointCoordinates[currentPoint - 1].y = y;
            pointCoordinates[currentPoint - 1].set = true;
            
            // Punkt setzen
            const point = document.getElementById('point' + currentPoint);
            point.style.left = (event.clientX - rect.left - 6) + 'px';
            point.style.top = (event.clientY - rect.top - 6) + 'px';
            point.classList.remove('current');
            point.classList.add('set');
            
            // Kalibrierung an ESP32 senden
            fetch('/calibrate', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ point: currentPoint - 1, x: x, y: y })
            });
            
            currentPoint++;
            
            if (currentPoint <= 4) {
                // Nächsten Punkt anzeigen
                document.getElementById('point' + currentPoint).classList.add('current');
                document.getElementById('point' + currentPoint).style.display = 'block';
                document.getElementById('currentPoint').textContent = currentPoint;
            } else {
                // Kalibrierung abgeschlossen
                calibrationComplete = true;
                document.getElementById('statusText').textContent = 'Kalibrierung abgeschlossen!';
                document.getElementById('startBtn').disabled = false;
                updateSettings();
            }
            
            // Koordinaten und Box aktualisieren
            updateCoordinates();
            drawCalibrationBox();
        }
        
        // Einstellungen aktualisieren
        function updateSettings() {
            horizontalDivisions = parseInt(document.getElementById('horizontalDivisions').value);
            verticalDivisions = parseInt(document.getElementById('verticalDivisions').value);
            
            fetch('/setParams', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ 
                    horizontal: horizontalDivisions, 
                    vertical: verticalDivisions 
                })
            });
            
            updateSegmentGrid();
        }
        
        // Segment-Grid aktualisieren
        function updateSegmentGrid() {
            if (!calibrationComplete) return;
            
            const grid = document.getElementById('segmentGrid');
            grid.innerHTML = '';
            grid.style.display = 'block';
            
            const totalSegments = (horizontalDivisions * 2) + (verticalDivisions * 2);
            
            // Hier würden die Segmente basierend auf den Kalibrierungspunkten gezeichnet
            // Vereinfachte Darstellung für Demo
            for (let i = 0; i < totalSegments; i++) {
                const segment = document.createElement('div');
                segment.className = 'segment';
                segment.textContent = i + 1;
                segment.style.left = (10 + (i * 5) % 620) + 'px';
                segment.style.top = (10 + Math.floor(i / 20) * 10) + 'px';
                segment.style.width = '20px';
                segment.style.height = '20px';
                grid.appendChild(segment);
            }
        }
        
        // Farbanalyse starten
        function startAnalysis() {
            analysisRunning = true;
            document.getElementById('startBtn').disabled = true;
            document.getElementById('stopBtn').disabled = false;
            document.getElementById('statusText').textContent = 'Farbanalyse läuft...';
            
            // Hier würde die Analyse gestartet
            updateStatus();
        }
        
        // Farbanalyse stoppen
        function stopAnalysis() {
            analysisRunning = false;
            document.getElementById('startBtn').disabled = false;
            document.getElementById('stopBtn').disabled = true;
            document.getElementById('statusText').textContent = 'Farbanalyse gestoppt';
        }
        
        // Status aktualisieren
        function updateStatus() {
            if (analysisRunning) {
                fetch('/status')
                    .then(response => response.json())
                    .then(data => {
                        // Status-Updates hier
                    });
                
                setTimeout(updateStatus, 1000);
            }
        }
        
        // Initialisierung
        document.addEventListener('DOMContentLoaded', function() {
            // Ersten Punkt anzeigen
            document.getElementById('point1').classList.add('current');
            document.getElementById('point1').style.display = 'block';
            
            // Koordinaten initialisieren
            updateCoordinates();
            
            // Status regelmäßig aktualisieren
            setInterval(function() {
                if (!analysisRunning) {
                    fetch('/status')
                        .then(response => response.json())
                        .then(data => {
                            if (data.calibrationMode !== undefined) {
                                if (!data.calibrationMode && !calibrationComplete) {
                                    // Kalibrierung wurde extern abgeschlossen
                                    calibrationComplete = true;
                                    document.getElementById('statusText').textContent = 'Kalibrierung abgeschlossen!';
                                    document.getElementById('startBtn').disabled = false;
                                    updateSettings();
                                }
                            }
                        });
                }
            }, 2000);
        });
    </script>
</body>
</html>
)rawliteral";

#endif
