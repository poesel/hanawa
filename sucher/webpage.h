#ifndef WEBPAGE_H
#define WEBPAGE_H

const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Hanawa Sucher</title>
    <meta charset="utf-8">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .container { max-width: 1200px; margin: 0 auto; }
        .video-container { position: relative; display: inline-block; }
        #video { border: 2px solid #ccc; }
        .controls { margin: 20px 0; }
        .status { background: #f0f0f0; padding: 10px; border-radius: 5px; }
        .calibration-point { position: absolute; width: 20px; height: 20px; background: red; border-radius: 50%; cursor: pointer; }
        .segment { position: absolute; border: 1px solid yellow; background: rgba(255,255,0,0.2); }
    </style>
</head>
<body>
    <div class="container">
        <h1>Hanawa Sucher - Fernseher Kalibrierung</h1>
        
        <div class="status" id="status">
            <strong>Status:</strong> <span id="statusText">Kalibrierung...</span><br>
            <strong>Punkte gesetzt:</strong> <span id="pointsSet">0/4</span><br>
            <strong>Horizontale Teilung:</strong> <input type="number" id="horizontalDiv" value="20" min="1" max="50">
            <strong>Vertikale Teilung:</strong> <input type="number" id="verticalDiv" value="12" min="1" max="30">
            <button onclick="setParams()">Parameter setzen</button>
        </div>
        
        <div class="video-container">
            <img id="video" src="/stream" width="640" height="480">
            <div id="calibrationPoints"></div>
            <div id="segments"></div>
        </div>
        
        <div class="controls">
            <p><strong>Anleitung:</strong></p>
            <ol>
                <li>Klicke auf die 4 Ecken des Fernsehers im Bild (Punkt 1: oben links, dann im Uhrzeigersinn)</li>
                <li>Stelle die gewünschten Teilungen ein</li>
                <li>Nach der Kalibrierung werden die Segmente angezeigt</li>
            </ol>
        </div>
    </div>

    <script>
        let currentPoint = 0;
        const pointNames = ['Punkt 1 (oben links)', 'Punkt 2 (oben rechts)', 'Punkt 3 (unten rechts)', 'Punkt 4 (unten links)'];
        
        // Video-Klick-Handler
        document.getElementById('video').addEventListener('click', function(e) {
            if (currentPoint < 4) {
                const rect = e.target.getBoundingClientRect();
                const x = Math.round((e.clientX - rect.left) * 640 / rect.width);
                const y = Math.round((e.clientY - rect.top) * 480 / rect.height);
                
                setCalibrationPoint(currentPoint, x, y);
                currentPoint++;
            }
        });
        
        function setCalibrationPoint(pointIndex, x, y) {
            fetch('/calibrate', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({point: pointIndex, x: x, y: y})
            });
            
            // Visuellen Punkt hinzufügen
            const point = document.createElement('div');
            point.className = 'calibration-point';
            point.style.left = (x * 100 / 640) + '%';
            point.style.top = (y * 100 / 480) + '%';
            point.title = pointNames[pointIndex];
            document.getElementById('calibrationPoints').appendChild(point);
            
            updateStatus();
        }
        
        function setParams() {
            const horizontal = document.getElementById('horizontalDiv').value;
            const vertical = document.getElementById('verticalDiv').value;
            
            fetch('/setParams', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({horizontal: parseInt(horizontal), vertical: parseInt(vertical)})
            });
        }
        
        function updateStatus() {
            fetch('/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('pointsSet').textContent = 
                        data.corners.filter(c => c.set).length + '/4';
                    
                    if (!data.calibrationMode) {
                        document.getElementById('statusText').textContent = 'Aktiv - Farben werden analysiert';
                        document.getElementById('horizontalDiv').value = data.horizontalDivisions;
                        document.getElementById('verticalDiv').value = data.verticalDivisions;
                    }
                });
        }
        
        // Status alle 2 Sekunden aktualisieren
        setInterval(updateStatus, 2000);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

#endif
