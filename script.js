// MQTT configuration
const broker = 'broker.hivemq.com';
const wsPort = 443; // Alternative port for MQTT over WSS
const deviceId = 'esp32_timer_relay_01_web' + Math.random().toString(16).substr(2, 8);
const mqttClient = new Paho.MQTT.Client(broker, wsPort, deviceId);

// MQTT topics
const statusTopic = 'singfirewire/relay/status';
const relay1Topic = 'singfirewire/relay/relay1/control'; // แก้ไข MQTT topic
const relay2Topic = 'singfirewire/relay/relay2/control'; // แก้ไข MQTT topic

mqttClient.onConnectionLost = onConnectionLost;
mqttClient.onMessageArrived = onMessageArrived;

let resultText = ""; // ประกาศตัวแปร resultText ไว้ด้านนอก

document.addEventListener('DOMContentLoaded', function () {
  // จัดการ click event สำหรับทุกปุ่ม
  document.querySelectorAll('button').forEach(button => {
    button.addEventListener('click', function (e) {
      e.preventDefault(); // เพิ่ม e.preventDefault() เพื่อป้องกัน default behavior ของ browser
      const relay = this.dataset.relay;
      const action = this.dataset.action;
      if (relay && action) {
        controlRelay(parseInt(relay), action);
      }
    });

    // เพิ่ม touch events สำหรับมือถือ
    button.addEventListener('touchstart', function (e) {
      e.preventDefault();
      this.click();
    });
  });
});

// Connect to MQTT broker
function connectMQTT() {
  const options = {
    timeout: 3,
    useSSL: true,
    onSuccess: onConnect,
    onFailure: onFailure,
    reconnect: true,
    keepAliveInterval: 30
  };
  mqttClient.connect(options);
}

function onConnect() {
  console.log('Connected to MQTT broker');
  const subscribeOptions = {
    qos: 1
  };
  mqttClient.subscribe(statusTopic, subscribeOptions);
}

function onFailure(message) {
  console.log('Failed to connect: ' + message.errorMessage);
  document.getElementById('wifi-status').textContent = 'MQTT: Disconnected';
  setTimeout(connectMQTT, 5000);
}

function onConnectionLost(responseObject) {
  if (responseObject.errorCode !== 0) {
    console.log('Connection lost: ' + responseObject.errorMessage);
    document.getElementById('wifi-status').textContent = 'MQTT: Connection Lost';
    setTimeout(connectMQTT, 5000);
  }
}

function onMessageArrived(message) {
  try {
    const status = JSON.parse(message.payloadString);
    updateStatus(status);
  } catch (e) {
    console.error('Error parsing message:', e);
  }
}

function controlRelay(relayNumber, action) {
  if (!mqttClient.isConnected()) {
    console.log('Not connected to MQTT broker');
    return;
  }

  const topic = relayNumber === 1 ? relay1Topic : relay2Topic;
  const message = new Paho.MQTT.Message(JSON.stringify({
    action: action,
    timestamp: Date.now()
  }));
  message.destinationName = topic;
  message.qos = 1;
  message.retained = false;

  try {
    mqttClient.send(message);
  } catch (e) {
    console.error('Error sending message:', e);
  }
}

function updateStatus(status) {
  try {
    const mqttStatus = mqttClient.isConnected() ? 'Connected' : 'Disconnected';
    document.getElementById('wifi-status').textContent =
      `WiFi: ${status.wifi_connected ? 'เชื่อมต่อแล้ว' : 'ไม่ได้เชื่อมต่อ'} | MQTT: ${mqttStatus}`;

    document.getElementById('signal-strength').textContent =
      `RSSI: ${status.wifi_rssi} dBm`;

    if (status.relay1?.active) {
      const minutes = Math.floor(status.relay1.remaining / 60); // แก้ไข field name
      const seconds = status.relay1.remaining % 60; // แก้ไข field name
      document.getElementById('timer1').textContent =
        `${minutes}:${seconds.toString().padStart(2, '0')}`;
    } else {
      document.getElementById('timer1').textContent = '40:00';
    }

    if (status.relay2?.active) {
      const minutes = Math.floor(status.relay2.remaining / 60); // แก้ไข field name
      const seconds = status.relay2.remaining % 60; // แก้ไข field name
      document.getElementById('timer2').textContent =
        `${minutes}:${seconds.toString().padStart(2, '0')}`;
    } else {
      document.getElementById('timer2').textContent = '40:00';
    }
  } catch (e) {
    console.error('Error updating status:', e);
  }
}

// Start MQTT connection
connectMQTT();

// Reconnect periodically if disconnected
setInterval(() => {
  if (!mqttClient.isConnected()) {
    connectMQTT();
  }
}, 10000);
