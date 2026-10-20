#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <time.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const String phoneNumber = "YOUR_PHONE_E164";
const String apiKey = "YOUR_API_KEY";

const int switchPin = 15;
int lastState = HIGH;
String doorState = "Unknown";

WebServer server(80);

#define MAX_LOGS 100
String messageLog[MAX_LOGS];
int logIndex = 0;
int totalLogsStored = 0;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -4 * 3600;
const int   daylightOffset_sec = 0;

void addToLog(const String& msg) {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%m/%d/%Y %H:%M:%S", &timeinfo);

  String entry = "[" + String(buffer) + "] " + msg;

  messageLog[logIndex] = entry;
  logIndex = (logIndex + 1) % MAX_LOGS;

  if (totalLogsStored < MAX_LOGS) {
    totalLogsStored++;
  }
}

void clearLogs() {
  for (int i = 0; i < MAX_LOGS; i++) {
    messageLog[i] = "";
  }
  logIndex = 0;
  totalLogsStored = 0;
}

String urlEncode(const String& msg) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < msg.length(); i++) {
    c = msg.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (('a' <= c && c <= 'z') ||
               ('A' <= c && c <= 'Z') ||
               ('0' <= c && c <= '9')) {
      encoded += c;
    } else {
      encoded += '%';
      code0 = (c >> 4) & 0xF;
      code1 = c & 0xF;
      encoded += (code0 > 9) ? (code0 - 10 + 'A') : (code0 + '0');
      encoded += (code1 > 9) ? (code1 - 10 + 'A') : (code1 + '0');
    }
  }
  return encoded;
}

void sendWhatsAppMessage(const String& message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String encodedMessage = urlEncode(message);
    String url = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber + "&text=" + encodedMessage + "&apikey=" + apiKey;

    Serial.println("Sending message: " + message);
    http.begin(url);
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String resp = http.getString();
      Serial.println("✅ WhatsApp sent: " + resp);
      addToLog("Sent: " + message + " | Response: " + resp);
    } else {
      Serial.println("❌ Failed to send WhatsApp. Error code: " + String(httpResponseCode));
      addToLog("Failed: " + message + " | Error code: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("❌ Not connected to WiFi");
    addToLog("Failed: " + message + " | No WiFi");
  }
}

String getLogsForDate(const String& dateStr) {
  String html = "<ul>";
  for (int i = 0; i < MAX_LOGS; i++) {
    int idx = (logIndex + i) % MAX_LOGS;
    if (messageLog[idx].length() > 0) {
      if (messageLog[idx].startsWith("[" + dateStr)) {
        html += "<li>" + messageLog[idx] + "</li>";
      }
    }
  }
  html += "</ul>";
  return html;
}

String getAllLogsCSV() {
  String csv = "Date,Status,Time\n";
  for (int i = 0; i < MAX_LOGS; i++) {
    int idx = (logIndex + i) % MAX_LOGS;
    String entry = messageLog[idx];
    if (entry.length() > 0) {
      int firstBracket = entry.indexOf('[');
      int secondBracket = entry.indexOf(']');
      if (firstBracket != -1 && secondBracket != -1) {
        String datetime = entry.substring(firstBracket + 1, secondBracket);
        String msg = entry.substring(secondBracket + 2);
        int spaceIndex = datetime.indexOf(' ');
        if (spaceIndex != -1) {
          String date = datetime.substring(0, spaceIndex);
          String time = datetime.substring(spaceIndex + 1);
          String status = "";
          if (msg.indexOf("Door opened") != -1) status = "Open";
          else if (msg.indexOf("Door closed") != -1) status = "Closed";
          else status = msg;

          csv += date + "," + status + "," + time + "\n";
        }
      }
    }
  }
  return csv;
}

String getMainPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Door Status Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      margin: 0; padding: 0;
      background: #f9fafb;
      color: #222;
    }
    header {
      background-color: #1e40af;
      color: white;
      padding: 1.2rem;
      text-align: center;
      font-weight: 700;
      font-size: 1.8rem;
      box-shadow: 0 2px 6px rgba(0,0,0,0.15);
    }
    .status {
      text-align: center;
      font-size: 1.4rem;
      margin: 1.5rem auto;
      max-width: 400px;
      background: white;
      padding: 1rem 1.5rem;
      border-radius: 8px;
      box-shadow: 0 1px 6px rgba(0,0,0,0.1);
    }
    .log-count {
      text-align: center;
      font-size: 1.2rem;
      margin: 0.5rem auto 1.5rem;
      max-width: 400px;
      background: #e0e7ff;
      padding: 0.6rem 1.5rem;
      border-radius: 8px;
      color: #1e40af;
      font-weight: 600;
      box-shadow: 0 1px 4px rgba(30, 64, 175, 0.3);
    }
    /* Bar container */
    .bar-container {
      width: 90%;
      max-width: 420px;
      height: 28px;
      background-color: #ddd;
      border-radius: 14px;
      margin: 0 auto 1.5rem;
      box-shadow: inset 0 2px 4px rgba(0,0,0,0.1);
    }
    /* The dynamic bar */
    .bar-fill {
      height: 100%;
      width: 0%;
      border-radius: 14px;
      transition: width 0.5s ease, background-color 0.5s ease;
    }
    .calendar {
      max-width: 420px;
      margin: 1rem auto 3rem;
      background: white;
      border-radius: 14px;
      box-shadow: 0 3px 15px rgba(0,0,0,0.12);
      padding: 1.5rem 1.5rem 2rem;
    }
    .calendar-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 0.6rem;
    }
    .calendar-header button {
      background: #1e40af;
      border: none;
      color: white;
      font-size: 1.4rem;
      font-weight: 600;
      padding: 0.3rem 0.8rem;
      border-radius: 6px;
      cursor: pointer;
      transition: background-color 0.3s ease;
    }
    .calendar-header button:hover {
      background: #3b82f6;
    }
    #homeBtn {
      background: #10b981;
      margin-left: 0.6rem;
    }
    #homeBtn:hover {
      background: #34d399;
    }
    .calendar-header span {
      font-weight: 700;
      font-size: 1.3rem;
      color: #1e40af;
    }
    .calendar-grid {
      display: grid;
      grid-template-columns: repeat(7, 1fr);
      gap: 0.25rem;
      text-align: center;
    }
    .day-name {
      font-weight: 600;
      color: #6b7280;
      user-select: none;
    }
    .day {
      padding: 0.6rem;
      border-radius: 50%;
      cursor: pointer;
      transition: background-color 0.2s ease;
      user-select: none;
      color: #374151;
    }
    .day:hover {
      background: #e0e7ff;
      color: #1e40af;
    }
    .today {
      background: #2563eb;
      color: white;
      font-weight: 700;
      box-shadow: 0 0 6px #2563ebaa;
    }
    #log {
      max-width: 420px;
      margin: 0 auto 2rem;
      padding: 1rem 1.2rem;
      background: #ffffff;
      border-radius: 10px;
      box-shadow: 0 2px 12px rgba(0,0,0,0.06);
      font-family: monospace;
      white-space: pre-wrap;
      display: none;
    }
    #saveLogsBtn, #clearLogsBtn {
      display: block;
      margin: 0 auto 1rem auto;
      background: #2563eb;
      color: white;
      border: none;
      padding: 0.8rem 1.5rem;
      font-size: 1.1rem;
      font-weight: 600;
      border-radius: 8px;
      cursor: pointer;
      box-shadow: 0 2px 6px rgba(37, 99, 235, 0.7);
      transition: background-color 0.3s ease;
      max-width: 420px;
      text-align: center;
    }
    #saveLogsBtn:hover, #clearLogsBtn:hover {
      background: #3b82f6;
    }
  </style>
  <script>
    let today = new Date();
    let selectedDate = new Date();

    function renderCalendar(date) {
      const month = date.getMonth();
      const year = date.getFullYear();
      const firstDay = new Date(year, month, 1).getDay();
      const lastDate = new Date(year, month + 1, 0).getDate();

      const grid = document.getElementById('calendar-grid');
      grid.innerHTML = '';

      document.getElementById('calendar-title').innerText = date.toLocaleString('default', { month: 'long', year: 'numeric' });

      const weekdays = ['S','M','T','W','T','F','S'];
      weekdays.forEach(day => {
        const cell = document.createElement('div');
        cell.innerText = day;
        cell.className = 'day-name';
        grid.appendChild(cell);
      });

      for (let i = 0; i < firstDay; i++) {
        const emptyCell = document.createElement('div');
        grid.appendChild(emptyCell);
      }

      for (let d = 1; d <= lastDate; d++) {
        const cell = document.createElement('div');
        cell.innerText = d;
        cell.className = 'day';

        const cellDate = new Date(year, month, d);

        if (cellDate.getFullYear() === today.getFullYear() &&
            cellDate.getMonth() === today.getMonth() &&
            cellDate.getDate() === today.getDate()) {
          cell.classList.add('today');
        }

        cell.onclick = () => {
          selectedDate = cellDate;
          fetchLogsForDate(cellDate);
        };
        grid.appendChild(cell);
      }

      document.getElementById('log').style.display = 'none';
      document.getElementById('log').innerHTML = '';
    }

    function changeMonth(offset) {
      selectedDate.setMonth(selectedDate.getMonth() + offset);
      renderCalendar(selectedDate);
    }

    function goHome() {
      selectedDate = new Date(today);
      renderCalendar(selectedDate);
      fetchLogsForDate(selectedDate);
    }

    function fetchLogsForDate(date) {
      const mm = String(date.getMonth() + 1).padStart(2, '0');
      const dd = String(date.getDate()).padStart(2, '0');
      const yyyy = date.getFullYear();
      const dateStr = `${mm}/${dd}/${yyyy}`;
      fetch('/log?date=' + encodeURIComponent(dateStr))
        .then(resp => resp.text())
        .then(text => {
          const logDiv = document.getElementById('log');
          logDiv.innerHTML = text;
          logDiv.style.display = 'block';
        });
    }

    function refreshStatus() {
      fetch('/status')
        .then(resp => resp.text())
        .then(text => {
          document.getElementById('doorState').innerText = text;
        });
    }

    function refreshLogCount() {
      fetch('/logCount')
        .then(resp => resp.text())
        .then(text => {
          document.getElementById('logCount').innerText = text;

          let count = parseInt(text);
          if (isNaN(count)) count = 0;
          if (count > 100) count = 100;

          const bar = document.getElementById('logBar');
          bar.style.width = count + '%';

          if (count <= 33) {
            bar.style.backgroundColor = '#10b981'; // green
          } else if (count <= 66) {
            bar.style.backgroundColor = '#fbbf24'; // yellow
          } else {
            bar.style.backgroundColor = '#ef4444'; // red
          }
        });
    }

    function saveLogs() {
      window.location.href = '/downloadLogs';
    }

    function clearLogs() {
      let confirmation = prompt('Type "Delete" to confirm clearing all logs:');
      if (confirmation === 'Delete') {
        fetch('/clearLogs', { method: 'POST' })
          .then(resp => resp.text())
          .then(text => {
            alert(text);
            refreshLogCount();
            // Refresh logs and calendar
            fetchLogsForDate(selectedDate);
          })
          .catch(() => alert('Failed to clear logs.'));
      } else {
        alert('Clear logs canceled or incorrect confirmation.');
      }
    }

    window.onload = function() {
      renderCalendar(today);
      refreshStatus();
      fetchLogsForDate(today);
      refreshLogCount();
      setInterval(refreshStatus, 3000);
      setInterval(refreshLogCount, 5000);
    };
  </script>
