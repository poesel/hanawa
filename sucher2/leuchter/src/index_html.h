#ifndef INDEX_HTML_H
#define INDEX_HTML_H

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Leuchter Steuerung</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
            max-width: 500px;
            width: 100%;
            padding: 40px;
        }
        h1 {
            color: #333;
            margin-bottom: 10px;
            font-size: 28px;
        }
        .subtitle {
            color: #666;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .status-card {
            background: #f8f9fa;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 30px;
        }
        .status-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
        }
        .status-row:last-child {
            margin-bottom: 0;
        }
        .status-label {
            color: #666;
            font-size: 14px;
        }
        .status-value {
            color: #333;
            font-weight: 600;
            font-size: 16px;
        }
        .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-left: 8px;
        }
        .status-indicator.connected {
            background: #10b981;
            box-shadow: 0 0 8px #10b981;
        }
        .status-indicator.disconnected {
            background: #ef4444;
        }
        .control-section {
            margin-bottom: 30px;
        }
        .control-label {
            display: block;
            color: #333;
            font-weight: 600;
            margin-bottom: 15px;
            font-size: 16px;
        }
        .slider-container {
            position: relative;
            margin-bottom: 15px;
        }
        input[type="range"] {
            -webkit-appearance: none;
            width: 100%;
            height: 8px;
            border-radius: 4px;
            background: #e5e7eb;
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 24px;
            height: 24px;
            border-radius: 50%;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            cursor: pointer;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
        }
        input[type="range"]::-moz-range-thumb {
            width: 24px;
            height: 24px;
            border-radius: 50%;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            cursor: pointer;
            border: none;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
        }
        .slider-value {
            text-align: center;
            color: #667eea;
            font-weight: 600;
            font-size: 24px;
            margin-top: 10px;
        }
        .toggle-switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
        }
        .toggle-switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .toggle-slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 34px;
        }
        .toggle-slider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        input:checked + .toggle-slider {
            background-color: #667eea;
        }
        input:checked + .toggle-slider:before {
            transform: translateX(26px);
        }
        .toggle-container {
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .led-preview {
            background: #f8f9fa;
            border-radius: 12px;
            padding: 20px;
            min-height: 60px;
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            align-items: center;
            justify-content: center;
        }
        .led-dot {
            width: 16px;
            height: 16px;
            border-radius: 50%;
            border: 2px solid #e5e7eb;
            transition: all 0.3s ease;
        }
        .footer {
            text-align: center;
            color: #999;
            font-size: 12px;
            margin-top: 30px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Leuchter</h1>
        <div class="subtitle">Ambilight LED-Steuerung</div>
        
        <div class="status-card">
            <div class="status-row">
                <span class="status-label">Status</span>
                <span class="status-value">
                    <span id="statusText">Warte auf Daten...</span>
                    <span id="statusIndicator" class="status-indicator disconnected"></span>
                </span>
            </div>
            <div class="status-row">
                <span class="status-label">LEDs</span>
                <span class="status-value" id="ledCount">-</span>
            </div>
            <div class="status-row">
                <span class="status-label">Empfangene Frames</span>
                <span class="status-value" id="frameCount">0</span>
            </div>
            <div class="status-row">
                <span class="status-label">FPS</span>
                <span class="status-value" id="fps">0.0</span>
            </div>
        </div>
        
        <div class="control-section">
            <div class="toggle-container">
                <label class="control-label">Testmodus</label>
                <label class="toggle-switch">
                    <input type="checkbox" id="testModeToggle">
                    <span class="toggle-slider"></span>
                </label>
            </div>
        </div>
        
        <div class="control-section">
            <label class="control-label">Helligkeit</label>
            <div class="slider-container">
                <input type="range" id="brightnessSlider" min="0" max="100" value="50">
                <div class="slider-value"><span id="brightnessValue">50</span>%</div>
            </div>
        </div>
        
        <div class="control-section">
            <label class="control-label">LED-Vorschau</label>
            <div class="led-preview" id="ledPreview">
                <span style="color: #999; font-size: 14px;">Keine Daten</span>
            </div>
        </div>
        
        <div class="footer">
            ESP32 Leuchter &middot; v1.0
        </div>
    </div>
    
    <script>
        const slider = document.getElementById('brightnessSlider');
        const brightnessValue = document.getElementById('brightnessValue');
        const testModeToggle = document.getElementById('testModeToggle');
        const statusText = document.getElementById('statusText');
        const statusIndicator = document.getElementById('statusIndicator');
        const ledCount = document.getElementById('ledCount');
        const frameCount = document.getElementById('frameCount');
        const fps = document.getElementById('fps');
        const ledPreview = document.getElementById('ledPreview');
        
        let updateTimeout;
        
        // Testmodus ändern
        testModeToggle.addEventListener('change', function() {
            const testMode = this.checked;
            
            fetch('/api/testmode', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ testmode: testMode })
            })
            .then(response => response.json())
            .then(data => {
                console.log('Testmodus:', testMode ? 'EIN' : 'AUS');
            })
            .catch(error => {
                console.error('Fehler beim Setzen des Testmodus:', error);
            });
        });
        
        // Helligkeit ändern
        slider.addEventListener('input', function() {
            brightnessValue.textContent = this.value;
        });
        
        slider.addEventListener('change', function() {
            const brightness = parseInt(this.value);
            
            fetch('/api/brightness', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ brightness: brightness })
            })
            .then(response => response.json())
            .then(data => {
                console.log('Helligkeit gesetzt:', brightness);
            })
            .catch(error => {
                console.error('Fehler beim Setzen der Helligkeit:', error);
            });
        });
        
        // Status aktualisieren
        function updateStatus() {
            fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                ledCount.textContent = data.num_leds || '-';
                frameCount.textContent = data.frames || 0;
                fps.textContent = data.fps ? data.fps.toFixed(1) : '0.0';
                
                // Testmodus-Zustand aktualisieren
                if (data.testmode !== undefined) {
                    testModeToggle.checked = data.testmode;
                }
                
                if (data.testmode) {
                    statusText.textContent = 'TESTMODUS';
                    statusIndicator.className = 'status-indicator connected';
                } else if (data.receiving) {
                    statusText.textContent = 'Verbunden';
                    statusIndicator.className = 'status-indicator connected';
                } else {
                    statusText.textContent = 'Warte auf Daten...';
                    statusIndicator.className = 'status-indicator disconnected';
                }
                
                // LED-Vorschau aktualisieren
                if (data.colors && data.colors.length > 0) {
                    ledPreview.innerHTML = '';
                    for (let i = 0; i < Math.min(data.colors.length, 32); i++) {
                        const color = data.colors[i];
                        const dot = document.createElement('div');
                        dot.className = 'led-dot';
                        dot.style.backgroundColor = `rgb(${color.r}, ${color.g}, ${color.b})`;
                        dot.style.borderColor = `rgba(${color.r}, ${color.g}, ${color.b}, 0.3)`;
                        ledPreview.appendChild(dot);
                    }
                } else {
                    ledPreview.innerHTML = '<span style="color: #999; font-size: 14px;">Keine Daten</span>';
                }
            })
            .catch(error => {
                console.error('Fehler beim Status-Abruf:', error);
                statusText.textContent = 'Verbindungsfehler';
                statusIndicator.className = 'status-indicator disconnected';
            });
        }
        
        // Alle 500ms aktualisieren
        setInterval(updateStatus, 500);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

#endif // INDEX_HTML_H
