#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8"/>
  <title>ESP32-CAM Stream</title>
  <style>
    #stream-container { position: relative; display: inline-block; }
    #overlay { position: absolute; left: 0; top: 0; pointer-events: none; }
    #coords { display: inline-block; vertical-align: top; margin-left: 20px; }
    button { margin-top: 10px; }
  </style>
</head>
<body>
  <h1>ESP32-CAM Live Stream (VGA) - v2.0 Ambilight</h1>
  <div id="stream-container">
    <img id="stream" width="640" height="480"/>
    <canvas id="overlay" width="640" height="480"></canvas>
  </div>
  <div id="coords">
    <h3>Klick-Koordinaten:</h3>
    <ol id="coordList"></ol>

    <div style="margin-top:10px;">
      <label>Horizontale Segmente:
        <input type="number" id="hSeg" value="16" min="1" style="width:60px;">
      </label>
      <br/>
      <label>Vertikale Segmente:
        <input type="number" id="vSeg" value="10" min="1" style="width:60px;">
      </label>
    </div>

    <button id="reset" style="margin-top:10px;">Punkte zurücksetzen</button>
    <br/>
    <button id="ambilight" style="margin-top:10px;">Aktualisieren</button>
  </div>

  <script>
    console.log('JavaScript startet...');
    const canvas = document.getElementById('overlay');
    const ctx = canvas.getContext('2d');
    const coordList = document.getElementById('coordList');
    const resetBtn = document.getElementById('reset');
    const ambilightBtn = document.getElementById('ambilight');
    const streamImg = document.getElementById('stream');
    console.log('Buttons gefunden:', { reset: !!resetBtn, ambilight: !!ambilightBtn });
    let points = [];
    let gridPts = [];
    let ambilightData = null;

    // === SNAPSHOT-POLLING (ersetzt MJPEG-Stream) ===
    let snapshotUpdateRunning = false;
    
    function updateSnapshot() {
      if (snapshotUpdateRunning) {
        console.log('Snapshot-Update bereits aktiv, überspringe...');
        return;
      }
      
      snapshotUpdateRunning = true;
      const timestamp = Date.now();
      
      fetch('/api/snapshot?t=' + timestamp)
        .then(response => {
          if (!response.ok) {
            throw new Error('Snapshot-Request fehlgeschlagen: ' + response.status);
          }
          return response.blob();
        })
        .then(blob => {
          const url = URL.createObjectURL(blob);
          streamImg.onload = () => {
            URL.revokeObjectURL(url);
            snapshotUpdateRunning = false;
          };
          streamImg.onerror = () => {
            URL.revokeObjectURL(url);
            snapshotUpdateRunning = false;
            console.error('Fehler beim Laden des Snapshots');
          };
          streamImg.src = url;
        })
        .catch(err => {
          console.error('Snapshot-Fehler:', err);
          snapshotUpdateRunning = false;
        });
    }
    
    // Snapshot alle 1 Sekunde aktualisieren
    setInterval(updateSnapshot, 1000);
    updateSnapshot(); // Sofort erstes Bild laden

    function drawPoints() {
      console.log('drawPoints aufgerufen, ambilightData:', ambilightData ? 'vorhanden' : 'null');
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      // Ambilight-Rechtecke zeichnen (falls vorhanden)
      if (ambilightData) {
        drawAmbilightRects(ambilightData);
      } else {
        console.log('Keine Ambilight-Daten zum Zeichnen');
      }

      // Punkte zeichnen
      ctx.fillStyle = 'red';
      points.forEach(p => {
        ctx.beginPath();
        ctx.arc(p.x, p.y, 5, 0, 2 * Math.PI);
        ctx.fill();
      });

      // Verbinde Punkte, sobald vier gesetzt wurden
      if (points.length === 4) {
        ctx.strokeStyle = 'lime';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(points[0].x, points[0].y);
        ctx.lineTo(points[1].x, points[1].y);
        ctx.lineTo(points[2].x, points[2].y);
        ctx.lineTo(points[3].x, points[3].y);
        ctx.lineTo(points[0].x, points[0].y); // schließt das Viereck
        ctx.stroke();

        // gelbe Punkte aus Serverantwort zeichnen
        ctx.fillStyle = 'yellow';
        gridPts.forEach(g => {
          ctx.beginPath();
          ctx.arc(g.x, g.y, 3, 0, 2 * Math.PI);
          ctx.fill();
        });
      }
    }

    function drawAmbilightRects(data) {
      // Zeichne alle Ambilight-Rechtecke mit ihren berechneten Farben
      // WICHTIG: ESP32 sendet Koordinaten für 320x240, Canvas ist 640x480
      // -> Koordinaten müssen x2 skaliert werden!
      const scale = 2.0;
      
      console.log('drawAmbilightRects aufgerufen mit Daten:', data);
      
      const sides = ['top', 'bottom', 'left', 'right'];
      let totalRects = 0;
      
      sides.forEach(side => {
        const colors = data[side] || [];
        const rects = data[side + 'Rects'] || [];
        
        console.log(`${side}: ${colors.length} Farben, ${rects.length} Rechtecke`);
        
        for (let i = 0; i < colors.length && i < rects.length; i++) {
          const [r, g, b] = colors[i];
          const rect = rects[i];
          
          // Skaliere Koordinaten zurück auf Canvas-Größe
          const x1 = rect.x1 * scale;
          const y1 = rect.y1 * scale;
          const x2 = rect.x2 * scale;
          const y2 = rect.y2 * scale;
          
          if (i === 0) {
            console.log(`${side}[0]: Farbe rgb(${r},${g},${b}), Rect (${x1},${y1}) -> (${x2},${y2})`);
          }
          
          // Fülle das Rechteck mit der berechneten Farbe
          ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
          ctx.fillRect(x1, y1, x2 - x1, y2 - y1);
          
          // Zeichne grünen Rahmen für bessere Sichtbarkeit
          ctx.strokeStyle = 'rgba(0, 255, 0, 0.5)';
          ctx.lineWidth = 1;
          ctx.strokeRect(x1, y1, x2 - x1, y2 - y1);
          totalRects++;
        }
      });
      
      console.log(`Insgesamt ${totalRects} Rechtecke gezeichnet`);
    }

    // === KONTINUIERLICHES POLLING ===
    let pollingInterval = null;

    // Holt Ambilight-Daten vom Server (GET - keine Berechnung, nur Abruf)
    function fetchAmbilightData() {
      if (points.length !== 4) {
        return; // Kein Log mehr, zu viel Spam
      }
      
      console.log('Polling: Hole Daten...');
      fetch('/api/ambilight')
        .then(r => {
          console.log('Polling: Response erhalten, Status:', r.status, r.statusText);
          return r.text();
        })
        .then(text => {
          console.log('Polling: Response-Text (erste 200 Zeichen):', text.substring(0, 200));
          const data = JSON.parse(text);
          console.log('Polling: JSON geparst:', data);
          if (!data.error) {
            ambilightData = data;
            console.log('Polling: ambilightData gesetzt, rufe drawPoints()...');
            drawPoints();
          } else {
            console.error('Polling: Server-Fehler:', data.error);
          }
        })
        .catch(err => {
          console.error('Polling-Fehler:', err);
        });
    }

    // Startet automatisches Polling (nur zur Visualisierung)
    function startPolling() {
      if (pollingInterval) return;
      console.log('Starte Polling...');
      pollingInterval = setInterval(fetchAmbilightData, 2000); // Alle 2000ms (0.5 Hz)
      fetchAmbilightData(); // Sofort einmal ausführen
    }

    // Stoppt Polling
    function stopPolling() {
      if (pollingInterval) {
        console.log('Stoppe Polling...');
        clearInterval(pollingInterval);
        pollingInterval = null;
      }
    }

    // Klick auf Bild registrieren
    document.getElementById('stream-container').addEventListener('click', (e) => {
      if (points.length >= 4) return; // Maximal vier Punkte

      const rect = canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;

      // Canvas benötigt pointer-events, um Koordinaten exakt zu ermitteln
      points.push({ x, y });

      const li = document.createElement('li');
      li.textContent = `(${Math.round(x)}, ${Math.round(y)})`;
      coordList.appendChild(li);

      drawPoints();

      // Wenn vier Punkte vorhanden, Konfiguration an Server schicken
      if (points.length === 4) {
        const payload = {
          points,
          hSeg: parseInt(document.getElementById('hSeg').value) || 16,
          vSeg: parseInt(document.getElementById('vSeg').value) || 10
        };
        
        console.log('Sende Config:', payload);
        
        // Grid-Request für Visualisierung der Zwischenpunkte
        fetch('/api/grid', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        })
        .then(r => r.json())
        .then(data => {
          console.log('Grid-Response:', data);
          gridPts = data.points || [];
          drawPoints();
        })
        .catch(console.error);
        
        // Config-Request für Ambilight-Berechnung
        fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        })
        .then(r => r.json())
        .then(data => {
          console.log('Config gesetzt:', data);
        })
        .catch(console.error);
      }
    });

    // Punkte zurücksetzen
    resetBtn.addEventListener('click', () => {
      points = [];
      gridPts = [];
      ambilightData = null;
      coordList.innerHTML = '';
      drawPoints();
    });

    // Button für manuelles Update
    ambilightBtn.addEventListener('click', () => {
      console.log('Manuelles Update angefordert');
      fetchAmbilightData();
    });

    // Polling automatisch starten
    startPolling();

    // Polling stoppen beim Verlassen der Seite
    window.addEventListener('beforeunload', stopPolling);
  </script>
</body>
</html>
)rawliteral";

#endif