</head>
<body>
  <header>Door Status Monitor</header>
  <div class="status">Door is currently: <span id="doorState">Loading...</span></div>

  <div class="log-count" id="logCount">Loading log count...</div>
  <div class="bar-container">
    <div id="logBar" class="bar-fill"></div>
  </div>

  <button id="saveLogsBtn" onclick="saveLogs()">Save Logs</button>
  <button id="clearLogsBtn" onclick="clearLogs()">Clear Logs</button>

  <div class="calendar">
    <div class="calendar-header">
      <button onclick="changeMonth(-1)" aria-label="Previous Month">&#8592;</button>
      <span id="calendar-title"></span>
      <button onclick="changeMonth(1)" aria-label="Next Month">&#8594;</button>
      <button id="homeBtn" onclick="goHome()" aria-label="Today">Home</button>
    </div>
    <div id="calendar-grid" class="calendar-grid"></div>
  </div>
  <div id="log"></div>
</body>
</html>
  )rawliteral";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", getMainPage());
}

void handleStatus() {
  server.send(200, "text/plain", doorState);
}

void handleLog() {
  if (server.hasArg("date")) {
    String date = server.arg("date");
    server.send(200, "text/html", getLogsForDate(date));
  } else {
    server.send(200, "text/html", getLogsForDate(""));
  }
}

void handleDownloadLogs() {
  String csv = getAllLogsCSV();
  server.sendHeader("Content-Disposition", "attachment; filename=\"door_logs.csv\"");
  server.send(200, "text/csv", csv);
}

void handleLogCount() {
  server.send(200, "text/plain", String(totalLogsStored));
}

void handleClearLogs() {
  if (server.method() == HTTP_POST) {
    clearLogs();
    server.send(200, "text/plain", "All logs cleared.");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void setup() {
  pinMode(switchPin, INPUT_PULLUP);
  Serial.begin(115200);

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Waiting for time synchronization...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synchronized");

  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/log", handleLog);
  server.on("/downloadLogs", handleDownloadLogs);
  server.on("/logCount", handleLogCount);
  server.on("/clearLogs", HTTP_POST, handleClearLogs);

  server.begin();
  Serial.println("HTTP server started");

  doorState = digitalRead(switchPin) == LOW ? "Open" : "Closed";
  lastState = digitalRead(switchPin);
}

void loop() {
  server.handleClient();

  int currentState = digitalRead(switchPin);

  if (currentState != lastState) {
    if (currentState == LOW) {
      doorState = "Open";
      Serial.println("Door opened");
      sendWhatsAppMessage("Door opened");
      addToLog("Door opened");
    } else {
      doorState = "Closed";
      Serial.println("Door closed");
      sendWhatsAppMessage("Door closed");
      addToLog("Door closed");
    }
    lastState = currentState;
    delay(10000);
  }

  delay(50);
}