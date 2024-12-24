// MQTT configuration
const broker = 'broker.hivemq.com';
const wsPort = 8884; // เปลี่ยนเป็น secure port
const deviceId = 'esp32_timer_relay_01_web' + Math.random().toString(16).substr(2, 8);
const mqttClient = new Paho.MQTT.Client(broker, wsPort, deviceId);

// MQTT topics ใช้ topic ที่เฉพาะเจาะจงมากขึ้น
const statusTopic = 'singfirewire/relay/status'; 
const relay1Topic = 'singfirewire/relay1/control';
const relay2Topic = 'singfirewire/relay2/control'; 

mqttClient.onConnectionLost = onConnectionLost;
mqttClient.onMessageArrived = onMessageArrived;

// Connect to MQTT broker
function connectMQTT() {
   const options = {
       timeout: 3,
       useSSL: true, // เพิ่ม SSL
       onSuccess: onConnect,
       onFailure: onFailure,
       reconnect: true, // เพิ่มการ reconnect อัตโนมัติ
       keepAliveInterval: 30 // ส่ง ping ทุก 30 วินาที
   };
   mqttClient.connect(options);
}

function onConnect() {
   console.log('Connected to MQTT broker');
   mqttClient.subscribe(statusTopic);
   // Subscribe แบบ QoS 1 เพื่อให้แน่ใจว่าได้รับข้อความ
   const subscribeOptions = {
       qos: 1
   };
   mqttClient.subscribe(statusTopic, subscribeOptions);
}

function onFailure(message) {
   console.log('Failed to connect: ' + message.errorMessage);
   // เพิ่มการแสดงสถานะการเชื่อมต่อ
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
       timestamp: Date.now() // เพิ่ม timestamp
   }));
   message.destinationName = topic;
   message.qos = 1; // ใช้ QoS 1
   message.retained = false;

   try {
       mqttClient.send(message);
   } catch (e) {
       console.error('Error sending message:', e);
   }
}

function updateStatus(status) {
   try {
       // อัพเดทสถานะ WiFi และ MQTT
       const mqttStatus = mqttClient.isConnected() ? 'Connected' : 'Disconnected';
       document.getElementById('wifi-status').textContent = 
           `WiFi: ${status.wifi_connected ? 'Connected' : 'Disconnected'} | MQTT: ${mqttStatus}`;
       
       document.getElementById('signal-strength').textContent = 
           `RSSI: ${status.wifi_rssi} dBm`;

       // อัพเดทเวลาของ Relay 1
       if (status.relay1?.active) {
           const minutes = Math.floor(status.relay1.remaining_seconds / 60);
           const seconds = status.relay1.remaining_seconds % 60;
           document.getElementById('timer1').textContent = 
               `${minutes}:${seconds.toString().padStart(2, '0')}`;
       } else {
           document.getElementById('timer1').textContent = '40:00';
       }

       // อัพเดทเวลาของ Relay 2
       if (status.relay2?.active) {
           const minutes = Math.floor(status.relay2.remaining_seconds / 60);
           const seconds = status.relay2.remaining_seconds % 60;
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
