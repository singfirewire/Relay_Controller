// MQTT configuration
const broker = 'broker.hivemq.com';
const wsPort = 443; // Alternative port for MQTT over WSS
const deviceId = 'esp32_timer_relay_01_web' + Math.random().toString(16).substr(2, 8);
const mqttClient = new Paho.MQTT.Client(broker, wsPort, deviceId);

// MQTT topics
const statusTopic = 'home/relay/status';
const relay1Topic = 'home/relay/relay1/control';
const relay2Topic = 'home/relay/relay2/control';

mqttClient.onConnectionLost = onConnectionLost;
mqttClient.onMessageArrived = onMessageArrived;

document.addEventListener('DOMContentLoaded', () => {
  document.querySelectorAll('button').forEach(button => {
    button.addEventListener('click', (e) => {
      e.preventDefault();
      const relay = button.dataset.relay;
      const action = button.dataset.action;
      if (relay && action) {
        controlRelay(parseInt(relay), action);
      }
    });
    button.addEventListener('touchstart', (e) => {
      e.preventDefault();
      button.click();
    });
  });
});

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
  const subscribeOptions = { qos: 1 };
  mqttClient.subscribe(statusTopic, subscribeOptions);
  mqttClient.subscribe('home/relay/command', subscribeOptions);
  const message = new Paho.MQTT.Message(JSON.stringify({ command: "status" }));
  message.destinationName = 'home/relay/command';
  mqttClient.send(message);
}

function updateStatus(status) {
  try {
    if (status.hasOwnProperty('wifi_connected')) {
      if (!status.wifi_connected) {
        document.getElementById('wifi-status').textContent = 'WiFi: ไม่ได้เชื่อมต่อ';
      } else {
        document.getElementById('wifi-status').textContent = 'WiFi: เชื่อมต่อแล้ว';
      }
    } else {
      document.getElementById('wifi-status').textContent = 'WiFi: เชื่อมต่อไม่ได้';
    }
    document.getElementById('signal-strength').textContent = `RSSI: ${status.wifi_rssi} dBm`;

    if (status.relay1?.active) {
      const minutes = Math.floor(status.relay1.remaining / 60);
      const seconds = status.relay1.remaining % 60;
      document.getElementById('timer1').textContent = `${minutes}:${seconds.toString().padStart(2, '0')}`;
    } else {
      document.getElementById('timer1').textContent = '40:00';
    }

    if (status.relay2?.active) {
      const minutes = Math.floor(status.relay2.remaining / 60);
      const seconds = status.relay2.remaining % 60;
      document.getElementById('timer2').textContent = `${minutes}:${seconds.toString().padStart(2, '0')}`;
    } else {
      document.getElementById('timer2').textContent = '40:00';
    }
  } catch (e) {
    console.error('Error updating status:', e);
  }
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
  const message = new Paho.MQTT.Message(JSON.stringify({ action, timestamp: Date.now() }));
  message.destinationName = topic;
  message.qos = 1;
  message.retained = false;

  try {
    mqttClient.send(message);
  } catch (e) {
    console.error('Error sending message:', e);
  }
}

connectMQTT();
setInterval(() => {
  if (!mqttClient.isConnected()) {
    connectMQTT();
  }
}, 10000);
