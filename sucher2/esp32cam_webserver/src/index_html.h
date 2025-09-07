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
  <h1>ESP32-CAM Live Stream (VGA)</h1>
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
  </div>

  <script>
    const canvas = document.getElementById('overlay');
    const ctx = canvas.getContext('2d');
    const coordList = document.getElementById('coordList');
    const resetBtn = document.getElementById('reset');
    let points = [];
    let gridPts = [];

    function drawPoints() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
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
      coordList.innerHTML = '';
      drawPoints();
    });
  </script>
</body>
</html>
)rawliteral";

#endif
