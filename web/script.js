// MQTT configuration
const broker = 'broker.hivemq.com';
const wsPort = 8000;
const deviceId = 'esp32_timer_relay_01_web' + Math.random().toString(16).substr(2, 8);
const mqttClient = new Paho.MQTT.Client(broker, wsPort, deviceId);

// MQTT topics
const statusTopic = 'home/relay/status';
const relay1Topic = 'home/relay1/control';
const relay2Topic = 'home/relay2/control';

mqttClient.onConnectionLost = onConnectionLost;
mqttClient.onMessageArrived = onMessageArrived;

// Connect to MQTT broker
function connectMQTT() {
    const options = {
        timeout: 3,
        onSuccess: onConnect,
        onFailure: onFailure
    };
    mqttClient.connect(options);
}

function onConnect() {
    console.log('Connected to MQTT broker');
    mqttClient.subscribe(statusTopic);
}

function onFailure(message) {
    console.log('Failed to connect: ' + message.errorMessage);
    setTimeout(connectMQTT, 5000);
}

function onConnectionLost(responseObject) {
    if (responseObject.errorCode !== 0) {
        console.log('Connection lost: ' + responseObject.errorMessage);
        setTimeout(connectMQTT, 5000);
    }
}

function onMessageArrived(message) {
    const status = JSON.parse(message.payloadString);
    updateStatus(status);
}

function controlRelay(relayNumber, action) {
    const topic = relayNumber === 1 ? relay1Topic : relay2Topic;
    const message = new Paho.MQTT.Message(JSON.stringify({action: action}));
    message.destinationName = topic;
    mqttClient.send(message);
}

function updateStatus(status) {
    // อัพเดทสถานะ WiFi
    document.getElementById('wifi-status').textContent = 
        `WiFi: ${status.wifi_connected ? 'เชื่อมต่อแล้ว' : 'ไม่ได้เชื่อมต่อ'}`;
    document.getElementById('signal-strength').textContent = 
        `RSSI: ${status.wifi_rssi} dBm`;

    // อัพเดทเวลาของ Relay 1
    if (status.relay1.active) {
        const minutes = Math.floor(status.relay1.remaining_seconds / 60);
        const seconds = status.relay1.remaining_seconds % 60;
        document.getElementById('timer1').textContent = 
            `${minutes}:${seconds.toString().padStart(2, '0')}`;
    } else {
        document.getElementById('timer1').textContent = '40:00';
    }

    // อัพเดทเวลาของ Relay 2
    if (status.relay2.active) {
        const minutes = Math.floor(status.relay2.remaining_seconds / 60);
        const seconds = status.relay2.remaining_seconds % 60;
        document.getElementById('timer2').textContent = 
            `${minutes}:${seconds.toString().padStart(2, '0')}`;
    } else {
        document.getElementById('timer2').textContent = '40:00';
    }
}

// เริ่มต้นเชื่อมต่อ MQTT เมื่อโหลดหน้าเว็บ
connectMQTT();
