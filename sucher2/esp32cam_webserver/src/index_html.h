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
    <img id="stream" src="/stream" width="640" height="480"/>
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
    <button id="ambilight" style="margin-top:10px;">Ambilight berechnen</button>
  </div>

  <script>
    console.log('JavaScript startet...');
    const canvas = document.getElementById('overlay');
    const ctx = canvas.getContext('2d');
    const coordList = document.getElementById('coordList');
    const resetBtn = document.getElementById('reset');
    const ambilightBtn = document.getElementById('ambilight');
    console.log('Buttons gefunden:', { reset: !!resetBtn, ambilight: !!ambilightBtn });
    let points = [];
    let gridPts = [];
    let ambilightData = null;

    function drawPoints() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      // Ambilight-Rechtecke zeichnen (falls vorhanden)
      if (ambilightData) {
        drawAmbilightRects(ambilightData);
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
      const sides = ['top', 'bottom', 'left', 'right'];
      
      sides.forEach(side => {
        const colors = data[side] || [];
        const rects = data[side + 'Rects'] || [];
        
        for (let i = 0; i < colors.length && i < rects.length; i++) {
          const [r, g, b] = colors[i];
          const rect = rects[i];
          
          // Fülle das Rechteck mit der berechneten Farbe
          ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
          ctx.fillRect(rect.x1, rect.y1, rect.x2 - rect.x1, rect.y2 - rect.y1);
          
          // Zeichne grünen Rahmen für bessere Sichtbarkeit
          ctx.strokeStyle = 'rgba(0, 255, 0, 0.5)';
          ctx.lineWidth = 1;
          ctx.strokeRect(rect.x1, rect.y1, rect.x2 - rect.x1, rect.y2 - rect.y1);
        }
      });
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

      // Wenn vier Punkte vorhanden, an Server schicken
      if (points.length === 4) {
        const payload = {
          points,
          hSeg: parseInt(document.getElementById('hSeg').value) || 1,
          vSeg: parseInt(document.getElementById('vSeg').value) || 1
        };
        fetch('/api/grid', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        })
        .then(r => r.json())
        .then(data => {
          gridPts = data.points || [];
          drawPoints();
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

    // Ambilight berechnen
    ambilightBtn.addEventListener('click', () => {
      console.log('Ambilight-Button geklickt!', { punkteAnzahl: points.length });
      if (points.length !== 4) {
        alert('Bitte zuerst 4 Eckpunkte setzen!');
        return;
      }

      const payload = {
        points,
        hSeg: parseInt(document.getElementById('hSeg').value) || 16,
        vSeg: parseInt(document.getElementById('vSeg').value) || 10
      };
      console.log('Sende Ambilight-Request:', payload);

      ambilightBtn.textContent = 'Berechne...';
      ambilightBtn.disabled = true;

      fetch('/api/ambilight', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      })
      .then(r => r.json())
      .then(data => {
        if (data.error) {
          alert('Fehler: ' + data.error);
        } else {
          ambilightData = data;
          drawPoints();
          console.log('Ambilight-Daten empfangen:', data);
        }
      })
      .catch(err => {
        console.error('Ambilight-Fehler:', err);
        alert('Fehler beim Abrufen der Ambilight-Daten');
      })
      .finally(() => {
        ambilightBtn.textContent = 'Ambilight berechnen';
        ambilightBtn.disabled = false;
      });
    });
  </script>
</body>
</html>
)rawliteral";

#endif
